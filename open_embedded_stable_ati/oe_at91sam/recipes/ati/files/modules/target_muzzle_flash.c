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

#define FLASH_ON_IN_MSECONDS		100
#define FLASH_OFF_IN_MSECONDS		100
#define FLASH_BURST_COUNT			5

#define MUZZLE_FLASH_STATE_OFF   	0
#define MUZZLE_FLASH_STATE_ON    	1
#define MUZZLE_FLASH_STATE_ERROR  	2

#define MUZZLE_FLASH_MODE_SINGLE   	0
#define MUZZLE_FLASH_MODE_BURST    	1

#define TESTING_ON_EVAL
#ifdef TESTING_ON_EVAL
	#undef OUTPUT_MUZZLE_FLASH
	#define OUTPUT_MUZZLE_FLASH    	AT91_PIN_PB8
#endif // TESTING_ON_EVAL


//---------------------------------------------------------------------------
// This atomic variable is use to indicate that an operation is in progress,
// i.e. the flash has been commanded to turn on. It is used to synchronize
// user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t operating_atomic = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This atomic variable is use to store the mode. It is
// used to synchronize user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t mode_atomic = ATOMIC_INIT(MUZZLE_FLASH_MODE_SINGLE);

//---------------------------------------------------------------------------
// This atomic variable is use to store the initial delay (in seconds). It is
// used to synchronize user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t initial_delay_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This atomic variable is use to store the repeat delay (in seconds). It is
// used to synchronize user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t repeat_delay_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This atomic variable is use to store the burst count.
//---------------------------------------------------------------------------
atomic_t flash_count_atomic = ATOMIC_INIT(1);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the delay timeout fires.
//---------------------------------------------------------------------------
static void delay_timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the delay timeout.
//---------------------------------------------------------------------------
static struct timer_list delay_timeout_timer_list = TIMER_INITIALIZER(delay_timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the flash timeout fires.
//---------------------------------------------------------------------------
static void flash_timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the flash timeout.
//---------------------------------------------------------------------------
static struct timer_list flash_timeout_timer_list = TIMER_INITIALIZER(flash_timeout_fire, 0, 0);

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
	at91_set_gpio_value(OUTPUT_MUZZLE_FLASH, OUTPUT_MUZZLE_FLASH_ACTIVE_STATE); // Turn flash on
	return 0;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_flash_off(void)
	{
	at91_set_gpio_value(OUTPUT_MUZZLE_FLASH, !OUTPUT_MUZZLE_FLASH_ACTIVE_STATE); // Turn flash off
	return 0;
	}

/*
//---------------------------------------------------------------------------
// Starts the delay timeout timer.
//---------------------------------------------------------------------------
static void delay_timeout_timer_start(void)
	{
	// TODO - add single vs. burst setting
	mod_timer(&delay_timeout_timer_list, jiffies+(FLASH_ON_IN_SECONDS*HZ));
	}
*/

//---------------------------------------------------------------------------
// Stops the all the timeout timers.
//---------------------------------------------------------------------------
static void timeout_timers_stop(void)
	{
	del_timer(&delay_timeout_timer_list);
	del_timer(&flash_timeout_timer_list);
	}


//---------------------------------------------------------------------------
// The function that gets called when the delay timeout fires.
//---------------------------------------------------------------------------
static void delay_timeout_fire(unsigned long data)
	{
	flash_timeout_fire(0);
	}

//---------------------------------------------------------------------------
// The function that gets called when the flash timeout fires.
//---------------------------------------------------------------------------
static void flash_timeout_fire(unsigned long data)
	{
	if (at91_get_gpio_value(OUTPUT_MUZZLE_FLASH) == OUTPUT_MUZZLE_FLASH_ACTIVE_STATE)
		{
		at91_set_gpio_value(OUTPUT_MUZZLE_FLASH, !OUTPUT_MUZZLE_FLASH_ACTIVE_STATE); // Turn flash off

		if (atomic_dec_and_test(&flash_count_atomic) == TRUE)
			{
			if(atomic_read(&repeat_delay_atomic) > 0)
				{
				if (atomic_read(&mode_atomic) == MUZZLE_FLASH_MODE_BURST)
					{
					atomic_set(&flash_count_atomic, FLASH_BURST_COUNT);
					}
				else
					{
					atomic_set(&flash_count_atomic, 1);
					}
				mod_timer(&flash_timeout_timer_list, jiffies+(atomic_read(&repeat_delay_atomic)*HZ));
				}
			else
				{
				// signal that the operation has finished
				atomic_set(&operating_atomic, FALSE);
				}
			}
		else
			{
			mod_timer(&flash_timeout_timer_list, jiffies+(FLASH_OFF_IN_MSECONDS*HZ/1000));
			}
		}
	else
		{
		at91_set_gpio_value(OUTPUT_MUZZLE_FLASH, OUTPUT_MUZZLE_FLASH_ACTIVE_STATE); // Turn flash on
		mod_timer(&flash_timeout_timer_list, jiffies+(FLASH_ON_IN_MSECONDS*HZ/1000));
		}
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;

    // configure flash gpio for output and set initial output
    at91_set_gpio_output(OUTPUT_MUZZLE_FLASH, !OUTPUT_MUZZLE_FLASH_ACTIVE_STATE);

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
static int hardware_state_set(int on)
    {
	if (on == TRUE)
		{
		// check if an operation is in progress, if so ignore any command
		if (atomic_read(&operating_atomic) == FALSE)
			{
			// signal that the operation is in progress
			atomic_set(&operating_atomic, TRUE);

			at91_set_gpio_value(OUTPUT_MUZZLE_FLASH, !OUTPUT_MUZZLE_FLASH_ACTIVE_STATE); // Turn flash off

			if (atomic_read(&mode_atomic) == MUZZLE_FLASH_MODE_BURST)
				{
				atomic_set(&flash_count_atomic, FLASH_BURST_COUNT);
				}
			else
				{
				atomic_set(&flash_count_atomic, 1);
				}

			if (atomic_read(&initial_delay_atomic) > 0)
				{
				mod_timer(&delay_timeout_timer_list, jiffies+(atomic_read(&initial_delay_atomic)*HZ));
				}
			else
				{
				mod_timer(&delay_timeout_timer_list, jiffies+(100*HZ/1000));
				}
			}
		}
	else if (on == FALSE)
		{
		timeout_timers_stop();
		at91_set_gpio_value(OUTPUT_MUZZLE_FLASH, !OUTPUT_MUZZLE_FLASH_ACTIVE_STATE); // Turn flash off

	    // signal that the operation has finished
	    atomic_set(&operating_atomic, FALSE);
		}
	else
		{
	delay_printk("%s - %s() : unrecognized command\n",TARGET_NAME, __func__);
		}

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
    status = size;

    if (sysfs_streq(buf, "on"))
        {
    delay_printk("%s - %s() : user command on\n",TARGET_NAME, __func__);
        hardware_state_set(TRUE);
        }
    else if (sysfs_streq(buf, "off"))
		{
	delay_printk("%s - %s() : user command off\n",TARGET_NAME, __func__);
        hardware_state_set(FALSE);
		}
    else
		{
	delay_printk("%s - %s() : unrecognized user command\n",TARGET_NAME, __func__);
		}

    return status;
    }

//---------------------------------------------------------------------------
// Handles reads to the flash mode attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", muzzle_flash_mode[atomic_read(&mode_atomic)]);
    }

//---------------------------------------------------------------------------
// Handles writes to the flash mode attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    ssize_t status;

    status = size;

    if (sysfs_streq(buf, "single"))
        {
    delay_printk("%s - %s() : mode set to single\n",TARGET_NAME, __func__);
    	atomic_set(&mode_atomic, MUZZLE_FLASH_MODE_SINGLE);
        }
    else if (sysfs_streq(buf, "burst"))
        {
    delay_printk("%s - %s() : mode set to burst\n",TARGET_NAME, __func__);
    	atomic_set(&mode_atomic, MUZZLE_FLASH_MODE_BURST);
        }
    else
		{
	delay_printk("%s - %s() : mode setting unrecognized\n",TARGET_NAME, __func__);
		}

    return status;
    }

//---------------------------------------------------------------------------
// Handles reads to the flash repeat delay attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t repeat_delay_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	return sprintf(buf, "%d\n", atomic_read(&repeat_delay_atomic));
    }

//---------------------------------------------------------------------------
// Handles writes to the repeat delay attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t repeat_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    long value;
    ssize_t status;

    status = size;

    if ((strict_strtol(buf, 0, &value) == 0) &&
			(value >= 0) &&
			(value <= 60))
		{
		atomic_set(&repeat_delay_atomic, value);
		}
	else
		{
	delay_printk("%s - %s() : repeat delay out of range 0-60 (%s)\n",TARGET_NAME, __func__, buf);
		}

    return status;
    }

//---------------------------------------------------------------------------
// Handles reads to the flash initial delay attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t initial_delay_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	return sprintf(buf, "%d\n", atomic_read(&initial_delay_atomic));
    }

//---------------------------------------------------------------------------
// Handles writes to the initial delay attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t initial_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    long value;
    ssize_t status;

    status = size;

    if ((strict_strtol(buf, 0, &value) == 0) &&
			(value >= 0) &&
			(value <= 60))
		{
		atomic_set(&initial_delay_atomic, value);
		}
	else
		{
	delay_printk("%s - %s() : initial delay out of range 0-60 (%s)\n",TARGET_NAME, __func__, buf);
		}

    return status;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(state, 0644, state_show, state_store);
static DEVICE_ATTR(mode, 0644, mode_show, mode_store);
static DEVICE_ATTR(repeat_delay, 0644, repeat_delay_show, repeat_delay_store);
static DEVICE_ATTR(initial_delay, 0644, initial_delay_show, initial_delay_store);

//---------------------------------------------------------------------------
// Defines the attributes of the muzzle flash for sysfs
//---------------------------------------------------------------------------
static const struct attribute * muzzle_flash_attrs[] =
    {
    &dev_attr_state.attr,
    &dev_attr_mode.attr,
    &dev_attr_repeat_delay.attr,
    &dev_attr_initial_delay.attr,
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
delay_printk("%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
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

