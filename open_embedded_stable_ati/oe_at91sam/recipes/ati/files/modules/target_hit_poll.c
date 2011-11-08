//---------------------------------------------------------------------------
// target_hit_poll.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "netlink_kernel.h"
#include "target.h"
#include "target_hit_poll.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"hit sensor"
#define SENSOR_TYPE		"poll"

//#define TESTING_ON_EVAL
#ifdef TESTING_ON_EVAL
    #define FAKE_MS 100
    #undef INPUT_HIT_SENSOR
    #define	INPUT_HIT_SENSOR 					AT91_PIN_PB31
#endif

//---------------------------------------------------------------------------
// These variables are parameters giving when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
static int line = 0; // 0 for regular, 1 for miles
module_param(line, int, S_IRUGO);
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
    int invert;
    spinlock_t lock;
    int gpio;
    int active;
    int pullup;
    int deglitch;
} hit_sensor_t;
static hit_sensor_t sensors[] = {{
    -1, // invalid last value
    -1, // invalid established value
    100, // default seperation of 20 milliseconds
    15, // default sensitivity calibration setting
    -1, // invalid counting value
    0, // start not blanking
    0, // no inverting
    SPIN_LOCK_UNLOCKED,
    INPUT_HIT_SENSOR,
    INPUT_HIT_SENSOR_ACTIVE_STATE,
    INPUT_HIT_SENSOR_PULLUP_STATE,
    INPUT_HIT_SENSOR_DEGLITCH_STATE },
{   -1, // invalid last value
    -1, // invalid established value
    100, // default seperation of 20 milliseconds
    15, // default sensitivity calibration setting
    -1, // invalid counting value
    0, // start not blanking
    0, // no inverting
    SPIN_LOCK_UNLOCKED,
    INPUT_MILES_HIT,
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

#ifdef TESTING_ON_EVAL
//---------------------------------------------------------------------------
// Timer list for fake hit creation
//---------------------------------------------------------------------------
static void fake_run(unsigned long data);
static struct timer_list fake_timer = TIMER_INITIALIZER(fake_run, 0, 0);
#endif

//---------------------------------------------------------------------------
// Callback for hit events
//---------------------------------------------------------------------------
static hit_event_callback hit_callback = NULL;
void set_hit_callback(hit_event_callback handler) {
    spin_lock(&sensors[line].lock);
    // only allow setting the callback once
    if (handler != NULL && hit_callback == NULL) {
        hit_callback = handler;
        delay_printk("HIT SENSOR: Registered callback function for hit events\n");
    }
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(set_hit_callback);

//---------------------------------------------------------------------------
// Poll driven data-read
//---------------------------------------------------------------------------
static void hit_poll(void) {

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    // We have 2 microseconds to complete operations, so die if we can't lock
    if (!spin_trylock(&sensors[line].lock)) {
        return;
    }

    // on or off?
    if (at91_get_gpio_value(sensors[line].gpio) == sensors[line].active) {
        if (showpoll && sensors[line].last_val == 0) {
            delay_printk("."); // print transition from off to on
del_timer(&debug_timer); // cancel existing output
mod_timer(&debug_timer, jiffies+((100*HZ)/1000)); // write the carraige return later
        }
        sensors[line].last_val = 1;
    } else {
        if (showpoll && sensors[line].last_val == 1) {
            delay_printk("'"); // print transition from on to off
del_timer(&debug_timer); // cancel existing output
mod_timer(&debug_timer, jiffies+((100*HZ)/1000)); // write the carraige return later
        }
        sensors[line].last_val = 0;
    }

    spin_unlock(&sensors[line].lock);
}

//---------------------------------------------------------------------------
// Poll driven data-read (kyle method)
//---------------------------------------------------------------------------
static void hit_kyle(void) {
    int in_val;

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    // We have 2 microseconds to complete operations, so die if we can't lock
    if (!spin_trylock(&sensors[line].lock)) {
        return;
    }

    // if we're blanking the input, ignore everything
    if (sensors[line].blanking) {
        spin_unlock(&sensors[line].lock);
        return;
    }

    in_val = at91_get_gpio_value(sensors[line].gpio) == sensors[line].active; // do a test so the value of in_val can be 0 for non-hit and 1 for hit

    // invert if needed
    if (sensors[line].invert) {
        spin_unlock(&sensors[line].lock);
        in_val = !in_val;
    }

    // we only need transitions
    if (in_val == sensors[line].establish_val) {
        spin_unlock(&sensors[line].lock);
        return;
    }

    // reset count value on transition
    if (in_val != sensors[line].last_val) {
        if (in_val == 1) {
            sensors[line].l_count = sensors[line].sens_cal; // reset to sensitivity value
        } else {
            sensors[line].l_count = sensors[line].sep_cal; // reset to seperation value
        }
del_timer(&debug_timer); // cancel existing output
mod_timer(&debug_timer, jiffies+((100*HZ)/1000)); // write the carraige return later
    }

    // determine established value
    if (--sensors[line].l_count == 0) { // tests if the new value is 0 or not
        sensors[line].establish_val = in_val; // set as established

        // was it a hit?
        if (in_val == 1) {
            if (hit_callback == NULL) {
                delay_printk("K"); // simple print out if no callback set
            } else {
                hit_callback(line); // the hit sensor polling will wait for this to finish
            }
        }
    }

    // remember this value as the new last value
    sensors[line].last_val = in_val;

    spin_unlock(&sensors[line].lock);
}

//---------------------------------------------------------------------------
// Calibration methods
//---------------------------------------------------------------------------
void set_hit_calibration(int seperation, int sensitivity) { // set seperation and sensitivity hit calibration values

   delay_printk("Lifter: set_hit_calibration(sep/burst=%d  sens=%d) \n", seperation,sensitivity);
   
    // fix input values
    if (seperation <= 0) { seperation = 1; }
    if (sensitivity <= 0) { sensitivity = 1; }

    // calibrate
    spin_lock(&sensors[line].lock);
    sensors[line].sep_cal = seperation*5; // convert milliseconds to ticks
    sensors[line].sens_cal = sensitivity;
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(set_hit_calibration);

void get_hit_calibration(int *seperation, int *sensitivity) { // get seperation and sensitivity hit calibration values
    spin_lock(&sensors[line].lock);
    *seperation = sensors[line].sep_cal/5; // convert ticks to milliseconds
    *sensitivity = sensors[line].sens_cal;
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(get_hit_calibration);

//---------------------------------------------------------------------------
// Hit sensor blanking methods (turns sensor on or off)
//---------------------------------------------------------------------------
void hit_blanking_on(void) {
    spin_lock(&sensors[line].lock);
    sensors[line].blanking = 1; // turn sensor off (blanking on)
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(hit_blanking_on);

void hit_blanking_off(void) {
    spin_lock(&sensors[line].lock);
    sensors[line].blanking = 0; // turn sensor on (blanking off)
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(hit_blanking_off);

//---------------------------------------------------------------------------
// Turn on/off hit sensor line inverting
//---------------------------------------------------------------------------
void set_hit_invert(int invert) { // resets current poll counters
    // fix input values
    if (invert > 1) { // TODO -- impliment auto inverting
        invert = 1;
    } else {
        invert = 0;
    }

    // invert line (discard current data)
    spin_lock(&sensors[line].lock);
    sensors[line].invert = invert;
    sensors[line].last_val = -1; // invalid last value
    sensors[line].establish_val = -1; // invalid established value
    sensors[line].l_count = -1; // invalid counting value
    spin_unlock(&sensors[line].lock);
}
EXPORT_SYMBOL(set_hit_invert);

int get_hit_invert() {
    int invert;
    spin_lock(&sensors[line].lock);
    invert = sensors[line].invert;
    spin_unlock(&sensors[line].lock);
    return invert;
}
EXPORT_SYMBOL(get_hit_invert);

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
    
    delay_printk("\n");
}

//---------------------------------------------------------------------------
// Timer function to notify the user-space about a hit change
//---------------------------------------------------------------------------
static void debug_out(unsigned long data) {
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    delay_printk("\n");
}

#ifdef TESTING_ON_EVAL
//---------------------------------------------------------------------------
// Timer function to create fake hit
//---------------------------------------------------------------------------
static void fake_run(unsigned long data) {
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    if (hit_callback != NULL) {
        hit_callback(line); // the hit sensor polling will wait for this to finish
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
    if (usekyle) {
        hb_obj_init_nt(&hb_obj, hit_kyle, 5000); // heartbeat object calling hit_poll() at 5 khz
    } else {
        hb_obj_init_nt(&hb_obj, hit_poll, 5000); // heartbeat object calling hit_poll() at 5 khz
    }
    d_id = install_nl_driver(&driver); // do hit sensing once
delay_printk("%s(): %s - %s : %i\n",__func__,  __DATE__, __TIME__, d_id);
    atomic_set(&driver_id, d_id);

    atomic_set(&full_init, TRUE);
#ifdef TESTING_ON_EVAL
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
#ifdef TESTING_ON_EVAL
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

