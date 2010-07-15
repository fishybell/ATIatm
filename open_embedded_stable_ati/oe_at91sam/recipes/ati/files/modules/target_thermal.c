//---------------------------------------------------------------------------
// target_thermal.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_thermal.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"thermal"

#define TIMEOUT_IN_SECONDS	5

#define THERMAL_STATE_OFF   	0
#define THERMAL_STATE_ON    	1
#define THERMAL_STATE_ERROR  	2

#define TESTING_ON_EVAL
#ifdef TESTING_ON_EVAL
	#undef OUTPUT_THERMAL
	#define OUTPUT_THERMAL    	AT91_PIN_PA6
#endif // TESTING_ON_EVAL

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that an operation is in progress,
// i.e. the thermal device has been commanded to turn on. It is used to
// synchronize user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t operating_atomic = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the timeout.
//---------------------------------------------------------------------------
static struct timer_list timeout_timer_list = TIMER_INITIALIZER(timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// Maps the thermal device state to state name.
//---------------------------------------------------------------------------
static const char * thermal_state[] =
    {
    "off",
    "on",
    "error"
    };

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_thermal_on(void)
	{
	at91_set_gpio_value(OUTPUT_THERMAL, OUTPUT_THERMAL_ACTIVE_STATE); // Turn thermal device on
	return TRUE;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_thermal_off(void)
	{
	at91_set_gpio_value(OUTPUT_THERMAL, !OUTPUT_THERMAL_ACTIVE_STATE); // Turn thermal device off
	return TRUE;
	}

//---------------------------------------------------------------------------
// Starts the timeout timer.
//---------------------------------------------------------------------------
static void timeout_timer_start(void)
	{
	mod_timer(&timeout_timer_list, jiffies+(TIMEOUT_IN_SECONDS*HZ));
	}

//---------------------------------------------------------------------------
// Stops the timeout timer.
//---------------------------------------------------------------------------
static void timeout_timer_stop(void)
	{
	del_timer(&timeout_timer_list);
	}

//---------------------------------------------------------------------------
// The function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data)
	{
    // Turn the thermal device off
    hardware_thermal_off();

    // signal that the operation has finished
    atomic_set(&operating_atomic, FALSE);
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    // configure thermal device gpio for output and set initial output
    at91_set_gpio_output(OUTPUT_THERMAL, !OUTPUT_THERMAL_ACTIVE_STATE);

    return TRUE;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
	// Don't leave the thermal on
	hardware_thermal_off();

	return TRUE;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_state_set(void)
    {
	// signal that thermal is on
	atomic_set(&operating_atomic, TRUE);

	hardware_thermal_on();

	timeout_timer_start();

	return TRUE;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_state_get(void)
    {
    // check if thermal is on
    if (atomic_read(&operating_atomic))
		{
		return THERMAL_STATE_ON;
		}

    return THERMAL_STATE_OFF;
    }

//---------------------------------------------------------------------------
// Handles reads to the state attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t state_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", thermal_state[hardware_state_get()]);
    }

//---------------------------------------------------------------------------
// Handles writes to the state attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t state_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    ssize_t status;
    status = size;

    // check if an operation is in progress, if so ignore any command
    if (atomic_read(&operating_atomic))
		{
		//
		}
    else if (sysfs_streq(buf, "on"))
        {
    	printk(KERN_ALERT "%s - %s() : user command on\n",TARGET_NAME, __func__);
        hardware_state_set();
        }

    // TODO - should we be able to turn it off also, or let the timer expire?

    return status;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(state, 0644, state_show, state_store);

//---------------------------------------------------------------------------
// Defines the attributes of the thermal device for sysfs
//---------------------------------------------------------------------------
static const struct attribute * thermal_attrs[] =
    {
    &dev_attr_state.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the thermal device for sysfs
//---------------------------------------------------------------------------
const struct attribute_group thermal_attr_group =
    {
    .attrs = (struct attribute **) thermal_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the thermal device
//---------------------------------------------------------------------------
const struct attribute_group * thermal_get_attr_group(void)
    {
    return &thermal_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_thermal =
    {
    .type     		= TARGET_TYPE_THERMAL,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= thermal_get_attr_group,
    };

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_thermal_init(void)
    {
	printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
	hardware_init();
    return target_sysfs_add(&target_device_thermal);
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_thermal_exit(void)
    {
	hardware_exit();
    target_sysfs_remove(&target_device_thermal);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_thermal_init);
module_exit(target_thermal_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target thermal device module");
MODULE_AUTHOR("jpy");

