//---------------------------------------------------------------------------
// target_hit_poll.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "netlink_kernel.h"
#include "target.h"
#include "target_mover_hit_poll.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"hit sensor"
#define SENSOR_TYPE		"poll"
#define FRONT_TIRE 0
#define BACK_TIRE 1
#define ENGINE 2

//#define PRINT_DEBUG
#define SEND_DEBUG

#if defined(SEND_DEBUG)
#define SENDUSERCONNMSG  sendUserConnMsg
static void sendUserConnMsg( char *fmt, ...);
#else
#define SENDUSERCONNMSG(...)  //
#endif

#ifdef PRINT_DEBUG
#define DELAY_PRINTK  delay_printk
#else
#define DELAY_PRINTK(...)  //
#endif

int error_mfh(struct sk_buff *skb, void *msg) {
    // the msg argument is a null-terminated string
    return nla_put_string(skb, GEN_STRING_A_MSG, msg);
}

#ifdef SEND_DEBUG
void sendUserConnMsg( char *fmt, ...){
    va_list ap;
    char *msg;
    va_start(ap, fmt);
     msg = kmalloc(256, GFP_KERNEL);
     if (msg){
         vsnprintf(msg, 256, fmt, ap);
         DELAY_PRINTK(msg);
         send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
         kfree(msg);
     }
   va_end(ap);
}
#endif

// for ignoring hit sensor completely
//#define TESTING_ON_EVAL

// for testing large amount of fake hits
//#define TESTING_ON_EVAL_FAKE
#ifdef TESTING_ON_EVAL_FAKE
    #define FAKE_MS 100
    #undef INPUT_HIT_SENSOR
    #define	INPUT_HIT_SENSOR 					AT91_PIN_PB31
#endif

//---------------------------------------------------------------------------
// These variables are parameters giving when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
static int sens_mult = 2; // Default to 2
module_param(sens_mult, int, S_IRUGO);
static int showpoll = FALSE;
module_param(showpoll, bool, S_IRUGO);
static int usekyle = TRUE;
module_param(usekyle, bool, S_IRUGO);


//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This atomic variable is for our registration with netlink_provider
//---------------------------------------------------------------------------
atomic_t driver_id = ATOMIC_INIT(-1);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space of a hit change.
//---------------------------------------------------------------------------
static struct work_struct hit_work;

//---------------------------------------------------------------------------
// Global variables for each hit sensing line
//---------------------------------------------------------------------------
typedef struct hit_sensor {
    int last_val;
    int establish_val;
    int sep_cal;
    int sens_cal;
    int l_count;
    int blanking;
    int d_count;
    int d1_count;
    int d2_count;
    int disconnected;
    int sensitivity;
    int ucount;
    int ncount;
    int invert;
    spinlock_t lock;
    hit_mover_event_callback hit_callback;
    hit_mover_event_callback disconnected_hit_sensor_callback;
    int gpio;
    int active;
    int pullup;
    int deglitch;
} hit_sensor_t;
static hit_sensor_t sensors[] = {{
    -1, // invalid last value
    -1, // invalid established value
    100, // default seperation of 20 milliseconds
    4, // default sensitivity calibration setting
    -1, // invalid counting value
    0, // start not blanking
    0, // start connected 
    0, // start connected 
    0, // start connected 
    0, // start connected 
    2, // no inverting
    0, // sensitivity
    0, // integrated up count
    0, // integrated down count
    SPIN_LOCK_UNLOCKED,
    NULL, // hit callback
    NULL, // disconnected hit sensor callback
    INPUT_FRONT_TIRE_HIT,
    INPUT_HIT_SENSOR_ACTIVE_STATE,
    INPUT_HIT_SENSOR_PULLUP_STATE,
    INPUT_HIT_SENSOR_DEGLITCH_STATE },
{   -1, // invalid last value
    -1, // invalid established value
    100, // default seperation of 20 milliseconds
    4, // default sensitivity calibration setting
    -1, // invalid counting value
    0, // start not blanking
    0, // start connected 
    0, // start connected 
    0, // start connected 
    0, // start connected 
    2, // no inverting
    0, // sensitivity
    0, // integrated up count
    0, // integrated down count
    SPIN_LOCK_UNLOCKED,
    NULL, // hit callback
    NULL, // disconnected hit sensor callback
    INPUT_BACK_TIRE_HIT,
    INPUT_MILES_ACTIVE_STATE,
    INPUT_MILES_PULLUP_STATE,
    INPUT_MILES_DEGLITCH_STATE},
{   -1, // invalid last value
    -1, // invalid established value
    100, // default seperation of 20 milliseconds
    4, // default sensitivity calibration setting
    -1, // invalid counting value
    0, // start not blanking
    0, // start connected 
    0, // start connected 
    0, // start connected 
    0, // start connected 
    2, // no inverting
    0, // sensitivity
    0, // integrated up count
    0, // integrated down count
    SPIN_LOCK_UNLOCKED,
    NULL, // hit callback
    NULL, // disconnected hit sensor callback
    INPUT_ENGINE_HIT,
    INPUT_MILES_ACTIVE_STATE,
    INPUT_MILES_PULLUP_STATE,
    INPUT_MILES_DEGLITCH_STATE},
};
#define MAX_SENSOR (sizeof(sensors) / sizeof(hit_sensor_t))

//---------------------------------------------------------------------------
// Timer list for showing debug info
//---------------------------------------------------------------------------
static void debug_out(unsigned long data);
static struct timer_list debug_timer = TIMER_INITIALIZER(debug_out, 0, 0);

#ifdef TESTING_ON_EVAL_FAKE
//---------------------------------------------------------------------------
// Timer list for fake hit creation
//---------------------------------------------------------------------------
static void fake_run(unsigned long data);
static struct timer_list fake_timer = TIMER_INITIALIZER(fake_run, 0, 0);
#endif

//---------------------------------------------------------------------------
// Callback for hit events
//---------------------------------------------------------------------------
static hit_mover_event_callback hit_callback = NULL;
static hit_mover_event_callback disconnected_hit_sensor_callback = NULL;

void set_mover_hit_callback(int line, hit_mover_event_callback handler, hit_mover_event_callback discon) {
    spin_lock(&sensors[line].lock);
    // only allow setting the callback once
    if (handler != NULL) {
        sensors[line].hit_callback = handler;
        DELAY_PRINTK("HIT SENSOR: Registered callback function for hit events\n");
    }
    if (discon != NULL) {
        sensors[line].disconnected_hit_sensor_callback = discon;
        DELAY_PRINTK("HIT SENSOR: Registered callback function for disconnect hit sensor events\n");
    }
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(set_mover_hit_callback);

static void hit_detect(int line) {
    int in_val, tmp_val;
    // We have 2 microseconds to complete operations, so die if we can't lock
    if (!spin_trylock(&sensors[line].lock)) {
        return;
    }

    tmp_val = at91_get_gpio_value(sensors[line].gpio);
// don't look for disconnected hit sensor if we are on an eval board
#ifndef TESTING_ON_EVAL
   // Line is high see look for disconnected hit sensor
   if (tmp_val) {
      sensors[line].d_count ++;
      if ( sensors[line].d_count > 10000) {
         sensors[line].d_count = 10000;
         if (!sensors[line].disconnected) {
            sensors[line].disconnected = 1;
            if (disconnected_hit_sensor_callback != NULL) {
               disconnected_hit_sensor_callback(line, sensors[line].disconnected);
            }
         } else {
            sensors[line].d1_count ++;
            if ( sensors[line].d1_count > 10000) {
               sensors[line].d1_count = 0;
               sensors[line].d2_count ++;
               if ( sensors[line].d2_count > 30) {
                  sensors[line].d2_count = 0;
                  if (disconnected_hit_sensor_callback != NULL) {
                     disconnected_hit_sensor_callback(line, sensors[line].disconnected);
                  }
               }
            }
         }
      }
   } else {
      sensors[line].d_count --;
      if ( sensors[line].d_count < 0) {
         sensors[line].d_count = 0;
         if (sensors[line].disconnected) {
            sensors[line].disconnected = 0;
            if (sensors[line].disconnected_hit_sensor_callback != NULL) {
               sensors[line].disconnected_hit_sensor_callback(line, sensors[line].disconnected);
            }
         }
      }
   }
#endif

    // if we're blanking the input or disconnected, ignore everything
    if (sensors[line].blanking || sensors[line].disconnected) {
        spin_unlock(&sensors[line].lock);
        return;
    }

    // NDB - don't check the value twice in the same loop - in_val = at91_get_gpio_value(sensors[line].gpio) == sensors[line].active; // do a test so the value of in_val can be 0 for non-hit and 1 for hit
    in_val = tmp_val == sensors[line].active; // do a test so the value of in_val can be 0 for non-hit and 1 for hit
#ifdef TESTING_ON_EVAL
    in_val = 0; // ignore hit sensor on eval board
#endif

    // invert if needed
    if (sensors[line].invert) {
        in_val = !in_val;
    }

// ncount is the time between hits
    if (in_val == 1) {
    	++sensors[line].ucount;
    	sensors[line].ncount = 0;
        if (!sensors[line].l_count && (sensors[line].ucount == sensors[line].sens_cal)){
    	    ++sensors[line].l_count;
            if (sensors[line].hit_callback == NULL) {
                DELAY_PRINTK("K"); // simple print out if no callback set
            } else {
SENDUSERCONNMSG( "HIT counted %d line %d", sensors[line].ucount, line);
                sensors[line].hit_callback(line, line); // the hit sensor polling will wait for this to finish
            }
        }
    } else {
        if (sensors[line].ncount < sensors[line].sep_cal){
    	    ++sensors[line].ncount;
        } else if (sensors[line].ncount == sensors[line].sep_cal){
    	    ++sensors[line].ncount;
SENDUSERCONNMSG( "HIT magnitude %d line %d", sensors[line].ucount, line);
    	    sensors[line].ucount = 0;
    	    sensors[line].l_count = 0;
        }
    }

    spin_unlock(&sensors[line].lock);
}

static void hit_poll(void) {

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    hit_detect(FRONT_TIRE);
    hit_detect(BACK_TIRE);
    hit_detect(ENGINE);
}

//---------------------------------------------------------------------------
// Calibration methods
//---------------------------------------------------------------------------
void set_mover_hit_calibration(int line, int seperation, int sensitivity) { // set seperation and sensitivity hit calibration values

   DELAY_PRINTK("mover: set_mover_hit_calibration(line=%d sep/burst=%d  sens=%d) \n", line, seperation,sensitivity);
   SENDUSERCONNMSG("mover: set_mover_hit_calibration(line=%d sep/burst=%d  sens=%d) \n", line, seperation,sensitivity);
   
    // fix input values
    if (seperation <= 0) { seperation = 1; }
    if (sensitivity <= 0) { sensitivity = 1; }

    // calibrate
    spin_lock(&sensors[line].lock);
    sensors[line].sep_cal = seperation; // convert milliseconds to ticks
    sensors[line].sensitivity = sensitivity;
/*    sensors[line].sens_cal = cal_table[sensitivity];	*/
    sensors[line].sens_cal = sensitivity * sens_mult;
    sensors[line].ucount = 0;
    sensors[line].l_count = 0;
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(set_mover_hit_calibration);

void get_mover_hit_calibration(int line, int *seperation, int *sensitivity) { // get seperation and sensitivity hit calibration values
    spin_lock(&sensors[line].lock);
    *seperation = sensors[line].sep_cal; // convert ticks to milliseconds
    *sensitivity = sensors[line].sensitivity;
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(get_mover_hit_calibration);

//---------------------------------------------------------------------------
// Hit sensor blanking methods (turns sensor on or off)
//--------------------------------------------------------------------------
void hit_mover_blanking_on(int line) {
    spin_lock(&sensors[line].lock);
    sensors[line].blanking = 1; // turn sensor off (blanking on)
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(hit_mover_blanking_on);

void hit_mover_blanking_off(int line) {
    spin_lock(&sensors[line].lock);
    sensors[line].blanking = 0; // turn sensor on (blanking off)
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(hit_mover_blanking_off);

//---------------------------------------------------------------------------
// Turn on/off hit sensor line inverting
//---------------------------------------------------------------------------
void set_mover_hit_invert(int line, int invert) { // resets current poll counters
    // fix input values
    if (invert > 1) { // TODO -- impliment auto inverting
        invert = 1;
    } else {
        invert = 0;
    }

    // invert line (discard current data)
    spin_lock(&sensors[line].lock);
    if (invert != sensors[line].invert) {
        sensors[line].invert = invert;
        sensors[line].last_val = -1; // invalid last value
        sensors[line].establish_val = -1; // invalid established value
        sensors[line].l_count = -1; // invalid counting value
    }
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(set_mover_hit_invert);

int get_mover_hit_invert(int line) {
    int invert;
    spin_lock(&sensors[line].lock);
    invert = sensors[line].invert;
    spin_unlock(&sensors[line].lock);
    return invert;
}
EXPORT_SYMBOL(get_mover_hit_invert);

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void) {
    int status = 0, i;

    // Configure position gpios for input and deglitch
    for (i = 0; i < MAX_SENSOR; i++) {
        at91_set_gpio_input(sensors[i].gpio, sensors[i].pullup);
        at91_set_deglitch(sensors[i].gpio, sensors[i].deglitch);
    }

    return status;
}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void) {
    return 0;
}

//---------------------------------------------------------------------------
// Work item to notify the user-space about a hit change
//---------------------------------------------------------------------------
static void hit_change(struct work_struct * work) {
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    DELAY_PRINTK("\n");
}

//---------------------------------------------------------------------------
// Timer function to notify the user-space about a hit change
//---------------------------------------------------------------------------
static void debug_out(unsigned long data) {
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    DELAY_PRINTK("\n");
}

#ifdef TESTING_ON_EVAL_FAKE
//---------------------------------------------------------------------------
// Timer function to create fake hit
//---------------------------------------------------------------------------
static void fake_run(unsigned long data) {
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    for (int line = 0; line < MAX_SENSOR; line ++) {
      if (sensors[line].hit_callback != NULL) {
         sensors[line].hit_callback(line);
      }
    }
    mod_timer(&fake_timer, jiffies+((FAKE_MS*HZ)/1000)); // create a fake hit every 100 ms
}
#endif

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_hit_poll_init(void) {
    struct heartbeat_object hb_obj;
    int d_id;
    struct nl_driver driver = {&hb_obj, NULL, 0, NULL}; // only heartbeat object

    INIT_WORK(&hit_work, hit_change);

    hardware_init();

    // setup heartbeat for polling
    hb_obj_init_nt(&hb_obj, hit_poll, 1000); // heartbeat object calling hit_poll() at 1 khz
    d_id = install_nl_driver(&driver); // do hit sensing once
DELAY_PRINTK("%s(): %s - %s : %i\n",__func__,  __DATE__, __TIME__, d_id);
    atomic_set(&driver_id, d_id);

    atomic_set(&full_init, TRUE);
#ifdef TESTING_ON_EVAL_FAKE
    mod_timer(&fake_timer, jiffies+((FAKE_MS*HZ)/1000)); // create a fake hit every 100 ms
#endif
    return 0;
}

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_hit_poll_exit(void) {
    atomic_set(&full_init, FALSE);
    del_timer(&debug_timer);
#ifdef TESTING_ON_EVAL_FAKE
    del_timer(&fake_timer);
#endif
    ati_flush_work(&hit_work); // close any open work queue items
    uninstall_nl_driver(atomic_read(&driver_id));
    hardware_exit();
}


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_hit_poll_init);
module_exit(target_hit_poll_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target hit sensor (poll) module");
MODULE_AUTHOR("ndb");

