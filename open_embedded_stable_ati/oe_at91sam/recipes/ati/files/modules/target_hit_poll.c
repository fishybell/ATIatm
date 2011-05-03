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
    #undef INPUT_HIT_SENSOR
    #define	INPUT_HIT_SENSOR 					AT91_PIN_PB31
#endif

//---------------------------------------------------------------------------
// These variables are parameters giving when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
static int usemiles = FALSE;
module_param(usemiles, bool, S_IRUGO);
#define INPUT_HIT (usemiles?INPUT_MILES_HIT:INPUT_HIT_SENSOR)
#define INPUT_HIT_ACTIVE_STATE (usemiles?INPUT_MILES_ACTIVE_STATE:INPUT_HIT_SENSOR_ACTIVE_STATE)
#define INPUT_HIT_PULLUP_STATE (usemiles?INPUT_MILES_PULLUP_STATE:INPUT_HIT_SENSOR_PULLUP_STATE)
#define INPUT_HIT_DEGLITCH_STATE (usemiles?INPUT_MILES_DEGLITCH_STATE:INPUT_HIT_SENSOR_DEGLITCH_STATE)
static int showpoll = FALSE;
module_param(showpoll, bool, S_IRUGO);
static int showint = FALSE;
module_param(showint, bool, S_IRUGO);
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
// This atomic variable keeps track of the hit count
//---------------------------------------------------------------------------
atomic_t hit_count_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that the sensor is enabled.
// No change in settings is permitted when this is true.
//---------------------------------------------------------------------------
atomic_t sensor_enable_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space of a hit change.
//---------------------------------------------------------------------------
static struct work_struct hit_work;

//---------------------------------------------------------------------------
// Timer list for showing debug info
//---------------------------------------------------------------------------
static void debug_out(unsigned long data);
static struct timer_list debug_timer = TIMER_INITIALIZER(debug_out, 0, 0);

//---------------------------------------------------------------------------
// Callback for hit events
//---------------------------------------------------------------------------
static hit_event_callback hit_callback = NULL;
void set_hit_callback(hit_event_callback handler) {
    // only allow setting the callback once
    if (handler != NULL && hit_callback == NULL) {
        hit_callback = handler;
        delay_printk("HIT SENSOR: Registered callback function for hit events\n");
    }
}
EXPORT_SYMBOL(set_hit_callback);

//---------------------------------------------------------------------------
// Poll driven data-read
//---------------------------------------------------------------------------
atomic_t last_val = ATOMIC_INIT(-1); // invalid last value
static void hit_poll(void) {

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    // We poll 10000 times per second, so make this short and sweet

    // on or off?
    if (at91_get_gpio_value(INPUT_HIT) == INPUT_HIT_ACTIVE_STATE) {
        if (showpoll && atomic_read(&last_val) == 0) {
            delay_printk("."); // print transition from off to on
        }
        atomic_set(&last_val, 1);
    } else {
        if (showpoll && atomic_read(&last_val) == 1) {
            delay_printk("'"); // print transition from on to off
        }
        atomic_set(&last_val, 0);
    }
}

//---------------------------------------------------------------------------
// Poll driven data-read (kyle method)
//---------------------------------------------------------------------------
atomic_t establish_val = ATOMIC_INIT(-1); // invalid established value
atomic_t low_cal = ATOMIC_INIT(20); // default low calibration setting
atomic_t high_cal = ATOMIC_INIT(20); // default high calibration setting
atomic_t l_count = ATOMIC_INIT(-1); // invalid counting value
static void hit_kyle(void) {
    int in_val, l_val;

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    // We poll 10000 times per second, so make this short and sweet
    

    in_val = at91_get_gpio_value(INPUT_HIT) == INPUT_HIT_ACTIVE_STATE; // do a test so the value of in_val can be 0 for non-hit and 1 for hit

    // we only need transitions
    if (in_val == atomic_read(&establish_val)) {
        return;
    }

    // reset count value on transition
    if (in_val != atomic_read(&last_val)) {
        if (in_val == 1) {
            atomic_set(&l_count, atomic_read(&high_cal)); // reset to high
        } else {
            atomic_set(&l_count, atomic_read(&low_cal)); // reset to low
        }
    }

    // determine established value
    l_val = atomic_dec_and_test(&l_count); // tests if the new value is 0 or not
    if (l_val) {
    
        atomic_set(&establish_val, in_val); // set as established

        // was it a hit?
        if (in_val == 1) {
            if (hit_callback == NULL) {
                delay_printk("K"); // simple print out if no callback set
            } else {
                hit_callback();
            }
        }
    }

    atomic_set(&last_val, in_val);
}

//---------------------------------------------------------------------------
// Interrupt driven data-read
//---------------------------------------------------------------------------
irqreturn_t hit_int(int irq, void *dev_id, struct pt_regs *regs) {

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return IRQ_HANDLED;
    }
    
    // We get an interrupt on both edges, so we have to check to which edge we should respond.
    if (showint && at91_get_gpio_value(INPUT_HIT) == INPUT_HIT_ACTIVE_STATE) {
        delay_printk("|");
    } else if(showint) {
        delay_printk("-");
    }
    del_timer(&debug_timer); // cancel existing output
    mod_timer(&debug_timer, jiffies+((100*HZ)/1000)); // write the carraige return later

    return IRQ_HANDLED;
}

//---------------------------------------------------------------------------
// Calibration methods
//---------------------------------------------------------------------------
void set_hit_calibration(int lower, int upper) { // set lower and upper hit calibration values
    atomic_set(&low_cal, lower);
    atomic_set(&high_cal, upper);
}
EXPORT_SYMBOL(set_hit_calibration);

void get_hit_calibration(int *lower, int *upper) { // get lower and upper hit calibration values
    *lower = atomic_read(&low_cal);
    *upper = atomic_read(&high_cal);
}
EXPORT_SYMBOL(get_hit_calibration);


//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void) {
    int status = 0;

    // Configure position gpios for input and deglitch for interrupts
    at91_set_gpio_input(INPUT_HIT, INPUT_HIT_PULLUP_STATE);
    at91_set_deglitch(INPUT_HIT, INPUT_HIT_DEGLITCH_STATE);

    status = request_irq(INPUT_HIT, (void*)hit_int, 0, "hit", NULL);
    if (status != 0) {
        if (status == -EINVAL) {
            delay_printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_HIT);
        }
        else if (status == -EBUSY) {
            delay_printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_HIT);
        }
        return status;
    }

    return status;
}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void) {
    disable_irq(INPUT_HIT);
    free_irq(INPUT_HIT, NULL);
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
        hb_obj_init_nt(&hb_obj, hit_kyle, 10000); // heartbeat object calling hit_poll() at 10 khz
    } else {
        hb_obj_init_nt(&hb_obj, hit_poll, 10000); // heartbeat object calling hit_poll() at 10 khz
    }
    d_id = install_nl_driver(&driver); // do hit sensing once
delay_printk("%s(): %s - %s : %i\n",__func__,  __DATE__, __TIME__, d_id);
    atomic_set(&driver_id, d_id);

    atomic_set(&full_init, TRUE);
    return 0;
}

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_hit_poll_exit(void) {
    atomic_set(&full_init, FALSE);
    ati_flush_work(&hit_work); // close any open work queue items
    uninstall_nl_driver(atomic_read(&driver_id));
    hardware_exit();
    del_timer(&debug_timer);
}


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_hit_poll_init);
module_exit(target_hit_poll_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target hit sensor (poll) module");
MODULE_AUTHOR("ndb");

