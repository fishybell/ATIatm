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
typedef void (*scenario_function)(char*,int,char*,int,char*,int,char*,int); // function typedef for scenario prebuilt functions (four parameters given as pointers and lengths)
typedef struct string_match {
   const char * string;
   int number;
} string_match_t;
static string_match_t string_table[] = {
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
   {"NL_C_CMD_EVENT", NL_C_CMD_EVENT},  /* command event (command) (command event structure) */
   {"NL_C_SCENARIO", NL_C_SCENARIO},   /* run scenario message (reply) (generic string) */
   {"R_UNSPECIFIED", R_UNSPECIFIED}, // no role specified
   {"R_LIFTER", R_LIFTER},      // lifting device
   {"R_MOVER", R_MOVER},       // moving device
   {"R_SOUND", R_SOUND},       // sound effects device
   {"R_GUNNER", R_GUNNER},      // lifting device in a TTMT that acts as the gunner
   {"R_DRIVER", R_DRIVER},      // lifting device in a TTMT that acts as the driver
};
typedef struct command_handler {
   const char* name; // name of function
   scenario_function handler; // function pointer
} command_handler_t;
typedef struct wait_watcher {
   const char* name; // name of event to watch
   int nl_cmd;       // netlink command to watch
   int go_event;     // generic output event to watch
   int timeout;      // milliseconds remaining until timeout
   char *timeout_buf;// buffer to run when timeout occurs
   int buf_len;      // length of timeout buffer
   int ro;           // read only? set to 1 if can't be overwritten
} wait_watcher_t;
typedef struct wait_var_watcher {
   int variable;     // register variable to watch
   char *value_buf;  // buffer to match variable to
   int val_len;      // length of value buffer
   int timeout;      // milliseconds remaining until timeout
   char *timeout_buf;// buffer to run when timeout occurs
   int buf_len;      // length of timeout buffer
   int ro;           // read only? set to 1 if can't be overwritten
} wait_var_watcher_t;
typedef struct when_watcher {
   const char* name; // name of event to watch
   int nl_cmd;       // netlink command to watch
   int go_event;     // generic output event to watch
   int variable;     // register variable to set value (if any) into
   char *cmd_buf;    // buffer to run when event occurs
   int buf_len;      // length of command buffer
   int ro;           // read only? set to 1 if can't be overwritten
} when_watcher_t;
typedef struct when_var_watcher {
   int variable;     // register variable to watch
   char *value_buf;  // buffer to match variable to
   int val_len;      // length of value buffer
   char *cmd_buf;    // buffer to run when event occurs
   int buf_len;      // length of command buffer
   int ro;           // read only? set to 1 if can't be overwritten
} when_var_watcher_t;
static wait_watcher_t wait_watchers[] = {
   {"NL_C_FAILURE",  NL_C_FAILURE,  -1, 0, NULL, 0, 0},
   {"NL_C_BATTERY",  NL_C_BATTERY,  -1, 0, NULL, 0, 0},
   {"NL_C_EXPOSE",   NL_C_EXPOSE,   -1, 0, NULL, 0, 0},
   {"NL_C_MOVE",     NL_C_MOVE,     -1, 0, NULL, 0, 0},
   {"NL_C_POSITION", NL_C_POSITION, -1, 0, NULL, 0, 0},
   {"NL_C_STOP",     NL_C_STOP,     -1, 0, NULL, 0, 0},
   {"NL_C_HITS",     NL_C_HITS,     -1, 0, NULL, 0, 0},
   {"NL_C_HIT_LOG",  NL_C_HIT_LOG,  -1, 0, NULL, 0, 0},
   {"NL_C_HIT_CAL",  NL_C_HIT_CAL,  -1, 0, NULL, 0, 0},
   {"NL_C_BIT",      NL_C_BIT,      -1, 0, NULL, 0, 0},
   {"NL_C_ACCESSORY",NL_C_ACCESSORY,-1, 0, NULL, 0, 0},
   {"NL_C_GPS",      NL_C_GPS,      -1, 0, NULL, 0, 0},
   {"NL_C_EVENT",    NL_C_EVENT,    -1, 0, NULL, 0, 0},
   {"NL_C_SLEEP",    NL_C_SLEEP,    -1, 0, NULL, 0, 0},
   {"NL_C_DMSG",     NL_C_DMSG,     -1, 0, NULL, 0, 0},
   {"NL_C_CMD_EVENT",NL_C_CMD_EVENT,-1, 0, NULL, 0, 0},
   {"EVENT_RAISE",   -1, EVENT_RAISE,   0, NULL, 0, 0},
   {"EVENT_UP",      -1, EVENT_UP,      0, NULL, 0, 0},
   {"EVENT_LOWER",   -1, EVENT_LOWER,   0, NULL, 0, 0},
   {"EVENT_DOWN",    -1, EVENT_DOWN,    0, NULL, 0, 0},
   {"EVENT_MOVE",    -1, EVENT_MOVE,    0, NULL, 0, 0},
   {"EVENT_MOVING",  -1, EVENT_MOVING,  0, NULL, 0, 0},
   {"EVENT_POSITION",-1, EVENT_POSITION,0, NULL, 0, 0},
   {"EVENT_COAST",   -1, EVENT_COAST,   0, NULL, 0, 0},
   {"EVENT_STOP",    -1, EVENT_STOP,    0, NULL, 0, 0},
   {"EVENT_STOPPED", -1, EVENT_STOPPED, 0, NULL, 0, 0},
   {"EVENT_HIT",     -1, EVENT_HIT,     0, NULL, 0, 0},
   {"EVENT_KILL",    -1, EVENT_KILL,    0, NULL, 0, 0},
   {"EVENT_SHUTDOWN",-1, EVENT_SHUTDOWN,0, NULL, 0, 0},
   {"EVENT_SLEEP",   -1, EVENT_SLEEP,   0, NULL, 0, 0},
   {"EVENT_WAKE",    -1, EVENT_WAKE,    0, NULL, 0, 0},
   {"EVENT_ERROR",   -1, EVENT_ERROR,   0, NULL, 0, 0},
};
static wait_var_watcher_t wait_var_watchers[] = {
   {0, NULL, 0, 0, NULL, 0, 0},
   {1, NULL, 0, 0, NULL, 0, 0},
   {2, NULL, 0, 0, NULL, 0, 0},
   {3, NULL, 0, 0, NULL, 0, 0},
   {4, NULL, 0, 0, NULL, 0, 0},
   {5, NULL, 0, 0, NULL, 0, 0},
   {6, NULL, 0, 0, NULL, 0, 0},
   {7, NULL, 0, 0, NULL, 0, 0},
   {8, NULL, 0, 0, NULL, 0, 0},
   {9, NULL, 0, 0, NULL, 0, 0},
};
static when_watcher_t when_watchers[] = {
   {"NL_C_FAILURE",  NL_C_FAILURE,  -1, -1, "{END;;;;}", 9, 1},
   {"NL_C_BATTERY",  NL_C_BATTERY,  -1, -1, NULL, 0, 0},
   {"NL_C_EXPOSE",   NL_C_EXPOSE,   -1, -1, NULL, 0, 0},
   {"NL_C_MOVE",     NL_C_MOVE,     -1, -1, NULL, 0, 0},
   {"NL_C_POSITION", NL_C_POSITION, -1, -1, NULL, 0, 0},
   {"NL_C_STOP",     NL_C_STOP,     -1, -1, "{END;;;;}", 9, 1},
   {"NL_C_HITS",     NL_C_HITS,     -1, -1, NULL, 0, 0},
   {"NL_C_HIT_LOG",  NL_C_HIT_LOG,  -1, -1, NULL, 0, 0},
   {"NL_C_HIT_CAL",  NL_C_HIT_CAL,  -1, -1, NULL, 0, 0},
   {"NL_C_BIT",      NL_C_BIT,      -1, -1, NULL, 0, 0},
   {"NL_C_ACCESSORY",NL_C_ACCESSORY,-1, -1, NULL, 0, 0},
   {"NL_C_GPS",      NL_C_GPS,      -1, -1, NULL, 0, 0},
   {"NL_C_EVENT",    NL_C_EVENT,    -1, -1, NULL, 0, 0},
   {"NL_C_SLEEP",    NL_C_SLEEP,    -1, -1, NULL, 0, 0},
   {"NL_C_DMSG",     NL_C_DMSG,     -1, -1, NULL, 0, 0},
   {"NL_C_CMD_EVENT",NL_C_CMD_EVENT,-1, -1, NULL, 0, 0},
   {"EVENT_RAISE",   -1, EVENT_RAISE,   -1, NULL, 0, 0},
   {"EVENT_UP",      -1, EVENT_UP,      -1, NULL, 0, 0},
   {"EVENT_LOWER",   -1, EVENT_LOWER,   -1, NULL, 0, 0},
   {"EVENT_DOWN",    -1, EVENT_DOWN,    -1, NULL, 0, 0},
   {"EVENT_MOVE",    -1, EVENT_MOVE,    -1, NULL, 0, 0},
   {"EVENT_MOVING",  -1, EVENT_MOVING,  -1, NULL, 0, 0},
   {"EVENT_POSITION",-1, EVENT_POSITION,-1, NULL, 0, 0},
   {"EVENT_COAST",   -1, EVENT_COAST,   -1, NULL, 0, 0},
   {"EVENT_STOP",    -1, EVENT_STOP,    -1, NULL, 0, 0},
   {"EVENT_STOPPED", -1, EVENT_STOPPED, -1, NULL, 0, 0},
   {"EVENT_HIT",     -1, EVENT_HIT,     -1, NULL, 0, 0},
   {"EVENT_KILL",    -1, EVENT_KILL,    -1, NULL, 0, 0},
   {"EVENT_SHUTDOWN",-1, EVENT_SHUTDOWN,-1, NULL, 0, 0},
   {"EVENT_SLEEP",   -1, EVENT_SLEEP,   -1, NULL, 0, 0},
   {"EVENT_WAKE",    -1, EVENT_WAKE,    -1, NULL, 0, 0},
   {"EVENT_ERROR",   -1, EVENT_ERROR,   -1, "{END;;;;}", 9, 1},
};
static when_var_watcher_t when_var_watchers[] = {
   {0, NULL, 0, NULL, 0, 0},
   {1, NULL, 0, NULL, 0, 0},
   {2, NULL, 0, NULL, 0, 0},
   {3, NULL, 0, NULL, 0, 0},
   {4, NULL, 0, NULL, 0, 0},
   {5, NULL, 0, NULL, 0, 0},
   {6, NULL, 0, NULL, 0, 0},
   {7, NULL, 0, NULL, 0, 0},
   {8, NULL, 0, NULL, 0, 0},
   {9, NULL, 0, NULL, 0, 0},
};
#define MAX_TEMP_BUFFERS 250
static char *temp_buffers[MAX_TEMP_BUFFERS]; // 250 temporary buffers
static int buffer_pos = -1;
static char *register_vars[10] = { // register variables
   NULL, NULL, NULL, NULL, NULL,
   NULL, NULL, NULL, NULL, NULL,
}; // 10 variables for scenario use
struct task_struct *sleeping_task = NULL; // remember sleeping tasks
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

//---------------------------------------------------------------------------
// Scenario functions forward declarations
//---------------------------------------------------------------------------
void Scen_SetVar(char *param1, int param1_len, char *param2, int param2_len,
                 char *param3, int param3_len, char *param4, int param4_len);

void Scen_End(char *param1, int param1_len, char *param2, int param2_len,
              char *param3, int param3_len, char *param4, int param4_len);

void Scen_Send(char *param1, int param1_len, char *param2, int param2_len,
               char *param3, int param3_len, char *param4, int param4_len);

void Scen_Nothing(char *param1, int param1_len, char *param2, int param2_len,
                  char *param3, int param3_len, char *param4, int param4_len);

void Scen_Delay(char *param1, int param1_len, char *param2, int param2_len,
                char *param3, int param3_len, char *param4, int param4_len);

void Scen_If(char *param1, int param1_len, char *param2, int param2_len,
             char *param3, int param3_len, char *param4, int param4_len);

void Scen_DoWait(char *param1, int param1_len, char *param2, int param2_len,
                 char *param3, int param3_len, char *param4, int param4_len);

void Scen_DoWaitVar(char *param1, int param1_len, char *param2, int param2_len,
                    char *param3, int param3_len, char *param4, int param4_len);

void Scen_DoWhen(char *param1, int param1_len, char *param2, int param2_len,
                 char *param3, int param3_len, char *param4, int param4_len);

void Scen_DoWhenVar(char *param1, int param1_len, char *param2, int param2_len,
                    char *param3, int param3_len, char *param4, int param4_len);

static command_handler_t cmd_handlers[] = { // prebuilt command table
   {"SetVar", Scen_SetVar},
   {"End", Scen_End},
   {"Send", Scen_Send},
   {"Nothing", Scen_Nothing},
   {"Delay", Scen_Delay},
   {"If", Scen_If},
   {"DoWait", Scen_DoWait},
   {"DoWaitVar", Scen_DoWaitVar},
   {"DoWhen", Scen_DoWhen},
   {"DoWhenVar", Scen_DoWhenVar},
};

//---------------------------------------------------------------------------
// Internal functions
//---------------------------------------------------------------------------

// Message filler handler for command event messages
int cmd_event_mfh(struct sk_buff *skb, void *data) {
    // the data argument is a pre-made cmd_event_t
    return nla_put(skb, CMD_EVENT_A_MSG, sizeof(cmd_event_t), data);
}

// find a constant for the given string (returns 1 if found, 0 otherwise)
static int string_to_constant(char *string, int length, int *constant) {
   long tval;
   int i;
   // try converting to integer directly first
   if (strict_strtol(string, 10, &tval) == 0) { // base 10
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

static int scen_sleep(int milliseconds) { // sleeps for up to X milliseconds, returning how many milliseconds remain
   long jremain;
   // remember my task in case I need to sleep or wakeup
   sleeping_task = current; // "current" is a kernel defined macro
   set_current_state(TASK_INTERRUPTIBLE); // allow task to sleep
   jremain = schedule_timeout(jiffies+((milliseconds*HZ)/1000)); // sleep for up-to X milliseconds
   return ((jremain * 1000) / HZ);
}

static void scen_sleep_forever(void) { // sleep indefinitely (will be woken up later presumably)
   // remember my task in case I need to sleep or wakeup
   sleeping_task = current; // "current" is a kernel defined macro
   set_current_state(TASK_INTERRUPTIBLE); // allow task to sleep
   schedule_timeout(MAX_SCHEDULE_TIMEOUT);
}

static void scen_wake(void) {
   // wake task
   if (sleeping_task != NULL) {
      wake_up_process(sleeping_task);
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
static void run_buffer_chunk(char *cmd, int cmd_length, char *param1, int param1_len, char *param2, int param2_len, char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   char output_buf[1024];
#endif
   int i;
   // first check runability
   if (!check_running()) {
      return;
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
      return;
   }

   // find command to run and run it
   for (i=0; i<sizeof(cmd_handlers)/sizeof(command_handler_t); i++) {
      if (strncmp(cmd_handlers[i].name, temp_buffers[buffer_pos-4], cmd_length) == 0) {
         // found matching handler
         cmd_handlers[i].handler(temp_buffers[buffer_pos-3], param1_len,
                                 temp_buffers[buffer_pos-2], param2_len,
                                 temp_buffers[buffer_pos-1], param3_len,
                                 temp_buffers[buffer_pos-0], param4_len); // use temp buffers
         break; // don't check any more
      }
   }

   // re-check runability
   if (!check_running()) {
      return;
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
   for (i=0; i<10; i++) {
      if (register_vars[i] != NULL) {
         kfree(register_vars[i]);
         register_vars[i] = NULL;
      }
   }
}

// run the scenario state machine on a given buffer
typedef struct state_data {
   char *cmd_buffer; int cmd_length;
   char *param1; int param1_len;
   char *param2; int param2_len;
   char *param3; int param3_len;
   char *param4; int param4_len;
} state_data_t;
static void scenario_state(char *buffer, int *place, scen_state_t *state, state_data_t *d) {
   // first check runability
   if (!check_running()) {
      return;
   }

//#if DEBUG
//   delay_printk("%i\t%c\t%i\n", *place, buffer[*place], *state);
//#endif

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
         run_buffer_chunk(d->cmd_buffer, d->cmd_length,
                          d->param1, d->param1_len,
                          d->param2, d->param2_len,
                          d->param3, d->param3_len,
                          d->param4, d->param4_len);
         (*state) = INITIALIZE; // start over
      } break;
   }
}

// run the scenario in userspace
static void do_scenario(struct work_struct * work) {
   // prepare for initial state machine run
   scen_state_t state = INITIALIZE;
   int scen_at = 0;
   state_data_t data;
   memset(&data, 0, sizeof(state_data_t));

   // run scenario
   while (scen_at <= scen_length) {
      // run scenario state machine
      scenario_state(scen_buffer, &scen_at, &state, &data);
   }

   // clean up scenario
   scenario_cleanup();
}

//---------------------------------------------------------------------------
// External functions
//---------------------------------------------------------------------------
// run a scenario (only one globally at a time)
int scenario_run(char *scen) {
   // can we run a scenario now?
   if (!atomic_read(&full_init)) { // not fully initiliazed?
      return 0;
   } else if (atomic_inc_return(&scenario_running) != 1) { // scenario was already running?
      atomic_dec(&scenario_running); // revert atomic position
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
   delay_printk("Scenario Data: %s\n", scen_buffer);
#endif

   // schedule work
   schedule_work(&scenario_work);

   // return success to previous function
   return 1;
}
EXPORT_SYMBOL(scenario_run);

// kills running scenario, if any
void scenario_kill(void) {
   // stop the scenario as quickly as possible
   atomic_set(&scenario_stopping, 1);
}
EXPORT_SYMBOL(scenario_kill);

//---------------------------------------------------------------------------
// Scenario functions definitions
//---------------------------------------------------------------------------
void Scen_SetVar(char *param1, int param1_len, char *param2, int param2_len,
                 char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif
}

void Scen_End(char *param1, int param1_len, char *param2, int param2_len,
              char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif
}

void Scen_Send(char *param1, int param1_len, char *param2, int param2_len,
               char *param3, int param3_len, char *param4, int param4_len) {
   int role, id, attribute;
   cmd_event_t nl_cmd;
#if DEBUG
   char output_buf[1024];
   delay_printk("%s(...)\n",__func__);
#endif

   // check payload length
   if (param4_len > 32) {
      // couldn't decode payload parameter
      scenario_kill();
      return;
   }

   // find role, id, and attribute
   if (!string_to_constant(param1, param1_len, &role)) {
      // couldn't decode role parameter
      scenario_kill();
      return;
   }
   if (!string_to_constant(param2, param2_len, &id)) {
      // couldn't decode id parameter
      scenario_kill();
      return;
   }
   if (!string_to_constant(param1, param1_len, &attribute)) {
      // couldn't decode attribute parameter
      scenario_kill();
      return;
   }

#if DEBUG
   delay_printk("%s(%i, %i, %i, 0x...(%i))\n",__func__, role, id, attribute, param4_len/2);
#endif

   // convert payload to binary blob
   hex_decode_attr(param4, param4_len, nl_cmd.payload);

   // build netlink message
   nl_cmd.role = role;
   nl_cmd.cmd = id;
   nl_cmd.payload_size = param4_len/2;
   nl_cmd.attribute = attribute;

   // send netlink message
   send_nl_message_multi(&nl_cmd, cmd_event_mfh, NL_C_CMD_EVENT);
}

void Scen_Nothing(char *param1, int param1_len, char *param2, int param2_len,
                  char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif
}

void Scen_Delay(char *param1, int param1_len, char *param2, int param2_len,
                char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif
}

void Scen_If(char *param1, int param1_len, char *param2, int param2_len,
             char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif
}

void Scen_DoWait(char *param1, int param1_len, char *param2, int param2_len,
                 char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif
}

void Scen_DoWaitVar(char *param1, int param1_len, char *param2, int param2_len,
                    char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif
}

void Scen_DoWhen(char *param1, int param1_len, char *param2, int param2_len,
                 char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif
}

void Scen_DoWhenVar(char *param1, int param1_len, char *param2, int param2_len,
                    char *param3, int param3_len, char *param4, int param4_len) {
#if DEBUG
   delay_printk("%s(...)\n",__func__);
#endif
}

//---------------------------------------------------------------------------
// netlink command handler for stop commands
//---------------------------------------------------------------------------
#if 0
int nl_stop_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
    delay_printk("Lifter: handling stop command\n");

    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na); // value is ignored
        delay_printk("Lifter: received value: %i\n", value);

        // stop motor wherever it is
        lifter_position_set(LIFTER_POSITION_ERROR_NEITHER);
        enable_battery_check(1); // enable battery checking while motor is off

        // Stop accessories (will disable them as well)
        generic_output_event(EVENT_ERROR);

        // prepare response
        rc = nla_put_u8(skb, GEN_INT8_A_MSG, 1); // value is ignored

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
            delay_printk("Lifter: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        delay_printk("Lifter: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
    delay_printk("Lifter: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}
#endif

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
      delay_printk("found data: <<%s>>\n", value);
      scenario_run(value);

      rc = HANDLE_SUCCESS_NO_REPLY;
   } else {
      rc = HANDLE_FAILURE;
   }

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
   };
   struct nl_driver driver = {NULL, commands, sizeof(commands)/sizeof(struct driver_command), NULL}; // no heartbeat object, X command in list, no identifying data structure

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
   scenario_kill(); // stop running scenarios
   atomic_set(&full_init, FALSE);
   uninstall_nl_driver(atomic_read(&driver_id));
   ati_flush_work(&scenario_work); // close any open work queue items
   ati_flush_work(&end_scenario_work); // close any open work queue items
}


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(Scenario_init);
module_exit(Scenario_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI Scenario module");
MODULE_AUTHOR("ndb");
