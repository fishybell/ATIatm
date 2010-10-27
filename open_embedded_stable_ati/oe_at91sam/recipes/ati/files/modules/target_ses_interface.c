//---------------------------------------------------------------------------
// target_ses_interface.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_ses_interface.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"ses interface"

#define BLINK_ON_IN_MSECONDS            1000
#define BLINK_OFF_IN_MSECONDS           500

#define KNOB_TIME_IN_MSECONDS		10
#define MODE_TIME_IN_MSECONDS		50

#define KNOB_LIVEFIRE		15

#define MODE_MAINTENANCE	0
#define MODE_TESTING		1
#define MODE_RECORD		2
#define MODE_LIVEFIRE		3
#define MODE_ERROR		4

//#define TESTING_ON_EVAL
#ifdef TESTING_ON_EVAL

#undef INPUT_SES_MODE_BUTTON_ACTIVE_STATE
#undef INPUT_SES_MODE_BUTTON_PULLUP_STATE
#undef INPUT_SES_MODE_BUTTON_DEGLITCH_STATE
#undef INPUT_SES_MODE_BUTTON

#undef OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE
#undef OUTPUT_SES_MODE_MAINT_INDICATOR
#undef OUTPUT_SES_MODE_TESTING_INDICATOR
#undef OUTPUT_SES_MODE_RECORD_INDICATOR
#undef OUTPUT_SES_MODE_LIVEFIRE_INDICATOR

#undef INPUT_SELECTOR_KNOB_ACTIVE_STATE
#undef INPUT_SELECTOR_KNOB_PULLUP_STATE
#undef INPUT_SELECTOR_KNOB_DEGLITCH_STATE
#undef INPUT_SELECTOR_KNOB_PIN_1
#undef INPUT_SELECTOR_KNOB_PIN_2
#undef INPUT_SELECTOR_KNOB_PIN_4
#undef INPUT_SELECTOR_KNOB_PIN_8

#define INPUT_SES_MODE_BUTTON_ACTIVE_STATE                      ACTIVE_HIGH
#define INPUT_SES_MODE_BUTTON_PULLUP_STATE                      PULLUP_ON
#define INPUT_SES_MODE_BUTTON_DEGLITCH_STATE                    DEGLITCH_ON
#define INPUT_SES_MODE_BUTTON                                   AT91_PIN_PA30

#define OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE                  ACTIVE_HIGH
#define OUTPUT_SES_MODE_MAINT_INDICATOR                         AT91_PIN_PB9
#define OUTPUT_SES_MODE_TESTING_INDICATOR                       AT91_PIN_PB9
#define OUTPUT_SES_MODE_RECORD_INDICATOR                        AT91_PIN_PB9
#define OUTPUT_SES_MODE_LIVEFIRE_INDICATOR                      AT91_PIN_PB9

#define INPUT_SELECTOR_KNOB_ACTIVE_STATE                        ACTIVE_LOW
#define INPUT_SELECTOR_KNOB_PULLUP_STATE                        PULLUP_ON
#define INPUT_SELECTOR_KNOB_DEGLITCH_STATE                      DEGLITCH_ON
#define INPUT_SELECTOR_KNOB_PIN_1                               AT91_PIN_PB31
#define INPUT_SELECTOR_KNOB_PIN_2                               AT91_PIN_PB29
#define INPUT_SELECTOR_KNOB_PIN_4                               AT91_PIN_PB27
#define INPUT_SELECTOR_KNOB_PIN_8                               AT91_PIN_PB25


#endif // TESTING_ON_EVAL

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the blink timeout fires.
//---------------------------------------------------------------------------
static void blink_timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the blink timeout.
//---------------------------------------------------------------------------
static struct timer_list blink_timeout_timer_list = TIMER_INITIALIZER(blink_timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate what the mode is currently at.
//---------------------------------------------------------------------------
atomic_t mode_value_atomic = ATOMIC_INIT(MODE_MAINTENANCE);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the mode timeout fires.
//---------------------------------------------------------------------------
static void mode_timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the mode timeout.
//---------------------------------------------------------------------------
static struct timer_list mode_timeout_timer_list = TIMER_INITIALIZER(mode_timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space that the mode
// has changed
//---------------------------------------------------------------------------
static struct work_struct mode_work;

//---------------------------------------------------------------------------
// This atomic variable is use to indicate where the knob is currently at.
//---------------------------------------------------------------------------
static const char * mode_status[] =
    {
    "maintenance",
    "testing",
    "record",
    "livefire",
    "error"
    };

//---------------------------------------------------------------------------
// This atomic variable is use to indicate where the knob is currently at.
//---------------------------------------------------------------------------
atomic_t knob_value_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the knob timeout fires.
//---------------------------------------------------------------------------
static void knob_timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the knob timeout.
//---------------------------------------------------------------------------
static struct timer_list knob_timeout_timer_list = TIMER_INITIALIZER(knob_timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space that the
// selector knob has rotated
//---------------------------------------------------------------------------
static struct work_struct knob_work;

//---------------------------------------------------------------------------
// Selector Knob mapping
// actual = knob_map[PIN_1 + PIN_2 * 2 + PIN_4 * 4 + PIN_8 * 8];
// based on Greyhill part # 26GS22-01-1-16S-C
//---------------------------------------------------------------------------
static const int knob_map[16] = {0,1,3,2,7,6,4,5,15,14,12,13,8,9,11,10};

//---------------------------------------------------------------------------
// reads the value of all 4 knob pins at once, returning the computed value
//---------------------------------------------------------------------------
static void do_mode(void)
    {
    int mode = atomic_read(&mode_value_atomic);
    printk(KERN_ALERT "%s - %s() : %i\n",TARGET_NAME, __func__, mode);

    // turn off all indicator
    del_timer(&blink_timeout_timer_list);
    at91_set_gpio_value(OUTPUT_SES_MODE_MAINT_INDICATOR, !OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);
    at91_set_gpio_value(OUTPUT_SES_MODE_TESTING_INDICATOR, !OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);
    at91_set_gpio_value(OUTPUT_SES_MODE_RECORD_INDICATOR, !OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);
    at91_set_gpio_value(OUTPUT_SES_MODE_LIVEFIRE_INDICATOR, !OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);

    // turn on correct indicator
    switch (mode)
        {
        case MODE_MAINTENANCE:
            at91_set_gpio_value(OUTPUT_SES_MODE_MAINT_INDICATOR, OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);
            break;
        case MODE_TESTING:
            at91_set_gpio_value(OUTPUT_SES_MODE_TESTING_INDICATOR, OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);
            break;
        case MODE_RECORD:
            at91_set_gpio_value(OUTPUT_SES_MODE_RECORD_INDICATOR, OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);
            break;
        case MODE_LIVEFIRE:
            // live fire blinks
            mod_timer(&blink_timeout_timer_list, jiffies+(100*HZ/1000));
            break;
        }
    }

//---------------------------------------------------------------------------
// reads the value of all 4 knob pins at once, returning the computed value
//---------------------------------------------------------------------------
static DEFINE_SPINLOCK(knob_lock);
static int knob_read(void)
    {
    int pin1, pin2, pin4, pin8;
    unsigned long flags;
    spin_lock_irqsave(&knob_lock, flags);

    pin1 = (at91_get_gpio_value(INPUT_SELECTOR_KNOB_PIN_1) == INPUT_SELECTOR_KNOB_ACTIVE_STATE);
    pin2 = (at91_get_gpio_value(INPUT_SELECTOR_KNOB_PIN_2) == INPUT_SELECTOR_KNOB_ACTIVE_STATE);
    pin4 = (at91_get_gpio_value(INPUT_SELECTOR_KNOB_PIN_4) == INPUT_SELECTOR_KNOB_ACTIVE_STATE);
    pin8 = (at91_get_gpio_value(INPUT_SELECTOR_KNOB_PIN_8) == INPUT_SELECTOR_KNOB_ACTIVE_STATE);

    spin_unlock_irqrestore(&knob_lock, flags);

    printk(KERN_ALERT "%s - pins : %i %i %i %i\n",TARGET_NAME, pin1, pin2, pin4, pin8);

    return (pin1 * 1) + (pin2 * 2) + (pin4 * 4) + (pin8 * 8);
    }

//---------------------------------------------------------------------------
// The function that gets called when the blink timeout fires.
//---------------------------------------------------------------------------
static void blink_timeout_fire(unsigned long data)
    {
    printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);
    if (at91_get_gpio_value(OUTPUT_SES_MODE_LIVEFIRE_INDICATOR) == OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE)
        {
        at91_set_gpio_value(OUTPUT_SES_MODE_LIVEFIRE_INDICATOR, !OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE); // Turn LED off
        mod_timer(&blink_timeout_timer_list, jiffies+(BLINK_OFF_IN_MSECONDS*HZ/1000));
        }
    else
        {
        at91_set_gpio_value(OUTPUT_SES_MODE_LIVEFIRE_INDICATOR, OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE); // Turn LED on
        mod_timer(&blink_timeout_timer_list, jiffies+(BLINK_ON_IN_MSECONDS*HZ/1000));
        }
    }


//---------------------------------------------------------------------------
// interrupt for the mode changing. Actually reads data later when pins settle
//---------------------------------------------------------------------------
irqreturn_t mode_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

    printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

    // read the mode in 50 milliseconds
    del_timer(&mode_timeout_timer_list);
    mod_timer(&mode_timeout_timer_list, jiffies+(MODE_TIME_IN_MSECONDS*HZ/1000));

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// The function that gets called when the mode timeout fires.
//---------------------------------------------------------------------------
static void mode_timeout_fire(unsigned long data)
    {
    // read value from gpio
    int value;

    if (at91_get_gpio_value(INPUT_SES_MODE_BUTTON) != INPUT_SES_MODE_BUTTON_ACTIVE_STATE)
        {
    printk(KERN_ALERT "%s - %s - skipping\n",TARGET_NAME,__func__);
        return;
        }

    printk(KERN_ALERT "%s - %s\n",TARGET_NAME,__func__);

    // change value, if we're allowed
    if (atomic_read(&knob_value_atomic) == KNOB_LIVEFIRE)
        {
        atomic_set(&mode_value_atomic, MODE_LIVEFIRE);
        }
    else
        {
        // rotate value
        value = atomic_read(&mode_value_atomic);
        value++;
        if (value == MODE_ERROR)
            {
            value = MODE_MAINTENANCE;
            }
        atomic_set(&mode_value_atomic, value);
        }

    // notify user-space
    schedule_work(&mode_work);

    // change indicators
    do_mode();
    }

//---------------------------------------------------------------------------
// interrupt for the knob turning. Actually reads data later when pins settle
//---------------------------------------------------------------------------
irqreturn_t knob_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

    printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

    // read the knob in 10 milliseconds
    del_timer(&knob_timeout_timer_list);
    mod_timer(&knob_timeout_timer_list, jiffies+(KNOB_TIME_IN_MSECONDS*HZ/1000));

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// The function that gets called when the knob timeout fires.
//---------------------------------------------------------------------------
static void knob_timeout_fire(unsigned long data)
    {
    // read value from gpio
    int value, ovalue;
    ovalue = atomic_read(&knob_value_atomic);
    value = knob_read();

    printk(KERN_ALERT "%s - %s : value %i\n",TARGET_NAME,__func__,value);

    // change value
    if (value == KNOB_LIVEFIRE)
        {
        // changed to livefire on the knob, change the mode
        atomic_set(&mode_value_atomic, MODE_LIVEFIRE);

        // notify user-space
        schedule_work(&mode_work);
        }
    else if (ovalue == KNOB_LIVEFIRE)
        {
        // changed away from livefire on the knob, change the mode
        atomic_set(&mode_value_atomic, MODE_MAINTENANCE);

        // notify user-space
        schedule_work(&mode_work);
        }
    atomic_set(&knob_value_atomic, value);

    // notify user-space
    schedule_work(&knob_work);

    // change indicators
    do_mode();
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0, value;

    // Configure selector mode gpio for input and deglitch for interrupt
    at91_set_gpio_input(INPUT_SES_MODE_BUTTON, INPUT_SES_MODE_BUTTON_PULLUP_STATE);
    at91_set_deglitch(INPUT_SES_MODE_BUTTON, INPUT_SES_MODE_BUTTON_DEGLITCH_STATE);

    // Configure selector knob gpios for input and deglitch for interrupt
    at91_set_gpio_input(INPUT_SELECTOR_KNOB_PIN_1, INPUT_SELECTOR_KNOB_PULLUP_STATE);
    at91_set_gpio_input(INPUT_SELECTOR_KNOB_PIN_2, INPUT_SELECTOR_KNOB_PULLUP_STATE);
    at91_set_gpio_input(INPUT_SELECTOR_KNOB_PIN_4, INPUT_SELECTOR_KNOB_PULLUP_STATE);
    at91_set_gpio_input(INPUT_SELECTOR_KNOB_PIN_8, INPUT_SELECTOR_KNOB_PULLUP_STATE);
    at91_set_deglitch(INPUT_SELECTOR_KNOB_PIN_1, INPUT_SELECTOR_KNOB_DEGLITCH_STATE);

    // configure bit status gpio for output and set initial output
    at91_set_gpio_output(OUTPUT_SES_MODE_MAINT_INDICATOR, !OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);
    at91_set_gpio_output(OUTPUT_SES_MODE_TESTING_INDICATOR, !OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);
    at91_set_gpio_output(OUTPUT_SES_MODE_RECORD_INDICATOR, !OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);
    at91_set_gpio_output(OUTPUT_SES_MODE_LIVEFIRE_INDICATOR, !OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);

    // turn amp up to 11
    at91_set_gpio_output(OUTPUT_SES_MODE_AMPLIFIER_ON, OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE);

    // set initial value of knob/mode
    value = knob_read();
    atomic_set(&knob_value_atomic, value);
    if (value == KNOB_LIVEFIRE)
        {
        atomic_set(&mode_value_atomic, MODE_LIVEFIRE);
        }
    do_mode();

    // setup interrupts
    status = request_irq(INPUT_SES_MODE_BUTTON, (void*)mode_int, 0, "ses_interface_mode", NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
        	printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_SES_MODE_BUTTON);
            }
        else if (status == -EBUSY)
            {
        	printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_SES_MODE_BUTTON);
            }

        return status;
        }

    status = request_irq(INPUT_SELECTOR_KNOB_PIN_1, (void*)knob_int, 0, "ses_interface_knob", NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
        	printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_SELECTOR_KNOB_PIN_1);
            }
        else if (status == -EBUSY)
            {
        	printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_SELECTOR_KNOB_PIN_1);
            }

        return status;
        }


    return status;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
    del_timer(&knob_timeout_timer_list);
    del_timer(&mode_timeout_timer_list);
    del_timer(&blink_timeout_timer_list);
    free_irq(INPUT_SES_MODE_BUTTON, NULL);
    free_irq(INPUT_SELECTOR_KNOB_PIN_1, NULL);
    return 0;
    }


//---------------------------------------------------------------------------
// Handles reads to the mode attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	int status;
	status = atomic_read(&mode_value_atomic);
    return sprintf(buf, "%s\n", mode_status[status]);
    }

//---------------------------------------------------------------------------
// Handles reads to the knob attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t knob_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	int status;
	status = atomic_read(&knob_value_atomic);
    return sprintf(buf, "%i\n", status);
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(knob, 0444, knob_show, NULL);
static DEVICE_ATTR(mode, 0444, mode_show, NULL);

//---------------------------------------------------------------------------
// Defines the attributes of the target ses interface for sysfs
//---------------------------------------------------------------------------
static const struct attribute * ses_interface_attrs[] =
    {
    &dev_attr_knob.attr,
    &dev_attr_mode.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the target ses interface for sysfs
//---------------------------------------------------------------------------
const struct attribute_group ses_interface_attr_group =
    {
    .attrs = (struct attribute **) ses_interface_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the target ses interface
//---------------------------------------------------------------------------
const struct attribute_group * ses_interface_get_attr_group(void)
    {
    return &ses_interface_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_ses_interface =
    {
    .type     		= TARGET_TYPE_SES_INTERFACE,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= ses_interface_get_attr_group,
    };

static void knob_twisted(struct work_struct * work)
	{
	target_sysfs_notify(&target_device_ses_interface, "knob");
	}

static void mode_changed(struct work_struct * work)
	{
	target_sysfs_notify(&target_device_ses_interface, "mode");
	}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_ses_interface_init(void)
    {
    int retval;
	printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);

	hardware_init();

	INIT_WORK(&knob_work, knob_twisted);
	INIT_WORK(&mode_work, mode_changed);

    retval=target_sysfs_add(&target_device_ses_interface);

    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
    return retval;
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_ses_interface_exit(void)
    {
	hardware_exit();
    target_sysfs_remove(&target_device_ses_interface);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_ses_interface_init);
module_exit(target_ses_interface_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target ses interface module");
MODULE_AUTHOR("jpy");

