//---------------------------------------------------------------------------
// scenario.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/sched.h>

#include "netlink_kernel.h"

#include "target.h"
#include "scenario.h"
#include "target_generic_output.h"

#define DEBUG 1

#if DEBUG
#define KFREE(arg) { \
   delay_printk("kfree(0x%08X) - %s : %i\n", arg, __func__, __LINE__); \
   kfree(arg); \
}
#else
#define KFREE(arg) { \
   kfree(arg); \
}
#endif

static inline void* KMALLOC2 (size_t size, int a, const char *func, int line) {
   void *ret = kmalloc(size, a);
   #if DEBUG
      delay_printk("kmalloc(0x%08X) - %s : %i\n", ret, func, line);
   #endif
   return ret;
}

#define KMALLOC(arg1, arg2) KMALLOC2(arg1, arg2, __func__, __LINE__)

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This atomic variable is use to hold our driver id from netlink provider
//---------------------------------------------------------------------------
atomic_t driver_id = ATOMIC_INIT(-1);

//---------------------------------------------------------------------------
// This atomic variable is use to tell if we're in a scenario or not
//---------------------------------------------------------------------------
atomic_t scenario_running = ATOMIC_INIT(0); // running or going to run a scenario
atomic_t scenario_stopping = ATOMIC_INIT(0); // stopping the scenario

//---------------------------------------------------------------------------
// Internal data structures related to currently running scenario
//---------------------------------------------------------------------------
static char *scen_buffer; // start of scenario buffer
int scen_length; // length of scenario buffer
static struct work_struct scenario_work;
static struct work_struct end_scenario_work;
typedef int (*scenario_function)(char*,int,char*,int,char*,int,char*,int); // function typedef for scenario prebuilt functions (four parameters given as pointers and lengths) (return 1 for resume later, 0 for continue)
typedef struct string_match {
   const char * string;
   int number;
} string_match_t;
#define REG_VAR_CONSTANT 0x1234
static string_match_t string_table[] = {
   {"REG_0", REG_VAR_CONSTANT + 0},	// register variable 0
   {"REG_1", REG_VAR_CONSTANT + 1},	// register variable 1
   {"REG_2", REG_VAR_CONSTANT + 2},	// register variable 2
   {"REG_3", REG_VAR_CONSTANT + 3},	// register variable 3
   {"REG_4", REG_VAR_CONSTANT + 4},	// register variable 4
   {"REG_5", REG_VAR_CONSTANT + 5},	// register variable 5
   {"REG_6", REG_VAR_CONSTANT + 6},	// register variable 6
   {"REG_7", REG_VAR_CONSTANT + 7},	// register variable 7
   {"REG_8", REG_VAR_CONSTANT + 8},	// register variable 8
   {"REG_9", REG_VAR_CONSTANT + 9},	// register variable 9
   {"EVENT_RAISE", EVENT_RAISE},	// start of raise
   {"EVENT_UP", EVENT_UP},		// finished raising
   {"EVENT_LOWER", EVENT_LOWER},	// start of lower
   {"EVENT_DOWN", EVENT_DOWN},		// finished lowering
   {"EVENT_MOVE", EVENT_MOVE},		// start of move
   {"EVENT_MOVING", EVENT_MOVING},	// reached target speed
   {"EVENT_POSITION", EVENT_POSITION},	// changed position
   {"EVENT_COAST", EVENT_COAST},	// started coast
   {"EVENT_STOP", EVENT_STOP},		// started stopping
   {"EVENT_STOPPED", EVENT_STOPPED},	// finished stopping
   {"EVENT_HIT", EVENT_HIT},		// hit
   {"EVENT_KILL", EVENT_KILL},		// kill
   {"EVENT_SHUTDOWN", EVENT_SHUTDOWN},	// shutdown
   {"EVENT_SLEEP", EVENT_SLEEP},	// sleep
   {"EVENT_WAKE", EVENT_WAKE},		// wake
   {"EVENT_ERROR", EVENT_ERROR},	// error with one of the above (always causes immediate deactivate)
   {"NL_C_UNSPEC", NL_C_UNSPEC},
   {"NL_C_FAILURE", NL_C_FAILURE},    /* failure message (reply) (generic string) */
   {"NL_C_BATTERY", NL_C_BATTERY},    /* battery status as percentage (request/reply) (generic 8-bit int) */
   {"NL_C_EXPOSE", NL_C_EXPOSE},     /* expose/conceal (command/reply) (generic 8-bit int) */
   {"NL_C_MOVE", NL_C_MOVE},       /* move as mph (command/reply) (generic 16-bit int) */
   {"NL_C_MOVEAWAY", NL_C_MOVEAWAY},       /* move as mph (command/reply) (generic 16-bit int) */
   {"NL_C_POSITION", NL_C_POSITION},   /* position in feet from home (request/reply) (generic 16-bit int) */
   {"NL_C_STOP", NL_C_STOP},       /* stop (command/reply) (generic 8-bit int) */
   {"NL_C_HITS", NL_C_HITS},       /* hit count (request/reply) (generic 8-bit int) */
   {"NL_C_HIT_LOG", NL_C_HIT_LOG},    /* hit count (request/reply) (generic string) */
   {"NL_C_HIT_CAL", NL_C_HIT_CAL},    /* calibrate hit sensor (command/reply) (hit calibrate structure) */
   {"NL_C_BIT", NL_C_BIT},        /* bit button event (broadcast) (bit event structure) */
   {"NL_C_ACCESSORY", NL_C_ACCESSORY},  /* configure accesories (command/reply) (accessory structure) */
   {"NL_C_GPS", NL_C_GPS},        /* gps status (request/reply) (gps structure) */
   {"NL_C_EVENT", NL_C_EVENT},      /* mover/lifter event (command/reply) (generic 8-bit int) */
   {"NL_C_SLEEP", NL_C_SLEEP},      /* sleep/wake command (command) (generic 8-bit int) */
   {"NL_C_DMSG", NL_C_DMSG},       /* debug message (reply) (generic string) */
   {"NL_C_SCENARIO", NL_C_SCENARIO},   /* run scenario message (reply) (generic string) */
   {"NL_C_EVENT_REF", NL_C_EVENT_REF},      /* reflected event (command) (generic 8-bit int) */
   {"NL_C_FAULT", NL_C_FAULT},      /* fault event (reply) (generic 8-bit int) */
   {"R_UNSPECIFIED", R_UNSPECIFIED}, // no role specified
   {"R_LIFTER", R_LIFTER},      // lifting device
   {"R_MOVER", R_MOVER},       // moving device
   {"R_SOUND", R_SOUND},       // sound effects device
   {"R_GUNNER", R_GUNNER},      // lifting device in a TTMT that acts as the gunner
   {"R_DRIVER", R_DRIVER},      // lifting device in a TTMT that acts as the driver
};
// don't support -- {"NL_C_CMD_EVENT", NL_C_CMD_EVENT},  /* command event (command) (command event structure) */
typedef struct command_handler {
   const char* name; // name of function
   scenario_function handler; // function pointer
   scenario_function resume_handler; // resume function pointer
} command_handler_t;
typedef enum {
   WATCHER_NONE,     /* no effect on running scenario */
   WATCHER_RESUME,   /* resume running scenario if sleeping */
   WATCHER_RESUME_2, /* after another event, resume running scenario if sleeping */
   WATCHER_KILL,     /* kill running scenario */
} watcher_cmd_t;
typedef struct wait_watcher {
   const char* name; // name of event to watch
   int nl_cmd;       // netlink command to watch
   int go_event;     // generic output event to watch
   int timeout;      // milliseconds remaining until timeout
   watcher_cmd_t cmd;// action to take at event
   int ro;           // read only? set to 1 if can't be overwritten
} wait_watcher_t;
typedef struct wait_var_watcher {
   int variable;     // register variable to watch
   char *value_buf;  // buffer to match variable to
   int val_len;      // length of value buffer
   int timeout;      // milliseconds remaining until timeout
   watcher_cmd_t cmd;// action to take at event
   int ro;           // read only? set to 1 if can't be overwritten
} wait_var_watcher_t;
static wait_watcher_t wait_watchers[] = {
   {"NL_C_FAILURE",  NL_C_FAILURE,  -1, 0, WATCHER_KILL, 1},
   {"NL_C_BATTERY",  NL_C_BATTERY,  -1, 0, WATCHER_NONE, 0},
   {"NL_C_EXPOSE",   NL_C_EXPOSE,   -1, 0, WATCHER_NONE, 0},
   {"NL_C_MOVE",     NL_C_MOVE,     -1, 0, WATCHER_NONE, 0},
   {"NL_C_MOVEAWAY",     NL_C_MOVEAWAY,     -1, 0, WATCHER_NONE, 0},
   {"NL_C_POSITION", NL_C_POSITION, -1, 0, WATCHER_NONE, 0},
   {"NL_C_STOP",     NL_C_STOP,     -1, 0, WATCHER_NONE, 0},
   {"NL_C_HITS",     NL_C_HITS,     -1, 0, WATCHER_NONE, 0},
   {"NL_C_HIT_LOG",  NL_C_HIT_LOG,  -1, 0, WATCHER_NONE, 0},
   {"NL_C_HIT_CAL",  NL_C_HIT_CAL,  -1, 0, WATCHER_NONE, 0},
   {"NL_C_BIT",      NL_C_BIT,      -1, 0, WATCHER_NONE, 0},
   {"NL_C_ACCESSORY",NL_C_ACCESSORY,-1, 0, WATCHER_NONE, 0},
   {"NL_C_GPS",      NL_C_GPS,      -1, 0, WATCHER_NONE, 0},
   {"NL_C_EVENT",    NL_C_EVENT,    -1, 0, WATCHER_NONE, 0},
   {"NL_C_SLEEP",    NL_C_SLEEP,    -1, 0, WATCHER_NONE, 0},
   {"NL_C_DMSG",     NL_C_DMSG,     -1, 0, WATCHER_NONE, 0},
   {"NL_C_EVENT_REF",NL_C_EVENT_REF,-1, 0, WATCHER_NONE, 0},
   {"NL_C_FAULT",    NL_C_FAULT,    -1, 0, WATCHER_NONE, 0},
   {"EVENT_RAISE",   -1, EVENT_RAISE,   0, WATCHER_NONE, 0},
   {"EVENT_UP",      -1, EVENT_UP,      0, WATCHER_NONE, 0},
   {"EVENT_LOWER",   -1, EVENT_LOWER,   0, WATCHER_NONE, 0},
   {"EVENT_DOWN",    -1, EVENT_DOWN,    0, WATCHER_NONE, 0},
   {"EVENT_MOVE",    -1, EVENT_MOVE,    0, WATCHER_NONE, 0},
   {"EVENT_MOVING",  -1, EVENT_MOVING,  0, WATCHER_NONE, 0},
   {"EVENT_POSITION",-1, EVENT_POSITION,0, WATCHER_NONE, 0},
   {"EVENT_COAST",   -1, EVENT_COAST,   0, WATCHER_NONE, 0},
   {"EVENT_STOP",    -1, EVENT_STOP,    0, WATCHER_NONE, 0},
   {"EVENT_STOPPED", -1, EVENT_STOPPED, 0, WATCHER_NONE, 0},
   {"EVENT_HIT",     -1, EVENT_HIT,     0, WATCHER_NONE, 0},
   {"EVENT_KILL",    -1, EVENT_KILL,    0, WATCHER_NONE, 0},
   {"EVENT_SHUTDOWN",-1, EVENT_SHUTDOWN,0, WATCHER_NONE, 0},
   {"EVENT_SLEEP",   -1, EVENT_SLEEP,   0, WATCHER_NONE, 0},
   {"EVENT_WAKE",    -1, EVENT_WAKE,    0, WATCHER_NONE, 0},
   {"EVENT_ERROR",   -1, EVENT_ERROR,   0, WATCHER_KILL, 1},
};
// don't support -- {"NL_C_CMD_EVENT",NL_C_CMD_EVENT,-1, 0, WATCHER_NONE, 0},
static wait_var_watcher_t wait_var_watchers[] = {
   {0, NULL, 0, 0, WATCHER_NONE, 0},
   {1, NULL, 0, 0, WATCHER_NONE, 0},
   {2, NULL, 0, 0, WATCHER_NONE, 0},
   {3, NULL, 0, 0, WATCHER_NONE, 0},
   {4, NULL, 0, 0, WATCHER_NONE, 0},
   {5, NULL, 0, 0, WATCHER_NONE, 0},
   {6, NULL, 0, 0, WATCHER_NONE, 0},
   {7, NULL, 0, 0, WATCHER_NONE, 0},
   {8, NULL, 0, 0, WATCHER_NONE, 0},
   {9, NULL, 0, 0, WATCHER_NONE, 0},
};
#define MAX_TEMP_BUFFERS 250
static char *temp_buffers[MAX_TEMP_BUFFERS]; // 250 temporary buffers
static int buffer_pos = -1;
static char *register_vars[11] = { // register variables (10 = reservered, 0-9 = avail for use by scenario)
   NULL, NULL, NULL, NULL, NULL, NULL,
   NULL, NULL, NULL, NULL, NULL,
}; // 10 variables for scenario use
typedef enum {
   INITIALIZE,             /* Initialize state machine */
   FIND_CMD_START,         /* Looking for the start character of the next command */
   FIND_CMD_END,           /* Looking for the end character of the next command */
   FIND_PARAM1_START,      /* Looking for the start character of the first parameter */
   FIND_PARAM1_END,        /* Looking for the end character of the first parameter */
   FIND_PARAM2_START,      /* Looking for the start character of the second parameter */
   FIND_PARAM2_START_AFTER_QUOTE, /* Looking for the start character of the second parameter, after a quoted first parameter*/
   FIND_PARAM2_END,        /* Looking for the end character of the second parameter */
   FIND_PARAM3_START,      /* Looking for the start character of the third parameter */
   FIND_PARAM3_START_AFTER_QUOTE, /* Looking for the start character of the third parameter, after a quoted second parameter*/
   FIND_PARAM3_END,        /* Looking for the end character of the third parameter */
   FIND_PARAM4_START,      /* Looking for the start character of the fourth parameter */
   FIND_PARAM4_START_AFTER_QUOTE, /* Looking for the start character of the fourth parameter, after a quoted third parameter*/
   FIND_PARAM4_END,        /* Looking for the end character of the fourth parameter */
   RUN_CMD,                /* Run the found command */
   RESUME_CMD,             /* Resume the found command */
   FIND_CMD_START_ESCAPED, /* Looking for start character, but ignore the next one */
   FIND_CMD_END_ESCAPED,   /* Looking for end character, but ignore the next one */
   FIND_PARAM1_END_ESCAPED,/* Looking for end character, but ignore the next one */
   FIND_PARAM2_END_ESCAPED,/* Looking for end character, but ignore the next one */
   FIND_PARAM3_END_ESCAPED,/* Looking for end character, but ignore the next one */
   FIND_PARAM4_END_ESCAPED,/* Looking for end character, but ignore the next one */
   FIND_PARAM1_END_QUOTED, /* Looking for end quote only */
   FIND_PARAM2_END_QUOTED, /* Looking for end quote only */
   FIND_PARAM3_END_QUOTED, /* Looking for end quote only */
   FIND_PARAM4_END_QUOTED, /* Looking for end quote only */
   FIND_PARAM1_END_QUOTED_ESCAPED, /* Looking for end quote only, but ignore the next one */
   FIND_PARAM2_END_QUOTED_ESCAPED, /* Looking for end quote only, but ignore the next one */
   FIND_PARAM3_END_QUOTED_ESCAPED, /* Looking for end quote only, but ignore the next one */
   FIND_PARAM4_END_QUOTED_ESCAPED, /* Looking for end quote only, but ignore the next one */
} scen_state_t;
typedef struct state_data {
   char *cmd_buffer; int cmd_length;
   char *param1; int param1_len;
   char *param2; int param2_len;
   char *param3; int param3_len;
   char *param4; int param4_len;
} state_data_t;
// main scenario data
scen_state_t g_state = INITIALIZE;
state_data_t g_data;
int g_place = 0;
// branched sub-scenario data
scen_state_t sub_state = INITIALIZE;
state_data_t sub_data;
int sub_place = 0;
static void sleep_done(unsigned long data); // forward declaration
static struct timer_list sleep_timer = TIMER_INITIALIZER(sleep_done, 0, 0);
atomic_t sleep_time = ATOMIC_INIT(0); // sleep forever
atomic_t sleep_acc = ATOMIC_INIT(0); // time slept
struct timespec sleep_start; // time sleep started at

//---------------------------------------------------------------------------
// Scenario functions forward declarations
//---------------------------------------------------------------------------
int Scen_SetVar(char *param1, int param1_len, char *param2, int param2_len,
                 char *param3, int param3_len, char *param4, int param4_len);

int Scen_SetVarLast(char *param1, int param1_len, char *param2, int param2_len,
                    char *param3, int param3_len, char *param4, int param4_len);

int Scen_End(char *param1, int param1_len, char *param2, int param2_len,
              char *param3, int param3_len, char *param4, int param4_len);

int Scen_Send(char *param1, int param1_len, char *param2, int param2_len,
               char *param3, int param3_len, char *param4, int param4_len);

int Scen_SendWait(char *param1, int param1_len, char *param2, int param2_len,
                   char *param3, int param3_len, char *param4, int param4_len);

int Scen_SendWait_Resume(char *param1, int param1_len, char *param2, int param2_len,
                          char *param3, int param3_len, char *param4, int param4_len);

int Scen_Nothing(char *param1, int param1_len, char *param2, int param2_len,
                  char *param3, int param3_len, char *param4, int param4_len);

int Scen_Delay(char *param1, int param1_len, char *param2, int param2_len,
                char *param3, int param3_len, char *param4, int param4_len);

int Scen_If(char *param1, int param1_len, char *param2, int param2_len,
             char *param3, int param3_len, char *param4, int param4_len);

int Scen_DoWait(char *param1, int param1_len, char *param2, int param2_len,
                 char *param3, int param3_len, char *param4, int param4_len);

int Scen_DoWait_Resume(char *param1, int param1_len, char *param2, int param2_len,
                        char *param3, int param3_len, char *param4, int param4_len);


static command_handler_t cmd_handlers[] = {            // prebuilt command table
   {"SetVar", Scen_SetVar, Scen_Nothing},              // no resume function
   {"SetVarLast", Scen_SetVarLast, Scen_Nothing},      // no resume function
   {"End", Scen_End, Scen_Nothing},                    // no resume function
   {"Send", Scen_Send, Scen_Nothing},                  // no resume function
   {"SendWait", Scen_SendWait, Scen_SendWait_Resume},  // has resume function
   {"Nothing", Scen_Nothing, Scen_Nothing},            // no resume function
   {"Delay", Scen_Delay, Scen_Nothing},                // no resume function
   {"If", Scen_If, Scen_If},                           // resume function is same as main function
   {"DoWait", Scen_DoWait, Scen_DoWait_Resume},        // has resume function
};

//---------------------------------------------------------------------------
// Internal functions
//---------------------------------------------------------------------------

// The sleep has finished, resume work
static void sleep_done(unsigned long data) {
   struct timespec time = timespec_sub(current_kernel_time(), sleep_start); // time since start
   atomic_set(&sleep_acc, (time.tv_sec * 1000) + (time.tv_nsec / 1000000));
   delay_printk("Slept for %i milliseconds\n", atomic_read(&sleep_acc));
   atomic_set(&sleep_time,10); // sleep for 10 milliseconds maximum now

   // schedule work to do actual scenario resume
   schedule_work(&scenario_work);
}

// Message filler handler for command event messages
int cmd_event_mfh(struct sk_buff *skb, void *data) {
    // the data argument is a pre-made cmd_event_t
    return nla_put(skb, CMD_EVENT_A_MSG, sizeof(cmd_event_t), data);
}

// find a constant for the given string (returns 1 if found, 0 otherwise)
static int string_to_constant(char *string, int length, int *constant) {
   long tval;
   int i;
   char *p; // Ignore me!
   char buf[256];
   memcpy(buf, string, min(length,255)); // make a copy
   buf[min(length,255)] = '\0'; // null terminate the copy
   // try converting to integer directly first
   tval = simple_strtol(buf, &p, 10); // test copy, base 10
   if (*p == 0) { // did tval get parsed?
      (*constant) = tval & 0xffffffff;
      return 1;
   }
   // couldn't convert...try matching to constant table
   for (i=0; i<sizeof(string_table)/sizeof(string_match_t); i++) {
      if (strncmp(string_table[i].string, string, length) == 0) {
         // found it!
         (*constant) = string_table[i].number;
         return 1;
      }
   }
   // nothing matched...
   return 0;
}

static char *unescape_buffer(char *buffer, int *length) {
#if DEBUG
   char output_1[1024];
   char output_2[1024];
#endif
   char *new_buf;
   int olength = (*length);
   int needed = 0;
   int i; int j;
   if (olength == 0) {
      return buffer;
   }
   new_buf = kmalloc(olength, GFP_KERNEL);

   // loop through buffer, copying and unescaping
   for (i=0, j=0; j<olength; i++, j++) {
      if (buffer[j] != '\\') {
         new_buf[i] = buffer[j]; // copy this one
      } else if (j+1<olength) {
         new_buf[i] = buffer[++j]; // copy next one
         needed = 1; // we made a change, not just a straight copy
         (*length)--; // one less character in new buffer
      } else {
         new_buf[i] = buffer[j]; // copy this one (buffer ended with backslash)
      }
   }

   // check if we actually needed to do the copy or not
   if (needed) {
      // return new buffer
#if DEBUG
   memcpy(output_1, buffer, min(olength, 1023));
   output_1[min(olength,1023)] = '\0';
   memcpy(output_2, new_buf, min((*length), 1023));
   output_2[min((*length),1023)] = '\0';
   delay_printk("before:<<%s>>:%i\tafter:<<%s>>:%i\n", output_1, olength, output_2, (*length));
#endif
      return new_buf;
   } else {
      // free new buffer and return original
      kfree(new_buf);
      return buffer;
   }
}

static void scen_sleep(int milliseconds) { // sleeps for up to X milliseconds (the caling function will need to return 1 for resume later)
   sleep_start = current_kernel_time(); // sleep started now
   atomic_set(&sleep_time,milliseconds);
#ifdef DEBUG
   delay_printk("Attempting to sleep for %i milliseconds\n", milliseconds);
#endif
}

static void scen_wake(void) {
   struct timespec time = timespec_sub(current_kernel_time(), sleep_start); // time since start
   // wake scenario from slumber
   if (g_state == RESUME_CMD) { // check that we're sleeping
      atomic_set(&sleep_acc, (time.tv_sec * 1000) + (time.tv_nsec / 1000000));
      delay_printk("Slept for %i milliseconds\n", atomic_read(&sleep_acc));
	   del_timer(&sleep_timer); // cancel wakeup timer
      schedule_work(&scenario_work); // wake up
   }
}

static int check_running(void) { // returns 1 if we're good to go, 0 otherwise
   if (!atomic_read(&full_init)) {
      return 0; // driver unloading, no good;
   }
   if (!atomic_read(&scenario_running)) {
      return 0; // scenario not running, no good;
   }
   if (atomic_read(&scenario_stopping)) {
      return 0; // scenario is stopping, no good
   }
   return 1; // all good
}

static void do_end_scenario(struct work_struct * work) {
   // end scenario
}

// run a chunk of the scenario buffer
static int run_buffer_chunk(char *cmd, int cmd_length, char *param1, int param1_len, char *param2, int param2_len, char *param3, int param3_len, char *param4, int param4_len, int resume_now) {
#if DEBUG
   char output_buf[1024];
#endif
   int i, resume_later = 0;
   // first check runability
   if (!check_running()) {
      return 0; // if we can't run, don't resume
   }

   // re-map buffers to temp buffers 
   temp_buffers[++buffer_pos] = unescape_buffer(cmd, &cmd_length); // may or may not allocate memory
   temp_buffers[++buffer_pos] = unescape_buffer(param1, &param1_len); // may or may not allocate memory
   temp_buffers[++buffer_pos] = unescape_buffer(param2, &param2_len); // may or may not allocate memory
   temp_buffers[++buffer_pos] = unescape_buffer(param3, &param3_len); // may or may not allocate memory
   temp_buffers[++buffer_pos] = unescape_buffer(param4, &param4_len); // may or may not allocate memory
   
#if DEBUG
   // debug output
delay_printk("Parsed...\n");
   memcpy(output_buf, temp_buffers[buffer_pos-4], min(cmd_length, 1023));
   output_buf[min(cmd_length,1023)] = '\0';
delay_printk("Command %s : %i\n", output_buf, cmd_length);
   memcpy(output_buf, temp_buffers[buffer_pos-3], min(param1_len, 1023));
   output_buf[min(param1_len,1023)] = '\0';
delay_printk("Param 1 %s : %i\n", output_buf, param1_len);
   memcpy(output_buf, temp_buffers[buffer_pos-2], min(param2_len, 1023));
   output_buf[min(param2_len,1023)] = '\0';
delay_printk("Param 2 %s : %i\n", output_buf, param2_len);
   memcpy(output_buf, temp_buffers[buffer_pos-1], min(param3_len, 1023));
   output_buf[min(param3_len,1023)] = '\0';
delay_printk("Param 3 %s : %i\n", output_buf, param3_len);
   memcpy(output_buf, temp_buffers[buffer_pos-0], min(param4_len, 1023));
   output_buf[min(param4_len,1023)] = '\0';
delay_printk("Param 4 %s : %i\n", output_buf, param4_len);
#endif

   // re-check runability
   if (!check_running()) {
      return 0; // if we can't run, don't resume
   }

   // find command to run and run it
   for (i=0; i<sizeof(cmd_handlers)/sizeof(command_handler_t); i++) {
      if (strncmp(cmd_handlers[i].name, temp_buffers[buffer_pos-4], cmd_length) == 0) {
         // found matching handler
         if (resume_now) {
            resume_later = cmd_handlers[i].resume_handler(temp_buffers[buffer_pos-3], param1_len,
               temp_buffers[buffer_pos-2], param2_len,
               temp_buffers[buffer_pos-1], param3_len,
               temp_buffers[buffer_pos-0], param4_len); // use temp buffers
         } else {
            resume_later = cmd_handlers[i].handler(temp_buffers[buffer_pos-3], param1_len,
               temp_buffers[buffer_pos-2], param2_len,
               temp_buffers[buffer_pos-1], param3_len,
               temp_buffers[buffer_pos-0], param4_len); // use temp buffers
         }
         break; // don't check any more
      }
   }

   // re-check runability
   if (!check_running()) {
      return 0; // if we can't run, don't resume
   }

   // clear temp buffers
   if (temp_buffers[buffer_pos--] != param4) { // test to see if we allocated a new buffer
      kfree(temp_buffers[buffer_pos+1]);
   }
   if (temp_buffers[buffer_pos--] != param3) { // test to see if we allocated a new buffer
      kfree(temp_buffers[buffer_pos+1]);
   }
   if (temp_buffers[buffer_pos--] != param2) { // test to see if we allocated a new buffer
      kfree(temp_buffers[buffer_pos+1]);
   }
   if (temp_buffers[buffer_pos--] != param1) { // test to see if we allocated a new buffer
      kfree(temp_buffers[buffer_pos+1]);
   }
   if (temp_buffers[buffer_pos--] != cmd) { // test to see if we allocated a new buffer
      kfree(temp_buffers[buffer_pos+1]);
   }

   return resume_later;
}

// called to free all allocated buffers
static void scenario_cleanup(void) {
   int i = 0;
   // are we in a scenario?
   if (atomic_dec_return(&scenario_running) != 0) { // not running?
      atomic_inc(&scenario_running);
      return;
   }

   // we aren't any more (scenario_running == 0)
   atomic_set(&scenario_stopping, 1);

   // clear main buffer
   kfree(scen_buffer);

   // clear temp buffers
   while (buffer_pos >= 0) {
      kfree(temp_buffers[buffer_pos--]);
   }

   // clear register variables
   for (i=0; i<11; i++) {
      if (register_vars[i] != NULL) {
         kfree(register_vars[i]);
         register_vars[i] = NULL;
      }
   }

   // reset watchers
   for (i=0; i<sizeof(wait_watchers)/sizeof(wait_watcher_t); i++) {
      if (!wait_watchers[i].ro) {
         // if not read-only, overwrite
         wait_watchers[i].cmd = WATCHER_NONE;
      }
   }
   for (i=0; i<sizeof(wait_var_watchers)/sizeof(wait_var_watcher_t); i++) {
      if (!wait_var_watchers[i].ro) {
         // if not read-only, overwrite
         wait_var_watchers[i].cmd = WATCHER_NONE;
         wait_var_watchers[i].value_buf = NULL;
         wait_var_watchers[i].val_len = 0;
      }
   }
}

// run the scenario state machine on a given buffer
static int scenario_state(char *buffer, int *place, scen_state_t *state, state_data_t *d) {
   int resume_later = 0;
   // first check runability
   if (!check_running()) {
      return 0; // don't resume on kill
   }

#if DEBUG
//   delay_printk("%i\t%c\t%i\n", *place, buffer[*place], *state);
#endif

   // run state machine one iteration
   switch (*state) {
      case INITIALIZE : {
         // re-initialize state variables
         d->cmd_buffer = NULL; d->cmd_length = 0;
         d->param1 = NULL; d->param1_len = 0;
         d->param2 = NULL; d->param2_len = 0;
         d->param3 = NULL; d->param3_len = 0;
         d->param4 = NULL; d->param4_len = 0;
         (*state) = FIND_CMD_START; // start by finding a command
      } break;
      case FIND_CMD_START : {
         // find start of command
         switch (buffer[(*place)]) {
            case '{' :
               // next character signifies command start, find command end next
               d->cmd_buffer = buffer + (*place) + 1;
               d->cmd_length = 0;
               (*state) = FIND_CMD_END;
               break;
            case '\\' :
               // next character escaped
               (*state) = FIND_CMD_START_ESCAPED;
               break;
         }
         (*place)++;
      } break;
      case FIND_CMD_END : {
         // find end of command
         switch (buffer[(*place)]) {
            case ';' :
               // found end, now find start of first parameter
               d->cmd_length--; // last character was the end
               (*state) = FIND_PARAM1_START;
               break;
            case '\\' :
               // next character escaped
               (*state) = FIND_CMD_START_ESCAPED;
               break;
         }
         d->cmd_length++;
         (*place)++;
      } break;
      case FIND_PARAM1_START : {
         // find start of first parameter
         switch (buffer[(*place)]) {
            case ';' :
               // no first parameter
               (*state) = FIND_PARAM2_START;
               break;
            case '\\' :
               // found start (escaped), now find end
               d->param1 = buffer + (*place);
               d->param1_len = 0;
               (*state) = FIND_PARAM1_END_ESCAPED;
               break;
            case '"' :
               // next character signifies parameter start, the whole block is quoted
               d->param1 = buffer + (*place) + 1;
               d->param1_len = 0;
               (*state) = FIND_PARAM1_END_QUOTED;
               break;
            default :
               // found start, now find end
               d->param1 = buffer + (*place);
               d->param1_len = 0;
               (*state) = FIND_PARAM1_END;
               break;
         }
         (*place)++;
      } break;
      case FIND_PARAM1_END : {
         // find end of first parameter
         switch (buffer[(*place)]) {
            case ';' :
               // found end of first parameter, find start of second
               (*state) = FIND_PARAM2_START;
               break;
            case '\\' :
               // next character escaped
               (*state) = FIND_PARAM1_END_ESCAPED;
               break;
         }
         d->param1_len++;
         (*place)++;
      } break;
      case FIND_PARAM1_END_QUOTED : {
         // find end of first parameter
         switch (buffer[(*place)]) {
            case '"' :
               // found end of first parameter, find start of second
               d->param1_len--; // last character was the end
               (*state) = FIND_PARAM2_START_AFTER_QUOTE;
               break;
            case '\\' :
               // next character escaped
               (*state) = FIND_PARAM1_END_QUOTED_ESCAPED;
               break;
         }
         d->param1_len++;
         (*place)++;
      } break;
      case FIND_PARAM2_START_AFTER_QUOTE : {
         // find start of second parameter after a quote
         switch (buffer[(*place)]) {
            case ';' :
               // true end of first parameter, start looking normally for second
               (*state) = FIND_PARAM2_START;
               break;
         }
         (*place)++;
      } break;
      case FIND_PARAM2_START : {
         // find start of second parameter
         switch (buffer[(*place)]) {
            case ';' :
               // no second parameter
               (*state) = FIND_PARAM3_START;
               break;
            case '\\' :
               // found start (escaped), now find end
               d->param2 = buffer + (*place);
               d->param2_len = 0;
               (*state) = FIND_PARAM2_END_ESCAPED;
               break;
            case '"' :
               // next character signifies parameter start, the whole block is quoted
               d->param2 = buffer + (*place) + 1;
               d->param2_len = 0;
               (*state) = FIND_PARAM2_END_QUOTED;
               break;
            default :
               // found start, now find end
               d->param2 = buffer + (*place);
               d->param2_len = 0;
               (*state) = FIND_PARAM2_END;
               break;
         }
         (*place)++;
      } break;
      case FIND_PARAM2_END : {
         // find end of second parameter
         switch (buffer[(*place)]) {
            case ';' :
               // found end of second parameter, find start of third
               (*state) = FIND_PARAM3_START;
               break;
            case '\\' :
               // next character escaped
               (*state) = FIND_PARAM2_END_ESCAPED;
               break;
         }
         d->param2_len++;
         (*place)++;
      } break;
      case FIND_PARAM2_END_QUOTED : {
         // find end of second parameter
         switch (buffer[(*place)]) {
            case '"' :
               // found end of second parameter, find start of third
               d->param2_len--; // last character was the end
               (*state) = FIND_PARAM3_START_AFTER_QUOTE;
               break;
            case '\\' :
               // next character escaped
               (*state) = FIND_PARAM2_END_QUOTED_ESCAPED;
               break;
         }
         d->param2_len++;
         (*place)++;
      } break;
      case FIND_PARAM3_START_AFTER_QUOTE : {
         // find start of third parameter after a quote
         switch (buffer[(*place)]) {
            case ';' :
               // true end of second parameter, start looking normally for third
               (*state) = FIND_PARAM3_START;
               break;
         }
         (*place)++;
      } break;
      case FIND_PARAM3_START : {
         // find start of third parameter
         switch (buffer[(*place)]) {
            case ';' :
               // no third parameter
               (*state) = FIND_PARAM4_START;
               break;
            case '\\' :
               // found start (escaped), now find end
               d->param3 = buffer + (*place);
               d->param3_len = 0;
               (*state) = FIND_PARAM3_END_ESCAPED;
               break;
            case '"' :
               // next character signifies parameter start, the whole block is quoted
               d->param3 = buffer + (*place) + 1;
               d->param3_len = 0;
               (*state) = FIND_PARAM3_END_QUOTED;
               break;
            default :
               // found start, now find end
               d->param3 = buffer + (*place);
               d->param3_len = 0;
               (*state) = FIND_PARAM3_END;
               break;
         }
         (*place)++;
      } break;
      case FIND_PARAM3_END : {
         // find end of third parameter
         switch (buffer[(*place)]) {
            case ';' :
               // found end of third parameter, find start of fourth
               (*state) = FIND_PARAM4_START;
               break;
            case '\\' :
               // next character escaped
               (*state) = FIND_PARAM3_END_ESCAPED;
               break;
         }
         d->param3_len++;
         (*place)++;
      } break;
      case FIND_PARAM3_END_QUOTED : {
         // find end of third parameter
         switch (buffer[(*place)]) {
            case '"' :
               // found end of third parameter, find start of fourth
               d->param3_len--; // last character was the end
               (*state) = FIND_PARAM4_START_AFTER_QUOTE;
               break;
            case '\\' :
               // next character escaped
               (*state) = FIND_PARAM3_END_QUOTED_ESCAPED;
               break;
         }
         d->param3_len++;
         (*place)++;
      } break;
      case FIND_PARAM4_START_AFTER_QUOTE : {
         // find start of fourth parameter after a quote
         switch (buffer[(*place)]) {
            case ';' :
               // true end of third parameter, start looking normally for fourth
               (*state) = FIND_PARAM4_START;
               break;
         }
         (*place)++;
      } break;
      case FIND_PARAM4_START : {
         // find start of first parameter
         switch (buffer[(*place)]) {
            case '}' :
               // no fourth parameter, run command next
               (*state) = RUN_CMD;
               break;
            case '\\' :
               // found start (escaped), now find end
               d->param4 = buffer + (*place);
               d->param4_len = 0;
               (*state) = FIND_PARAM4_END_ESCAPED;
               break;
            case '"' :
               // next character signifies parameter start, the whole block is quoted
               d->param4 = buffer + (*place) + 1;
               d->param4_len = 0;
               (*state) = FIND_PARAM4_END_QUOTED;
               break;
            default :
               // found start, now find end
               d->param4 = buffer + (*place);
               d->param4_len = 0;
               (*state) = FIND_PARAM4_END;
               break;
         }
         (*place)++;
      } break;
      case FIND_PARAM4_END : {
         // find end of fourth parameter
         switch (buffer[(*place)]) {
            case '}' :
               // found end of fourth parameter, run command next
               (*state) = RUN_CMD;
               break;
            case '\\' :
               // next character escaped
               (*state) = FIND_PARAM4_END_ESCAPED;
               break;
         }
         d->param4_len++;
         (*place)++;
      } break;
      case FIND_PARAM4_END_QUOTED : {
         // find end of fourth parameter
         switch (buffer[(*place)]) {
            case '"' :
               // found end of fourth parameter, run command next
               d->param4_len--; // last character was the end
               (*state) = RUN_CMD;
               break;
            case '\\' :
               // next character escaped
               (*state) = FIND_PARAM4_END_QUOTED_ESCAPED;
               break;
         }
         d->param4_len++;
         (*place)++;
      } break;
      case FIND_CMD_START_ESCAPED : {
         (*state) = FIND_CMD_START;
         (*place)++;
      } break;
      case FIND_CMD_END_ESCAPED : {
         (*state) = FIND_CMD_END;
         d->cmd_length++;
         (*place)++;
      } break;
      case FIND_PARAM1_END_ESCAPED : {
         (*state) = FIND_PARAM1_END;
         d->param1_len++;
         (*place)++;
      } break;
      case FIND_PARAM2_END_ESCAPED : {
         (*state) = FIND_PARAM2_END;
         d->param2_len++;
         (*place)++;
      } break;
      case FIND_PARAM3_END_ESCAPED : {
         (*state) = FIND_PARAM3_END;
         d->param3_len++;
         (*place)++;
      } break;
      case FIND_PARAM4_END_ESCAPED : {
         (*state) = FIND_PARAM4_END;
         d->param4_len++;
         (*place)++;
      } break;
      case FIND_PARAM1_END_QUOTED_ESCAPED : {
         (*state) = FIND_PARAM1_END_QUOTED;
         d->param1_len++;
         (*place)++;
      } break;
      case FIND_PARAM2_END_QUOTED_ESCAPED : {
         (*state) = FIND_PARAM2_END_QUOTED;
         d->param2_len++;
         (*place)++;
      } break;
      case FIND_PARAM3_END_QUOTED_ESCAPED : {
         (*state) = FIND_PARAM3_END_QUOTED;
         d->param3_len++;
         (*place)++;
      } break;
      case FIND_PARAM4_END_QUOTED_ESCAPED : {
         (*state) = FIND_PARAM4_END_QUOTED;
         d->param4_len++;
         (*place)++;
      } break;
      case RUN_CMD : {
         // run the found buffer chunks
         resume_later = run_buffer_chunk(d->cmd_buffer, d->cmd_length,
            d->param1, d->param1_len,
            d->param2, d->param2_len,
            d->param3, d->param3_len,
            d->param4, d->param4_len, 0); // don't resume
         if (resume_later) {
            (*state) = RESUME_CMD; // start over on next command
         } else {
            (*state) = INITIALIZE; // start over on next command
         }
      } break;
      case RESUME_CMD : {
         // run the found buffer chunks
         resume_later = run_buffer_chunk(d->cmd_buffer, d->cmd_length,
            d->param1, d->param1_len,
            d->param2, d->param2_len,
            d->param3, d->param3_len,
            d->param4, d->param4_len, 1); // resume
         if (resume_later) {
            (*state) = RESUME_CMD; // start over on next command
         } else {
            (*state) = INITIALIZE; // start over on next command
         }
      } break;
      default : {
#if DEBUG
         delay_printk("MAJOR ERROR!!!\n");
#endif
         scenario_kill();
      } break;
   }
   return resume_later;
}

// run a branched sub-scenario (may resume)
static int run_sub_command_branch(char *buffer, int buf_len) {
   int resume_later = 0;
   // check to see if we're resuming work
   if (sub_state != RESUME_CMD) {
      // prepare for initial state machine run
      sub_state = INITIALIZE;
      sub_place = 0;
      memset(&sub_data, 0, sizeof(state_data_t));
   }

   // run scenario until buffer empty or need to sleep
   while (sub_place <= buf_len && resume_later == 0 && check_running()) {
      // run scenario state machine
      resume_later = scenario_state(buffer, &sub_place, &sub_state, &sub_data);
   }

   return resume_later; 
}

// run a sub-scenario (can't resume)
static void run_sub_command(char *buffer, int buf_len) {
   int resume_later = 0;
   // prepare for initial state machine run
   scen_state_t state = INITIALIZE;
   int place = 0;
   state_data_t data;
   memset(&data, 0, sizeof(state_data_t));

   // run scenario until buffer empty or need to sleep
   while (place <= buf_len && resume_later == 0 && check_running()) {
      // run scenario state machine
      resume_later = scenario_state(buffer, &place, &state, &data);
   }

   // can't resume, and don't do cleanup here, so just return
}

// run the scenario in userspace
static void do_scenario(struct work_struct * work) {
   int resume_later = 0, sleep_t;
   // check to see if we're shutting down
   if (!check_running()) {
      scenario_cleanup();
      return;
   }
   // check to see if we're resuming work
   if (g_state != RESUME_CMD) {
      // prepare for initial state machine run
      g_state = INITIALIZE;
      g_place = 0;
      memset(&g_data, 0, sizeof(state_data_t));
   }

   // run scenario until buffer empty or need to sleep
   while (g_place <= scen_length && resume_later == 0 && check_running()) {
      // run scenario state machine
      resume_later = scenario_state(scen_buffer, &g_place, &g_state, &g_data);
   }

   // are we coming back?
   if (resume_later) {
      // set timer appropriately
      sleep_t = atomic_read(&sleep_time);
      if (sleep_t > 0) {
         delay_printk("Sleeping for %i milliseconds\n", sleep_t);
         mod_timer(&sleep_timer, jiffies+((sleep_t*HZ)/1000)); // blank for X milliseconds
      }
   } else {
      // clean up scenario
      scenario_cleanup();
   }
delay_printk("Scenario done for now...\n");
}

//---------------------------------------------------------------------------
// External functions
//---------------------------------------------------------------------------
// run a scenario (only one globally at a time)
int scenario_run(char *scen) {
   // can we run a scenario now?
   if (!atomic_read(&full_init)) { // not fully initiliazed?
#if DEBUG
      delay_printk("Error! not fully initialized!\n");
#endif
      return 0;
   } else if (scen == NULL) {
#if DEBUG
      delay_printk("Error! scenario is null!\n");
#endif
      return 0;
   } else if (atomic_inc_return(&scenario_running) != 1) { // scenario was already running?
      atomic_dec(&scenario_running); // revert atomic position
#if DEBUG
      delay_printk("Error! already running!\n");
#endif
      return 0;
   }
#if DEBUG
   delay_printk("Starting Scenario\n");
#endif

   // we're running now (scenario_running == 1)
   atomic_set(&scenario_stopping, 0); // don't stop as soon as we start

   // copy "scen" to internal buffer
   scen_length = strlen(scen);
   scen_buffer = kmalloc(scen_length, GFP_KERNEL); // allocate new block
   scen_buffer = memcpy(scen_buffer, scen, scen_length); // copy original data
#if DEBUG
   delay_printk("Scenario Length: %i\n", scen_length);
   //delay_printk("Scenario Data: %s\n", scen_buffer);
#endif

   // schedule work
   schedule_work(&scenario_work);

   // return success to previous function
   return 1;
}
EXPORT_SYMBOL(scenario_run);

// kills running scenario, if any
void scenario_kill(void) {
   // only kill if we're running
   if (atomic_read(&scenario_running) == 1) {
      // stop the scenario as quickly as possible
      atomic_set(&scenario_stopping, 1);

      // if we're not shutting down, and we're sleeping, wake up to stop and clean
      if (g_state == RESUME_CMD && atomic_read(&full_init)) {
         // fix state to not resume
         g_state = INITIALIZE;
         // wake up
         schedule_work(&scenario_work);
      }
      
      // cancel timers if timers are running
      del_timer(&sleep_timer);
   }
}
EXPORT_SYMBOL(scenario_kill);

//---------------------------------------------------------------------------
// Scenario functions definitions
//---------------------------------------------------------------------------
int Scen_SetVar(char *param1, int param1_len, char *param2, int param2_len,
                 char *param3, int param3_len, char *param4, int param4_len) {
   int variable;
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif

   // find variable
   if (!string_to_constant(param1, param1_len, &variable)) {
#if DEBUG
      delay_printk("ERROR: couldn't decode variable parameter\n");
#endif
      scenario_kill();
      return 0;
   }

   // check variable
   if (variable < 0 || variable > 9) {
#if DEBUG
      delay_printk("ERROR: variable parameter (%i) invalid\n", variable);
#endif
      scenario_kill();
      return 0;
   }

   // reset register variable
   if (register_vars[variable] != NULL) {
      kfree(register_vars[variable]);
      register_vars[variable] = NULL;
   }

   // set register variable
   register_vars[variable] = kmalloc(param2_len+1, GFP_KERNEL); // allocate (len + 1)
   memcpy(register_vars[variable], param2, param2_len); // copy
   register_vars[variable][param2_len] = '\0'; // double-check it is null terminated
#if DEBUG
   delay_printk("---> Saved reg %i: %s\n", variable, register_vars[variable]);
#endif
   
   return 0;
}

int Scen_SetVarLast(char *param1, int param1_len, char *param2, int param2_len,
                    char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif

   // check the last (reserved) variable
   if (register_vars[10] == NULL) {
      // it was blank, send a blank string
      return Scen_SetVar(param1, param1_len, "", 0, NULL, 0, NULL, 0);
   } else {
      // send the last (reserved) variable to Scen_SetVar
      return Scen_SetVar(param1, param1_len, register_vars[10], strlen(register_vars[10]), NULL, 0, NULL, 0);
   }
}

int Scen_End(char *param1, int param1_len, char *param2, int param2_len,
              char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif

   // just end the scenario
   scenario_kill();
   return 0;
}

int Scen_Send(char *param1, int param1_len, char *param2, int param2_len,
               char *param3, int param3_len, char *param4, int param4_len) {
   int role, id, attribute, reg_var;
   cmd_event_t nl_cmd;
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif

   // check payload length
   if (param4_len > 32) {
#if DEBUG
      delay_printk("ERROR: couldn't decode payload parameter\n");
#endif
      scenario_kill();
      return 0;
   }

   // find role, id, and attribute
   if (!string_to_constant(param1, param1_len, &role)) {
#if DEBUG
      delay_printk("ERROR: couldn't decode role parameter\n");
#endif
      scenario_kill();
      return 0;
   }
   if (!string_to_constant(param2, param2_len, &id)) {
#if DEBUG
      delay_printk("ERROR: couldn't decode id parameter\n");
#endif
      scenario_kill();
      return 0;
   }
   if (!string_to_constant(param3, param3_len, &attribute)) {
#if DEBUG
      delay_printk("ERROR: couldn't decode attribute parameter\n");
#endif
      scenario_kill();
      return 0;
   }

#if DEBUG
   delay_printk("%s(%i, %i, %i, 0x...(%i))\n",__func__, role, id, attribute, param4_len/2);
#endif

   // convert payload to binary blob
   if (string_to_constant(param4, param4_len, &reg_var)) {
      // was it a register variable?
      reg_var -= REG_VAR_CONSTANT; // convert to 0-9 value
      if (reg_var >= 0 && reg_var <= 9) {
         // check that register variable is valid
         if (register_vars[reg_var] == NULL) {
#if DEBUG
            delay_printk("ERROR: register %i is null\n", reg_var);
#endif
            scenario_kill();
            return 0;
         }
         // it is a register variable, use the correct one
#if DEBUG
   delay_printk("---> Parsed reg %i: %s\n", reg_var, register_vars[reg_var]);
#endif
         hex_decode_attr(register_vars[reg_var], strlen(register_vars[reg_var]), nl_cmd.payload);
         nl_cmd.payload_size = strlen(register_vars[reg_var]);
      } else {
         // it's a hex encoded blob
         hex_decode_attr(param4, param4_len, nl_cmd.payload);
         nl_cmd.payload_size = param4_len/2;
      }
   } else {
      // it's a hex encoded blob
      hex_decode_attr(param4, param4_len, nl_cmd.payload);
      nl_cmd.payload_size = param4_len/2;
   }

   // build netlink message
   nl_cmd.role = role;
   nl_cmd.cmd = id;
   nl_cmd.attribute = attribute;

   // send netlink message
   send_nl_message_multi(&nl_cmd, cmd_event_mfh, NL_C_CMD_EVENT);
   return 0;
}

int Scen_SendWait(char *param1, int param1_len, char *param2, int param2_len,
                   char *param3, int param3_len, char *param4, int param4_len) {
   int role, id, time, reg_var, retval, i;
   cmd_event_t nl_cmd;
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif

   // check payload length
   if (param4_len > 32) {
#if DEBUG
      delay_printk("ERROR: couldn't decode payload parameter\n");
#endif
      scenario_kill();
      return 0;
   }

   // find role, id, and time
   if (!string_to_constant(param1, param1_len, &role)) {
#if DEBUG
      delay_printk("ERROR: couldn't decode role parameter\n");
#endif
      scenario_kill();
      return 0;
   }
   if (!string_to_constant(param2, param2_len, &id)) {
#if DEBUG
      delay_printk("ERROR: couldn't decode id parameter\n");
#endif
      scenario_kill();
      return 0;
   }
   if (!string_to_constant(param4, param4_len, &time)) {
#if DEBUG
      delay_printk("ERROR: couldn't decode time parameter\n");
#endif
      scenario_kill();
      return 0;
   }

#if DEBUG
   delay_printk("%s(%i, %i, 0x...(%i), %i)\n",__func__, role, id, param3_len/2, time);
#endif

   // convert payload to binary blob
   if (string_to_constant(param3, param3_len, &reg_var)) {
      // was it a register variable?
      reg_var -= REG_VAR_CONSTANT; // convert to 0-9 value
      if (reg_var >= 0 && reg_var <= 9) {
         // check that register variable is valid
         if (register_vars[reg_var] == NULL) {
#if DEBUG
            delay_printk("ERROR: register %i is null\n", reg_var);
#endif
            scenario_kill();
            return 0;
         }
         // it is a register variable, use the correct one
#if DEBUG
   delay_printk("---> Parsed reg %i: %s\n", reg_var, register_vars[reg_var]);
#endif
         hex_decode_attr(register_vars[reg_var], strlen(register_vars[reg_var]), nl_cmd.payload);
         nl_cmd.payload_size = strlen(register_vars[reg_var]);
      } else {
         // it's a hex encoded blob
         hex_decode_attr(param3, param3_len, nl_cmd.payload);
         nl_cmd.payload_size = param3_len/2;
      }
   } else {
      // it's a hex encoded blob
      hex_decode_attr(param3, param3_len, nl_cmd.payload);
      nl_cmd.payload_size = param3_len/2;
   }

   // build netlink message
   nl_cmd.role = role;
   nl_cmd.cmd = id;
   nl_cmd.attribute = 1; // assumed to be 1 for SendWait function

   // install watcher based on return of same command as sent command
   retval = 0;
   for (i=0; i<sizeof(wait_watchers)/sizeof(wait_watcher_t); i++) {
      if (strncmp(wait_watchers[i].name, param2, param2_len) == 0) {
         // found it!
         if (!wait_watchers[i].ro) {
            // not read-only, overwrite
#if DEBUG
   delay_printk("INSTALLING RESUME for @ %i\n", i);
#endif
            wait_watchers[i].cmd = WATCHER_RESUME_2; // resume state machine on timeout
         }
         // sleep for the given time
         scen_sleep(time);

         retval = 1; // will resume later
      }
   }

   if (!retval) {
   // could not install watcher...broken script...kill
#if DEBUG
      delay_printk("ERROR: couldn't decode until parameter\n");
#endif
      scenario_kill();
      return 0;
   } else {
      // send netlink message
      send_nl_message_multi(&nl_cmd, cmd_event_mfh, NL_C_CMD_EVENT);
      return retval;
   }
}

int Scen_SendWait_Resume(char *param1, int param1_len, char *param2, int param2_len,
                          char *param3, int param3_len, char *param4, int param4_len) {
   int i;
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif

   // remove watcher
   for (i=0; i<sizeof(wait_watchers)/sizeof(wait_watcher_t); i++) {
      if (strncmp(wait_watchers[i].name, param2, param2_len) == 0) {
         // found it!
         if (!wait_watchers[i].ro) {
            // not read-only, overwrite
#if DEBUG
   delay_printk("REMOVING RESUME for @ %i\n", i);
#endif
            wait_watchers[i].cmd = WATCHER_NONE; // no watcher
         }
         break;
      }
   }

   return 0;
}

int Scen_Nothing(char *param1, int param1_len, char *param2, int param2_len,
                  char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif
   return 0;
}

int Scen_Delay(char *param1, int param1_len, char *param2, int param2_len,
                char *param3, int param3_len, char *param4, int param4_len) {
   int time;
#if DEBUG
   char output_buf[1024];
   delay_printk("%s(...)\n",__func__);
#endif

#if DEBUG
   memcpy(output_buf, param1, min(param1_len, 1023));
   output_buf[min(param1_len,1023)] = '\0';
delay_printk("Trying %s as time parameter\n", output_buf);
#endif
   // find time
   if (!string_to_constant(param1, param1_len, &time)) {
#if DEBUG
      delay_printk("ERROR: couldn't decode time parameter\n");
#endif
      scenario_kill();
      return 0;
   }
   
   // sleep for the given time
   scen_sleep(time);

   return 1; // will resume later
}

int Scen_If(char *param1, int param1_len, char *param2, int param2_len,
             char *param3, int param3_len, char *param4, int param4_len) {
   int variable;
#if DEBUG
   char output_buf[1024];
   delay_printk("%s(...)\n",__func__);
#endif

   // find variable
   if (!string_to_constant(param1, param1_len, &variable)) {
#if DEBUG
      delay_printk("ERROR: couldn't decode variable parameter\n");
#endif
      scenario_kill();
      return 0;
   }

   // check variable
   if (variable < 0 || variable > 9) {
#if DEBUG
      delay_printk("ERROR: variable parameter (%i) invalid\n", variable);
#endif
      scenario_kill();
      return 0;
   }

   // compare to value
#if DEBUG
   memcpy(output_buf, param2, min(param2_len, 1023));
   output_buf[min(param2_len,1023)] = '\0';
   delay_printk("---> Compare reg %i: %s to value: %s\n", variable, register_vars[variable], output_buf);
#endif
   if (memcmp(param2, register_vars[variable], param2_len) == 0) {
      // run true cmd
      return run_sub_command_branch(param3, param3_len);
   } else {
      // run false cmd
      return run_sub_command_branch(param4, param4_len);
   }
}

int Scen_DoWait(char *param1, int param1_len, char *param2, int param2_len,
                 char *param3, int param3_len, char *param4, int param4_len) {
   int time, i;
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif
   // find time
   if (!string_to_constant(param3, param3_len, &time)) {
#if DEBUG
      delay_printk("ERROR: couldn't decode time parameter\n");
#endif
      scenario_kill();
      return 0;
   }

   // run command
   run_sub_command(param1, param1_len);

   // re-check runability
   if (!check_running()) {
      return 0; // if we can't run, don't resume
   }

   // install watcher
   for (i=0; i<sizeof(wait_watchers)/sizeof(wait_watcher_t); i++) {
      if (strncmp(wait_watchers[i].name, param2, param2_len) == 0) {
         // found it!
         if (!wait_watchers[i].ro) {
            // not read-only, overwrite
#if DEBUG
   delay_printk("INSTALLING RESUME for @ %i\n", i);
#endif
            wait_watchers[i].cmd = WATCHER_RESUME; // resume state machine on timeout
         }
         // sleep for the given time
         scen_sleep(time);

         return 1; // will resume later
      }
   }

   // could not install watcher...broken script...kill
#if DEBUG
      delay_printk("ERROR: couldn't decode until parameter\n");
#endif
   scenario_kill();
   return 0;
}

int Scen_DoWait_Resume(char *param1, int param1_len, char *param2, int param2_len,
                        char *param3, int param3_len, char *param4, int param4_len) {
   int time, i, sleep_t;
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif

   // find time
   if (!string_to_constant(param3, param3_len, &time)) {
#if DEBUG
      delay_printk("ERROR: couldn't decode time parameter\n");
#endif
      scenario_kill();
      return 0;
   }

   // remove watcher
   for (i=0; i<sizeof(wait_watchers)/sizeof(wait_watcher_t); i++) {
      if (strncmp(wait_watchers[i].name, param2, param2_len) == 0) {
         // found it!
         if (!wait_watchers[i].ro) {
            // not read-only, overwrite
#if DEBUG
   delay_printk("REMOVING RESUME for @ %i\n", i);
#endif
            wait_watchers[i].cmd = WATCHER_NONE; // no watcher
         }
         break;
      }
   }

   // check time slept vs. timeout time
   sleep_t = atomic_read(&sleep_acc);
   if (abs(sleep_t - time) < 100) { // timed out if less than 100 milliseconds left
      // run timeout command
      run_sub_command(param4, param4_len);
   }

   // re-check runability
   if (!check_running()) {
      return 0; // if we can't run, don't resume
   }

   return 0;
}

//---------------------------------------------------------------------------
// netlink command handler for scenario messages
//---------------------------------------------------------------------------
int nl_scenario_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
   struct nlattr *na;
   int rc;
   char *value;
   delay_printk("%s()\n",__func__);

   // get attribute from message
   na = info->attrs[GEN_STRING_A_MSG]; // generic string message
   if (na) {
      // grab value from attribute
      value = (char*) nla_data(na);

      // run the scenario
      //delay_printk("found data: <<%s>>\n", value);
      scenario_run(value);

      rc = HANDLE_SUCCESS_NO_REPLY;
   } else {
      rc = HANDLE_FAILURE;
   }

   // return to let provider send message back
   return rc;
}

void handle_watchers(int cmd, void *data, int data_len) {
   int i, value;
#if DEBUG
   struct timespec time = timespec_sub(current_kernel_time(), sleep_start); // time since start
   int s = (time.tv_sec * 1000) + (time.tv_nsec / 1000000);
   delay_printk("%s(%i)\n",__func__, cmd);
#endif

   if (atomic_read(&scenario_running) == 1) {
#if DEBUG
      delay_printk("HANDLING CMD %i, with data len %i!\n", cmd, data_len);
      delay_printk("So far slept for %i milliseconds\n", s);
#endif
      // look for appropriate watcher
      for (i=0; i<sizeof(wait_watchers)/sizeof(wait_watcher_t); i++) {
         // look for netlink commands
         if (wait_watchers[i].nl_cmd == cmd) {
            // take action!
            switch(wait_watchers[i].cmd) {
               case WATCHER_NONE: break; /* no action */
               case WATCHER_KILL: 
#if DEBUG
delay_printk("KILLING!!!\n");
#endif
                  scenario_kill();
                  break;
               case WATCHER_RESUME:
#if DEBUG
delay_printk("RESUMING!!!\n");
#endif
                  // reset reserved register variable
                  if (register_vars[10] != NULL) {
                     kfree(register_vars[10]);
                     register_vars[10] = NULL;
                  }
                  if (data) {
                     // set reserved register variable to value from command
                     register_vars[10] = kmalloc(data_len*2+1, GFP_KERNEL); // allocate twice the space + 1
                     hex_encode_attr(data, data_len, register_vars[10]); // hex encode
                     register_vars[10][data_len*2] = '\0'; // double-check it is null terminated
#if DEBUG
   delay_printk("---> Saved reg %i: %s\n", 10, register_vars[10]);
#endif
                  }
                  // wake up scenario
                  scen_wake();
                  break;
               case WATCHER_RESUME_2:
#if DEBUG
delay_printk("SLEEPING!!!\n");
#endif
                  // keep sleeping once more...
                  wait_watchers[i].cmd = WATCHER_RESUME; // ... by resuming later
                  break;
            }
            break; // stop looking for watcher
         }
      }

      if (cmd == NL_C_EVENT || cmd == NL_C_EVENT_REF) {
         // convert data to u8
         value = *(u8*) data;

         // handle "generic output event" events
         if (atomic_read(&scenario_running) == 1) {
#if DEBUG
            delay_printk("HANDLING EVENT %i!\n", value);
#endif

            // look for appropriate watcher
            for (i=0; i<sizeof(wait_watchers)/sizeof(wait_watcher_t); i++) {
               // look for generic output events
               if (wait_watchers[i].go_event == value) {
                  // take action!
                  switch(wait_watchers[i].cmd) {
                     case WATCHER_NONE: break; /* no action */
                     case WATCHER_KILL: 
#if DEBUG
delay_printk("KILLING!!!\n");
#endif
                        scenario_kill();
                        break;
                     case WATCHER_RESUME:
#if DEBUG
delay_printk("RESUMING!!!\n");
#endif
                        // no value for events (reset reserved register variable)
                        if (register_vars[10] != NULL) {
                           kfree(register_vars[10]);
                           register_vars[10] = NULL;
                        }
                        // wake up scenario
                        scen_wake();
                        break;
                     case WATCHER_RESUME_2:
#if DEBUG
delay_printk("SLEEPING!!!\n");
#endif
                        // keep sleeping once more...
                        wait_watchers[i].cmd = WATCHER_RESUME; // ... by resuming later
                        break;
                  }
                  break; // stop looking for watcher
               }
            }
         }
      }

   }
}

//---------------------------------------------------------------------------
// netlink command handler for command events (reflected replies to cmd events)
//---------------------------------------------------------------------------
int nl_cmd_event_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
   struct nlattr *na;
   int rc = HANDLE_SUCCESS_NO_REPLY;
   cmd_event_t *cmd_ev;
#if DEBUG
   delay_printk("%s(%i)\n",__func__, cmd);
#endif

   // get attribute from message
   na = info->attrs[CMD_EVENT_A_MSG]; // accessory message
   if (na) {
      // grab actual command from attribute
      cmd_ev = (cmd_event_t*)nla_data(na);
      if (cmd_ev != NULL) {
         handle_watchers(cmd_ev->cmd, cmd_ev->payload, cmd_ev->payload_size);
      }
   }

   // return to let provider send message back
   return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for scenario events (every netlink message)
//---------------------------------------------------------------------------
int nl_default_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
   struct nlattr *na;
   int rc = HANDLE_SUCCESS_NO_REPLY, s;
#if DEBUG
   delay_printk("%s(%i)\n",__func__, cmd);
#endif

   // handle "netlink command" events
   na = info->attrs[1]; // first attribute only
   s = nl_attr_sizes[cmd].size;
   if (s == -1) {
      s = nla_len(na);
   }
   handle_watchers(cmd, nla_data(na), s);

   // return to let provider send message back
   return rc;
}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init Scenario_init(void) {
   int retval = 0, d_id;
   struct driver_command commands[] = {
        {NL_C_SCENARIO,      nl_scenario_handler},
        {NL_C_FAILURE,       nl_default_handler},
        {NL_C_BATTERY,       nl_default_handler},
        {NL_C_EXPOSE,        nl_default_handler},
        {NL_C_MOVE,          nl_default_handler},
        {NL_C_MOVEAWAY,      nl_default_handler},
        {NL_C_POSITION,      nl_default_handler},
        {NL_C_STOP,          nl_default_handler},
        {NL_C_HITS,          nl_default_handler},
        {NL_C_HIT_LOG,       nl_default_handler},
        {NL_C_HIT_CAL,       nl_default_handler},
        {NL_C_BIT,           nl_default_handler},
        {NL_C_ACCESSORY,     nl_default_handler},
        {NL_C_GPS,           nl_default_handler},
        {NL_C_EVENT,         nl_default_handler},
        {NL_C_SLEEP,         nl_default_handler},
        {NL_C_CMD_EVENT,     nl_cmd_event_handler},
        {NL_C_EVENT_REF,     nl_default_handler},
        {NL_C_FAULT,         nl_default_handler},
   };
   struct nl_driver driver = {NULL, commands, sizeof(commands)/sizeof(struct driver_command), NULL}; // no heartbeat object, X command in list, no identifying data structure

   // install scenario callback functions
   set_scenario_callback(scenario_run);
   set_kill_scenario_callback(scenario_kill);

   // install driver w/ netlink provider
   d_id = install_nl_driver(&driver);
   delay_printk("%s(): %s - %s : %i\n",__func__,  __DATE__, __TIME__, d_id);
   atomic_set(&driver_id, d_id);

   INIT_WORK(&scenario_work, do_scenario);
   INIT_WORK(&end_scenario_work, do_end_scenario);

   // signal that we are fully initialized
   atomic_set(&full_init, TRUE);
   return retval;
}

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit Scenario_exit(void) {
   atomic_set(&full_init, FALSE);
   scenario_kill(); // stop running scenarios
   uninstall_nl_driver(atomic_read(&driver_id));
   ati_flush_work(&scenario_work); // close any open work queue items
   ati_flush_work(&end_scenario_work); // close any open work queue items
   scenario_cleanup(); // clean up scenario allocations
}


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(Scenario_init);
module_exit(Scenario_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI Scenario module");
MODULE_AUTHOR("ndb");

