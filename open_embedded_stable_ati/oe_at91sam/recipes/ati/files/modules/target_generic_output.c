//---------------------------------------------------------------------------
// target_generic_output.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_generic_output.h"
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
// These variables are parameters given when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
static int has_moon = FALSE;				// has moon glow light
module_param(has_moon, bool, S_IRUGO);
static int has_muzzle = FALSE;				// has muzzle flash simulator
module_param(has_muzzle, bool, S_IRUGO);
static int has_phi = FALSE;					// has positive hit indicator light
module_param(has_phi, bool, S_IRUGO);
static int has_ses = FALSE;					// has SES
module_param(has_ses, bool, S_IRUGO);
static int has_msdh = FALSE;				// has MSDH
module_param(has_msdh, bool, S_IRUGO);
static int has_thermalX = FALSE;			// has X thermal generators
module_param(has_thermalX, int, S_IRUGO);
static int has_smokeX = FALSE;				// has X smoke generators
module_param(has_smokeX, int, S_IRUGO);


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
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_generic_output_init(void)
    {
delay_printk("%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
	hardware_init();
	return 0;
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_generic_output_exit(void)
    {
	hardware_exit();
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_generic_output_init);
module_exit(target_generic_output_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target muzzle flash simulator module");
MODULE_AUTHOR("jpy");

