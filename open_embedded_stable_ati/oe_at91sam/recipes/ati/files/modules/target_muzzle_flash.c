//---------------------------------------------------------------------------
// target_muzzle_flash.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_muzzle_flash.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"muzzle flash simulator"

#define TIMEOUT_IN_SECONDS_SINGLE	2
#define TIMEOUT_IN_SECONDS_BURST	5

#define MUZZLE_FLASH_STATE_OFF   	0
#define MUZZLE_FLASH_STATE_ON    	1
#define MUZZLE_FLASH_STATE_ERROR  	2

#define MUZZLE_FLASH_MODE_SINGLE   	0
#define MUZZLE_FLASH_MODE_BURST    	1

#define PIN_FLASH_ACTIVE    	0       		// Active low

#ifdef DEV_BOARD_REVB
	#define PIN_FLASH_CONTROL    	AT91_PIN_PA6
#else
	#define PIN_FLASH_CONTROL    	AT91_PIN_PB8
#endif


//---------------------------------------------------------------------------
// Keep track of the mode / repeat delay setting
//---------------------------------------------------------------------------
static int flash_mode 			= MUZZLE_FLASH_MODE_SINGLE;
static int flash_repeat_delay 	= 0;

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that an operation is in progress,
// i.e. the flash has been commanded to turn on. It is used to synchronize
// user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t operating_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the timeout.
//---------------------------------------------------------------------------
static struct timer_list timeout_timer_list = TIMER_INITIALIZER(timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// Maps the state to state name.
//---------------------------------------------------------------------------
static const char * muzzle_flash_state[] =
    {
    "off",
    "on",
    "error"
    };

//---------------------------------------------------------------------------
// Maps the mode to mode name.
//---------------------------------------------------------------------------
static const char * muzzle_flash_mode[] =
    {
    "single",
    "burst"
    };

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_flash_on(void)
	{
	at91_set_gpio_value(PIN_FLASH_CONTROL, PIN_FLASH_ACTIVE); // Turn flash on
	return 0;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_flash_off(void)
	{
	at91_set_gpio_value(PIN_FLASH_CONTROL, !PIN_FLASH_ACTIVE); // Turn flash off
	return 0;
	}

//---------------------------------------------------------------------------
// Starts the timeout timer.
//---------------------------------------------------------------------------
static void timeout_timer_start(void)
	{
	// TODO - add single vs. burst setting
	mod_timer(&timeout_timer_list, jiffies+(TIMEOUT_IN_SECONDS_SINGLE*HZ));
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
    // Turn the flash off
    hardware_flash_off();

    // signal that the operation has finished
    atomic_set(&operating_atomic, 0);
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;

    // configure flash gpio for output and set initial output
    at91_set_gpio_output(PIN_FLASH_CONTROL, !PIN_FLASH_ACTIVE);

    return status;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_state_set(void)
    {
	// signal that an operation is in progress
	atomic_set(&operating_atomic, 1);

	hardware_flash_on();

	timeout_timer_start();
	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_state_get(void)
    {
    // check if an operation is in progress...
    if (atomic_read(&operating_atomic))
		{
		return MUZZLE_FLASH_STATE_ON;
		}

    return MUZZLE_FLASH_STATE_OFF;
    }

//---------------------------------------------------------------------------
// Handles reads to the state attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t state_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", muzzle_flash_state[hardware_state_get()]);
    }

//---------------------------------------------------------------------------
// Handles writes to the state attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t state_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    ssize_t status;

    // check if an operation is in progress, if so ignore any command
    if (atomic_read(&operating_atomic))
		{
    	status = size;
		}
    else if (sysfs_streq(buf, "on"))
        {
    	printk(KERN_ALERT "%s - %s() : user command on\n",TARGET_NAME, __func__);
        status = size;
        hardware_state_set();
        }
    else
		{
    	status = size;
		}

    return status;
    }

//---------------------------------------------------------------------------
// Handles reads to the flash mode attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", muzzle_flash_mode[flash_mode]);
    }

//---------------------------------------------------------------------------
// Handles writes to the flash mode attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    ssize_t status;

    // check if an operation is in progress, if so ignore any command
    if (atomic_read(&operating_atomic))
		{
    	status = size;
		}
    else if (sysfs_streq(buf, "single"))
        {
    	printk(KERN_ALERT "%s - %s() : mode set to single\n",TARGET_NAME, __func__);
    	flash_mode = MUZZLE_FLASH_MODE_SINGLE;
    	status = size;
        }
    else if (sysfs_streq(buf, "burst"))
        {
    	printk(KERN_ALERT "%s - %s() : mode set to burst\n",TARGET_NAME, __func__);
    	flash_mode = MUZZLE_FLASH_MODE_BURST;
    	status = size;
        }
    else
		{
    	status = size;
		}

    return status;
    }

//---------------------------------------------------------------------------
// Handles reads to the flash repeat delay attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t repeat_delay_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	return sprintf(buf, "%d\n", flash_repeat_delay);
    }

//---------------------------------------------------------------------------
// Handles writes to the repeat delay attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t repeat_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    long value;
    ssize_t status;

    // check if an operation is in progress, if so ignore any command
    if (atomic_read(&operating_atomic))
		{
    	status = size;
		}
    else if ((strict_strtol(buf, 0, &value) == 0) &&
			(value >= 0) &&
			(value <= 60))
		{
		flash_repeat_delay = value;
		status = size;
		}
	else
		{
		printk(KERN_ALERT "%s - %s() : repeat delay out of range 0-60 (%s)\n",TARGET_NAME, __func__, buf);
		status = size;
		}

    return status;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(state, 0644, state_show, state_store);
static DEVICE_ATTR(mode, 0644, mode_show, mode_store);
static DEVICE_ATTR(repeat_delay, 0644, repeat_delay_show, repeat_delay_store);

//---------------------------------------------------------------------------
// Defines the attributes of the muzzle flash for sysfs
//---------------------------------------------------------------------------
static const struct attribute * muzzle_flash_attrs[] =
    {
    &dev_attr_state.attr,
    &dev_attr_mode.attr,
    &dev_attr_repeat_delay.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the muzzle flash for sysfs
//---------------------------------------------------------------------------
const struct attribute_group muzzle_flash_attr_group =
    {
    .attrs = (struct attribute **) muzzle_flash_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the muzzle flash
//---------------------------------------------------------------------------
const struct attribute_group * muzzle_flash_get_attr_group(void)
    {
    return &muzzle_flash_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_muzzle_flash =
    {
    .type     		= TARGET_TYPE_MUZZLE_FLASH,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= muzzle_flash_get_attr_group,
    };

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_muzzle_flash_init(void)
    {
	printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
	hardware_init();
    return target_sysfs_add(&target_device_muzzle_flash);
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_muzzle_flash_exit(void)
    {
	hardware_exit();
    target_sysfs_remove(&target_device_muzzle_flash);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_muzzle_flash_init);
module_exit(target_muzzle_flash_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target muzzle flash simulator module");
MODULE_AUTHOR("jpy");

