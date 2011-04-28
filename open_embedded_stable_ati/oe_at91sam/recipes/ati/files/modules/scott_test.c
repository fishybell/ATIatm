//---------------------------------------------------------------------------
// scott_test.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "scott_test.h"
#include "target_hardware.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"scott test"

#undef INPUT_TEST_BUTTON_ACTIVE_STATE
#undef INPUT_TEST_BUTTON_PULLUP_STATE
#undef INPUT_TEST_BUTTON_DEGLITCH_STATE
#undef INPUT_TEST_BUTTON

#define	INPUT_TEST_BUTTON_ACTIVE_STATE		ACTIVE_LOW
#define INPUT_TEST_BUTTON_PULLUP_STATE		PULLUP_ON
#define INPUT_TEST_BUTTON_DEGLITCH_STATE	DEGLITCH_ON
#define	INPUT_TEST_BUTTON			AT91_PIN_PA31  // BP4 on dev. board

#undef OUTPUT_TEST_INDICATOR_ACTIVE_STATE
#undef OUTPUT_TEST_INDICATOR

#define	OUTPUT_TEST_INDICATOR_ACTIVE_STATE	ACTIVE_LOW
#define	OUTPUT_TEST_INDICATOR			AT91_PIN_PB8	// LED on dev. board

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space that the bit
// button has been pressed.
//---------------------------------------------------------------------------
static struct work_struct button_work;

//---------------------------------------------------------------------------
// Maps the bit indicator status to status name.
//---------------------------------------------------------------------------
static const char * led_status[] =
    {
    "off",
    "on",
    "error"
    };

//---------------------------------------------------------------------------
// Maps a button status to status name.
//---------------------------------------------------------------------------
static const char * button_status[] =
    {
    "clear",
    "pressed",
    "error"
    };

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_led_status_on(void)
	{
	at91_set_gpio_value(OUTPUT_TEST_INDICATOR, OUTPUT_TEST_INDICATOR_ACTIVE_STATE); // Turn bit status on
	return 0;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_led_status_off(void)
	{
	at91_set_gpio_value(OUTPUT_TEST_INDICATOR, !OUTPUT_TEST_INDICATOR_ACTIVE_STATE); // Turn bit status off
	return 0;
	}

//---------------------------------------------------------------------------
// button has been pressed
//---------------------------------------------------------------------------
irqreturn_t button_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

    return IRQ_HANDLED;
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

    status = request_irq(INPUT_TEST_BUTTON, (void*)button_int, 0, "scott_test_button", NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
        delay_printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_TEST_BUTTON);
            }
        else if (status == -EBUSY)
            {
        delay_printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_TEST_BUTTON);
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
    free_irq(INPUT_TEST_BUTTON, NULL);
    return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_button_get(void)
    {
	int status;
	status = at91_get_gpio_value(INPUT_TEST_BUTTON);
    return (status == INPUT_TEST_BUTTON_ACTIVE_STATE);
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_led_status_get(void)
    {
	int status;
	status = at91_get_gpio_value(OUTPUT_TEST_INDICATOR);
    return (status == OUTPUT_TEST_INDICATOR_ACTIVE_STATE);
    }

//---------------------------------------------------------------------------
// Handles reads to the button attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t button_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", button_status[hardware_button_get()]);
    }

//---------------------------------------------------------------------------
// Handles reads to the bit status attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t led_status_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", led_status[hardware_led_status_get()]);
    }

//---------------------------------------------------------------------------
// Handles writes to the bit status attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t led_status_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    ssize_t status;
    status = size;

    if (sysfs_streq(buf, "on"))
        {
    delay_printk("%s - %s() : user command on\n",TARGET_NAME, __func__);
        hardware_led_status_on();
        }
    else if (sysfs_streq(buf, "off"))
        {
    delay_printk("%s - %s() : user command off\n",TARGET_NAME, __func__);
        hardware_led_status_off();
        }
    else
		{
	delay_printk("%s - %s() : unrecognized user command\n",TARGET_NAME, __func__);
		}

    return status;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(button, 0444, button_show, NULL);
static DEVICE_ATTR(led_status, 0644, led_status_show, led_status_store);

//---------------------------------------------------------------------------
// Defines the attributes of the target scott test for sysfs
//---------------------------------------------------------------------------
static const struct attribute * scott_test_attrs[] =
    {
    &dev_attr_button.attr,
    &dev_attr_led_status.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the target scott test for sysfs
//---------------------------------------------------------------------------
const struct attribute_group scott_test_attr_group =
    {
    .attrs = (struct attribute **) scott_test_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the target scott test
//---------------------------------------------------------------------------
const struct attribute_group * scott_test_get_attr_group(void)
    {
    return &scott_test_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_scott_test =
    {
    .type     		= TARGET_TYPE_SCOTT_TEST,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= scott_test_get_attr_group,
    };

//---------------------------------------------------------------------------
// Work item to notify the user-space about a bit button press
//---------------------------------------------------------------------------
static void button_pressed(struct work_struct * work)
	{
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
	target_sysfs_notify(&target_device_scott_test, "button");
	}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init scott_test_init(void)
    {
    int retval;
delay_printk("%s(): %s - %s\n",__func__,  __DATE__, __TIME__);

	hardware_init();

	INIT_WORK(&button_work, button_pressed);

    retval=target_sysfs_add(&target_device_scott_test);

    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
    return retval;
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit scott_test_exit(void)
    {
    atomic_set(&full_init, FALSE);
    ati_flush_work(&button_work); // close any open work queue items
	hardware_exit();
    target_sysfs_remove(&target_device_scott_test);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(scott_test_init);
module_exit(scott_test_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target scott test module");
MODULE_AUTHOR("ndb");

