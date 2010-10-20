//---------------------------------------------------------------------------
// target_user_interface.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_user_interface.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"user interface"

#define BIT_DOUBLE_IN_MSECONDS		2000
#define BIT_READ_IN_MSECONDS		50
#define BIT_CANCEL_IN_MSECONDS		5000

#define BLINK_ON_IN_MSECONDS		1000
#define BLINK_OFF_IN_MSECONDS		500

#define BIT_STATUS_OFF   	0
#define BIT_STATUS_ON    	1
#define BIT_STATUS_BLINK   	2
#define BIT_STATUS_ERROR   	3

// moves from clear, to unclear, to reclear, to press as we need a double click
#define BUTTON_STATUS_CLEAR   	0
#define BUTTON_STATUS_UNCLEAR  	1
#define BUTTON_STATUS_RECLEAR  	2
#define BUTTON_STATUS_PRESSED  	3
#define BUTTON_STATUS_ERROR   	4


//#define TESTING_ON_EVAL
#ifdef TESTING_ON_EVAL
	#undef INPUT_TEST_BUTTON_ACTIVE_STATE
	#undef INPUT_TEST_BUTTON_PULLUP_STATE
	#undef INPUT_TEST_BUTTON_DEGLITCH_STATE
	#undef INPUT_TEST_BUTTON

	#undef OUTPUT_TEST_INDICATOR_ACTIVE_STATE
	#undef OUTPUT_TEST_INDICATOR

	#define	INPUT_TEST_BUTTON_ACTIVE_STATE		ACTIVE_HIGH
	#define INPUT_TEST_BUTTON_PULLUP_STATE		PULLUP_ON
	#define INPUT_TEST_BUTTON_DEGLITCH_STATE	DEGLITCH_ON
	#define	INPUT_TEST_BUTTON 					AT91_PIN_PA31   // BP4 on dev. board

	#define	OUTPUT_TEST_INDICATOR_ACTIVE_STATE	ACTIVE_LOW
	#define	OUTPUT_TEST_INDICATOR 				AT91_PIN_PB8	// LED on dev. board

#endif // TESTING_ON_EVAL

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that an the bit button has been
// pressed. It gets cleared when user-space reads
//---------------------------------------------------------------------------
atomic_t bit_button_atomic = ATOMIC_INIT(BUTTON_STATUS_CLEAR);

//---------------------------------------------------------------------------
// This atomic variable is use to store the bit indicator setting.
//---------------------------------------------------------------------------
atomic_t bit_indicator_atomic = ATOMIC_INIT(BIT_STATUS_OFF);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the press timeout fires.
//---------------------------------------------------------------------------
static void press_timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the blink timeout.
//---------------------------------------------------------------------------
static struct timer_list press_timeout_timer_list = TIMER_INITIALIZER(press_timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the blink timeout fires.
//---------------------------------------------------------------------------
static void blink_timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the blink timeout.
//---------------------------------------------------------------------------
static struct timer_list blink_timeout_timer_list = TIMER_INITIALIZER(blink_timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the bit read timeout fires.
//---------------------------------------------------------------------------
static void bit_read_timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the bit read timeout.
//---------------------------------------------------------------------------
static struct timer_list bit_read_timeout_timer_list = TIMER_INITIALIZER(bit_read_timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the bit timeout fires.
//---------------------------------------------------------------------------
static void bit_timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the bit timeout.
//---------------------------------------------------------------------------
static struct timer_list bit_timeout_timer_list = TIMER_INITIALIZER(bit_timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space that the bit
// button has been pressed.
//---------------------------------------------------------------------------
static struct work_struct bit_button_work;

//---------------------------------------------------------------------------
// Maps the bit indicator status to status name.
//---------------------------------------------------------------------------
static const char * bit_led_status[] =
    {
    "off",
    "on",
    "blink",
    "error"
    };

//---------------------------------------------------------------------------
// Maps a button status to status name.
//---------------------------------------------------------------------------
static const char * button_status[] =
    {
    "clear",
    "clear",
    "clear",
    "pressed",
    "error"
    };

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_bit_status_on(void)
	{
	at91_set_gpio_value(OUTPUT_TEST_INDICATOR, OUTPUT_TEST_INDICATOR_ACTIVE_STATE); // Turn bit status on
	return 0;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_bit_status_off(void)
	{
	at91_set_gpio_value(OUTPUT_TEST_INDICATOR, !OUTPUT_TEST_INDICATOR_ACTIVE_STATE); // Turn bit status off
	return 0;
	}

//---------------------------------------------------------------------------
// The function that gets called when the bit timeout fires.
//---------------------------------------------------------------------------
static void bit_timeout_fire(unsigned long data)
    {
    printk(KERN_ALERT "%s - %s\n",TARGET_NAME, __func__);
    if (atomic_read(&bit_button_atomic) != BUTTON_STATUS_PRESSED)
        {
        atomic_set(&bit_button_atomic, BUTTON_STATUS_CLEAR);
        }
    }

//---------------------------------------------------------------------------
// The function that gets called when the press timeout fires.
//---------------------------------------------------------------------------
static void press_timeout_fire(unsigned long data)
    {
    printk(KERN_ALERT "%s - %s\n",TARGET_NAME, __func__);
    atomic_set(&bit_button_atomic, BUTTON_STATUS_CLEAR);

    // notify user-space only when fully double-clicked
    schedule_work(&bit_button_work);
    }

//---------------------------------------------------------------------------
// The function that gets called when the blink timeout fires.
//---------------------------------------------------------------------------
static void blink_timeout_fire(unsigned long data)
	{
	if (at91_get_gpio_value(OUTPUT_TEST_INDICATOR) == OUTPUT_TEST_INDICATOR_ACTIVE_STATE)
		{
		at91_set_gpio_value(OUTPUT_TEST_INDICATOR, !OUTPUT_TEST_INDICATOR_ACTIVE_STATE); // Turn LED off
		mod_timer(&blink_timeout_timer_list, jiffies+(BLINK_OFF_IN_MSECONDS*HZ/1000));
		}
	else
		{
		at91_set_gpio_value(OUTPUT_TEST_INDICATOR, OUTPUT_TEST_INDICATOR_ACTIVE_STATE); // Turn LED on
		mod_timer(&blink_timeout_timer_list, jiffies+(BLINK_ON_IN_MSECONDS*HZ/1000));
		}
	}

//---------------------------------------------------------------------------
// double press required to trip
//---------------------------------------------------------------------------
irqreturn_t bit_button_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    int value;
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

    // check if the button has been pressed but not read by user-space
    value = atomic_read(&bit_button_atomic);
    if (value == BUTTON_STATUS_PRESSED)
	{
	return IRQ_HANDLED;
	}

    // handle the actual read after a short timeout to let the button settle
    del_timer(&bit_read_timeout_timer_list);
    mod_timer(&bit_read_timeout_timer_list, jiffies+(BIT_READ_IN_MSECONDS*HZ/1000));

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// The function that gets called when the bit read timeout fires.
//---------------------------------------------------------------------------
static void bit_read_timeout_fire(unsigned long data)
    {
    int value;
    // read in value of bit button
    value = atomic_read(&bit_button_atomic);

    printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

    // We get an interrupt on both edges, so we have to check to which edge
    //  we are responding.
    if (at91_get_gpio_value(INPUT_TEST_BUTTON) == INPUT_TEST_BUTTON_ACTIVE_STATE)
        {
        printk(KERN_ALERT "%s - ACTIVE\n",TARGET_NAME);
        if (value == BUTTON_STATUS_CLEAR || BUTTON_STATUS_RECLEAR)
            {
            // signal that the wake-up button has been pressed
            atomic_set(&bit_button_atomic, ++value);

            // schedule a timeout in case we don't double-click
            if (value != BUTTON_STATUS_PRESSED)
                {
                mod_timer(&bit_timeout_timer_list, jiffies+(BIT_DOUBLE_IN_MSECONDS*HZ/1000));
                }
            else
                {
                // notify user-space only when fully double-clicked
                schedule_work(&bit_button_work);

                // at some point clear out the press
                mod_timer(&press_timeout_timer_list, jiffies+(BIT_CANCEL_IN_MSECONDS*HZ/1000));
                }
            }
        }
    else
        {
        printk(KERN_ALERT "%s - INACTIVE\n",TARGET_NAME);
        if (value == BUTTON_STATUS_UNCLEAR)
            {
            // signal that the wake-up button has been pressed
            atomic_set(&bit_button_atomic, ++value);

            // schedule a timeout in case we don't double-click
            mod_timer(&bit_timeout_timer_list, jiffies+(BIT_DOUBLE_IN_MSECONDS*HZ/1000));
            }
        }

    printk(KERN_ALERT "%s - %i\n",TARGET_NAME, value);
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;

    // configure bit status gpio for output and set initial output
    at91_set_gpio_output(OUTPUT_TEST_INDICATOR, !OUTPUT_TEST_INDICATOR_ACTIVE_STATE);

    // Configure button gpios for input and deglitch for interrupts
    at91_set_gpio_input(INPUT_TEST_BUTTON, INPUT_TEST_BUTTON_PULLUP_STATE);
    at91_set_deglitch(INPUT_TEST_BUTTON, INPUT_TEST_BUTTON_DEGLITCH_STATE);

    status = request_irq(INPUT_TEST_BUTTON, (void*)bit_button_int, 0, "user_interface_bit_button", NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
        	printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_TEST_BUTTON);
            }
        else if (status == -EBUSY)
            {
        	printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_TEST_BUTTON);
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
    del_timer(&press_timeout_timer_list);
    del_timer(&bit_timeout_timer_list);
    del_timer(&bit_read_timeout_timer_list);
    del_timer(&blink_timeout_timer_list);
    free_irq(INPUT_TEST_BUTTON, NULL);
    return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_bit_status_get(void)
    {
	int status;
	status = at91_get_gpio_value(OUTPUT_TEST_INDICATOR);
    return (status == OUTPUT_TEST_INDICATOR_ACTIVE_STATE);
    }

//---------------------------------------------------------------------------
// Handles reads to the bit_button attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t bit_button_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    int status;
    status = atomic_read(&bit_button_atomic);
    // clear if we were pressed
    if (status == BUTTON_STATUS_PRESSED)
        {
        // cancel the bit timer
        del_timer(&bit_timeout_timer_list);
        atomic_set(&bit_button_atomic, BUTTON_STATUS_CLEAR);

        // cancel the press clear timer
        del_timer(&press_timeout_timer_list);
        }
    return sprintf(buf, "%s\n", button_status[status]);
    }

//---------------------------------------------------------------------------
// Handles reads to the bit status attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t bit_status_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", bit_led_status[hardware_bit_status_get()]);
    }

//---------------------------------------------------------------------------
// Handles writes to the bit status attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t bit_status_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    ssize_t status;
    status = size;

    if (sysfs_streq(buf, "on"))
        {
    	printk(KERN_ALERT "%s - %s() : user command on\n",TARGET_NAME, __func__);
    	del_timer(&blink_timeout_timer_list);
        hardware_bit_status_on();
        }
    else if (sysfs_streq(buf, "off"))
        {
    	printk(KERN_ALERT "%s - %s() : user command off\n",TARGET_NAME, __func__);
    	del_timer(&blink_timeout_timer_list);
        hardware_bit_status_off();
        }
    else if (sysfs_streq(buf, "blink"))
		{
		printk(KERN_ALERT "%s - %s() : user command blink\n",TARGET_NAME, __func__);
		mod_timer(&blink_timeout_timer_list, jiffies+(100*HZ/1000));
		}
    else
		{
		printk(KERN_ALERT "%s - %s() : unrecognized user command\n",TARGET_NAME, __func__);
		}

    return status;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(bit_button, 0444, bit_button_show, NULL);
static DEVICE_ATTR(bit_status, 0644, bit_status_show, bit_status_store);

//---------------------------------------------------------------------------
// Defines the attributes of the target user interface for sysfs
//---------------------------------------------------------------------------
static const struct attribute * user_interface_attrs[] =
    {
    &dev_attr_bit_button.attr,
    &dev_attr_bit_status.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the target user interface for sysfs
//---------------------------------------------------------------------------
const struct attribute_group user_interface_attr_group =
    {
    .attrs = (struct attribute **) user_interface_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the target user interface
//---------------------------------------------------------------------------
const struct attribute_group * user_interface_get_attr_group(void)
    {
    return &user_interface_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_user_interface =
    {
    .type     		= TARGET_TYPE_USER_INTERFACE,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= user_interface_get_attr_group,
    };

//---------------------------------------------------------------------------
// Work item to notify the user-space about a bit button press
//---------------------------------------------------------------------------
static void bit_pressed(struct work_struct * work)
	{
	target_sysfs_notify(&target_device_user_interface, "bit_button");
	}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_user_interface_init(void)
    {
    int retval;
	printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);

	hardware_init();

	INIT_WORK(&bit_button_work, bit_pressed);

    retval=target_sysfs_add(&target_device_user_interface);

    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
    return retval;
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_user_interface_exit(void)
    {
	hardware_exit();
    target_sysfs_remove(&target_device_user_interface);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_user_interface_init);
module_exit(target_user_interface_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target user interface module");
MODULE_AUTHOR("jpy");

