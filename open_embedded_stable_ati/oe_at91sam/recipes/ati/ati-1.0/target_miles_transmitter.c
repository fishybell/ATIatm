//---------------------------------------------------------------------------
// target_miles_transmitter.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_miles_transmitter.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"miles transmitter"

#define TIMEOUT_IN_SECONDS	5

#define MILES_TX_STATE_OFF   	0
#define MILES_TX_STATE_ON    	1
#define MILES_TX_STATE_ERROR  	2

#define PIN_MILES_TX_ACTIVE    	0       		// Active low

#ifdef DEV_BOARD_REVB
	#define PIN_MILES_TX_CONTROL    	AT91_PIN_PA6
#else
	#define PIN_MILES_TX_CONTROL    	AT91_PIN_PB8
#endif

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that an operation is in progress,
// i.e. the transmitter has been commanded to turn on. It is used to
// synchronize user-space commands with the actual hardware.
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
// Maps the miles tx state to state name.
//---------------------------------------------------------------------------
static const char * miles_tx_state[] =
    {
    "off",
    "on",
    "error"
    };

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_miles_tx_on(void)
	{
	at91_set_gpio_value(PIN_MILES_TX_CONTROL, PIN_MILES_TX_ACTIVE); // Turn miles tx on
	return 0;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_miles_tx_off(void)
	{
	at91_set_gpio_value(PIN_MILES_TX_CONTROL, !PIN_MILES_TX_ACTIVE); // Turn miles tx off
	return 0;
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
    // Turn the miles tx off
    hardware_miles_tx_off();

    // signal that the operation has finished
    atomic_set(&operating_atomic, 0);
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;

    // configure miles tx gpio for output and set initial output
    at91_set_gpio_output(PIN_MILES_TX_CONTROL, !PIN_MILES_TX_ACTIVE);

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

	hardware_miles_tx_on();

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
		return MILES_TX_STATE_ON;
		}

    return MILES_TX_STATE_OFF;
    }

//---------------------------------------------------------------------------
// Handles reads to the state attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t state_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", miles_tx_state[hardware_state_get()]);
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
    // TODO - should we be able to turn it off also, or let the timer expire?
    else
		{
    	status = size;
		}

    return status;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(state, 0644, state_show, state_store);

//---------------------------------------------------------------------------
// Defines the attributes of the miles tx for sysfs
//---------------------------------------------------------------------------
static const struct attribute * miles_transmitter_attrs[] =
    {
    &dev_attr_state.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the miles tx for sysfs
//---------------------------------------------------------------------------
const struct attribute_group miles_transmitter_attr_group =
    {
    .attrs = (struct attribute **) miles_transmitter_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the miles tx
//---------------------------------------------------------------------------
const struct attribute_group * miles_transmitter_get_attr_group(void)
    {
    return &miles_transmitter_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_miles_transmitter =
    {
    .type     		= TARGET_TYPE_MILES_TRANSMITTER,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= miles_transmitter_get_attr_group,
    };

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_miles_transmitter_init(void)
    {
	printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
	hardware_init();
    return target_sysfs_add(&target_device_miles_transmitter);
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_miles_transmitter_exit(void)
    {
	hardware_exit();
    target_sysfs_remove(&target_device_miles_transmitter);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_miles_transmitter_init);
module_exit(target_miles_transmitter_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target miles transmitter module");
MODULE_AUTHOR("jpy");

