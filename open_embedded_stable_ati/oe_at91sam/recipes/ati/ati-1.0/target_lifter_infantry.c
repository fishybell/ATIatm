//---------------------------------------------------------------------------
// target_lifter_infantry.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_lifter_infantry.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"infantry lifter"
#define LIFTER_TYPE  	"infantry"

#define TIMEOUT_IN_SECONDS		5

#define LIFTER_POSITION_DOWN   	0
#define LIFTER_POSITION_UP    	1
#define LIFTER_POSITION_MOVING  2
#define LIFTER_POSITION_ERROR   3

#define PIN_POSITION_ACTIVE   	0       		// Active low
#define PIN_POSITION_DOWN    	AT91_PIN_PA30   // BP3 on dev. board
#define PIN_POSITION_UP     	AT91_PIN_PA31   // BP4 on dev. board

#define PIN_MOTOR_ACTIVE    	0       		// Active low

#ifdef DEV_BOARD_REVB
	#define PIN_MOTOR_CONTROL    	AT91_PIN_PA6
#else
	#define PIN_MOTOR_CONTROL    	AT91_PIN_PB8
#endif


//---------------------------------------------------------------------------
// This lock protects against motor control from simultaneously access from
// IRQs, timeout timer and user-space.
//---------------------------------------------------------------------------
static DEFINE_SPINLOCK(motor_lock);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that an operation is in progress,
// i.e. the lifter has been commanded to move. It is used to synchronize
// user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t operating_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space of a position
// change or error detected by the IRQs or the timeout timer.
//---------------------------------------------------------------------------
static struct work_struct position_work;

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the timeout.
//---------------------------------------------------------------------------
static struct timer_list timeout_timer_list = TIMER_INITIALIZER(timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// Maps the position to position name.
//---------------------------------------------------------------------------
static const char * lifter_position[] =
    {
    "down",
    "up",
    "moving",
    "error"
    };

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_motor_on(int direction)
	{
	unsigned long flags;
	// with the infantry lifter we don't care about direction,
	// just turn on the motor
    spin_lock_irqsave(&motor_lock, flags);
	at91_set_gpio_value(PIN_MOTOR_CONTROL, PIN_MOTOR_ACTIVE); // Turn motor on
	spin_unlock_irqrestore(&motor_lock, flags);
	return 0;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_motor_off(void)
	{
	unsigned long flags;
	spin_lock_irqsave(&motor_lock, flags);
	at91_set_gpio_value(PIN_MOTOR_CONTROL, !PIN_MOTOR_ACTIVE); // Turn motor off
	spin_unlock_irqrestore(&motor_lock, flags);
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
    // Turn the motor off
    hardware_motor_off();

	printk(KERN_ERR "%s - %s() - the operation has timed out.\n",TARGET_NAME, __func__);

    // signal that the operation has finished
    atomic_set(&operating_atomic, 0);

    // notify user-space
    schedule_work(&position_work);
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
irqreturn_t down_position_int(int irq, void *dev_id, struct pt_regs *regs)
    {
	// We get an interrupt on both edges, so we have to check to which edge
	// we are responding.
    if (at91_get_gpio_value(PIN_POSITION_DOWN) == PIN_POSITION_ACTIVE)
        {
    	timeout_timer_stop();

    	printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

        // Turn the motor off
        hardware_motor_off();

        // signal that the operation has finished
        atomic_set(&operating_atomic, 0);

        // notify user-space
        schedule_work(&position_work);
        }

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
irqreturn_t up_position_int(int irq, void *dev_id, struct pt_regs *regs)
    {
	// We get an interrupt on both edges, so we have to check to which edge
	// we are responding.
    if (at91_get_gpio_value(PIN_POSITION_UP) == PIN_POSITION_ACTIVE)
        {
    	timeout_timer_stop();

    	printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

        // Turn the motor off
        hardware_motor_off();

        // signal that the operation has finished
    	atomic_set(&operating_atomic, 0);

        // notify user-space
        schedule_work(&position_work);
        }

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;

    // configure motor gpio for output and set initial output
    at91_set_gpio_output(PIN_MOTOR_CONTROL, !PIN_MOTOR_ACTIVE);

    // Configure position gpios for input and deglitch for interrupts
    at91_set_gpio_input(PIN_POSITION_DOWN, 1);
    at91_set_deglitch(PIN_POSITION_DOWN, 1);
    at91_set_gpio_input(PIN_POSITION_UP, 1);
    at91_set_deglitch(PIN_POSITION_UP, 1);

    status = request_irq(PIN_POSITION_DOWN, (void*)down_position_int, 0, "infantry_target_down", NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
            printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", PIN_POSITION_DOWN);
            }
        else if (status == -EBUSY)
            {
            printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", PIN_POSITION_DOWN);
            }
        return status;
        }

    status = request_irq(PIN_POSITION_UP, (void*)up_position_int, 0, "infantry_target_up", NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
        	printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", PIN_POSITION_UP);
            }
        else if (status == -EBUSY)
            {
        	printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", PIN_POSITION_UP);
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
	free_irq(PIN_POSITION_DOWN, NULL);
	free_irq(PIN_POSITION_UP, NULL);
	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_position_set(int position)
    {
	// signal that an operation is in progress
	atomic_set(&operating_atomic, 1);

	// with the infantry lifter we don't care about position,
	// just turn on the motor
	hardware_motor_on(0);

	timeout_timer_start();

	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_position_get(void)
    {
    // check if an operation is in progress...
    if (atomic_read(&operating_atomic))
		{
		return LIFTER_POSITION_MOVING;
		}

    if (at91_get_gpio_value(PIN_POSITION_DOWN) == PIN_POSITION_ACTIVE)
        {
        return LIFTER_POSITION_DOWN;
        }

    if (at91_get_gpio_value(PIN_POSITION_UP) == PIN_POSITION_ACTIVE)
        {
        return LIFTER_POSITION_UP;
        }

    return LIFTER_POSITION_ERROR;
    }

//---------------------------------------------------------------------------
// Handles reads to the type attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t type_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", LIFTER_TYPE);
    }

//---------------------------------------------------------------------------
// Handles reads to the position attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t position_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", lifter_position[hardware_position_get()]);
    }

//---------------------------------------------------------------------------
// Handles writes to the position attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t position_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    ssize_t status;

    // check if an operation is in progress, if so ignore any command
    if (atomic_read(&operating_atomic))
		{
    	status = size;
		}
    else if (sysfs_streq(buf, "up"))
        {
    	printk(KERN_ALERT "%s - %s() : user command up\n",TARGET_NAME, __func__);
        status = size;
        // TODO - need to check error condition first
        if (hardware_position_get() == LIFTER_POSITION_DOWN)
            {
        	hardware_position_set(LIFTER_POSITION_UP);
            }
        }
    else if (sysfs_streq(buf, "down"))
        {
    	printk(KERN_ALERT "%s - %s() : user command down\n",TARGET_NAME, __func__);
        status = size;
        // TODO - need to check error condition first
        if (hardware_position_get() == LIFTER_POSITION_UP)
            {
            hardware_position_set(LIFTER_POSITION_DOWN);
            }
        }
    else
		{
    	status = size;
		}

    return status;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(type, 0444, type_show, NULL);
static DEVICE_ATTR(position, 0644, position_show, position_store);

//---------------------------------------------------------------------------
// Defines the attributes of the infantry target lifter for sysfs
//---------------------------------------------------------------------------
static const struct attribute * infantry_lifter_attrs[] =
    {
    &dev_attr_type.attr,
    &dev_attr_position.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the infantry target lifter for sysfs
//---------------------------------------------------------------------------
const struct attribute_group infantry_lifter_attr_group =
    {
    .attrs = (struct attribute **) infantry_lifter_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the infantry target lifter
//---------------------------------------------------------------------------
const struct attribute_group * infantry_lifter_get_attr_group(void)
    {
    return &infantry_lifter_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_lifter_infantry =
    {
    .type     		= TARGET_TYPE_LIFTER,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= infantry_lifter_get_attr_group,
    };

//---------------------------------------------------------------------------
// Work item to notify the user-space about a position change or error
//---------------------------------------------------------------------------
static void position_change(struct work_struct * work)
	{
	target_sysfs_notify(&target_device_lifter_infantry, "position");
	}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_lifter_infantry_init(void)
    {
	printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
	hardware_init();
	INIT_WORK(&position_work, position_change);
    return target_sysfs_add(&target_device_lifter_infantry);
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_lifter_infantry_exit(void)
    {
	hardware_exit();
    target_sysfs_remove(&target_device_lifter_infantry);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_lifter_infantry_init);
module_exit(target_lifter_infantry_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target lifter infantry module");
MODULE_AUTHOR("jpy");

