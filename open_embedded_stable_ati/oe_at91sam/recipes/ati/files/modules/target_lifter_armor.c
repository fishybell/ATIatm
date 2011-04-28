//---------------------------------------------------------------------------
// target_lifter_armor.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_lifter_armor.h"
#include "target_lifter_infantry.h" // shared constants and function definitions

#include "target_generic_output.h"

#include "netlink_kernel.h"
//---------------------------------------------------------------------------
// #define TESTING_ON_EVAL
#ifdef TESTING_ON_EVAL
	//for testing using eval. board buttons and LED
	#undef INPUT_LIFTER_POS_ACTIVE_STATE
	#undef INPUT_LIFTER_POS_DOWN_LIMIT
	#undef INPUT_LIFTER_POS_UP_LIMIT

	#undef OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE
	#undef OUTPUT_LIFTER_MOTOR_FWD_POS
	#undef OUTPUT_LIFTER_MOTOR_REV_POS

	#define INPUT_LIFTER_POS_ACTIVE_STATE   	ACTIVE_LOW
	#define INPUT_LIFTER_POS_DOWN_LIMIT    		AT91_PIN_PA30   // BP3 on dev. board
	#define INPUT_LIFTER_POS_UP_LIMIT     		AT91_PIN_PA31   // BP4 on dev. board

	#define OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE    ACTIVE_LOW
	#define OUTPUT_LIFTER_MOTOR_FWD_POS    			AT91_PIN_PB8
	#define OUTPUT_LIFTER_MOTOR_REV_POS    			AT91_PIN_PB9
#endif // TESTING_ON_EVAL


#define TARGET_NAME  "armor lifter"
#define LIFTER_TYPE  "armor"

#define TIMEOUT_IN_SECONDS  14

#undef INPUT_LIFTER_POS_ACTIVE_STATE
#define INPUT_LIFTER_POS_ACTIVE_STATE		ACTIVE_LOW

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
atomic_t operating_atomic = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

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
    "neither"
    };

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_motor_on(int direction)
    {
    unsigned long flags;

   delay_printk("%s - %s()\n",TARGET_NAME, __func__);
    spin_lock_irqsave(&motor_lock, flags);

    // These don't get used with the SAT but we set them everytime we get the chance for safety
    at91_set_gpio_value(OUTPUT_LIFTER_MOTOR_REV_NEG, !OUTPUT_LIFTER_MOTOR_NEG_ACTIVE_STATE);
    at91_set_gpio_value(OUTPUT_LIFTER_MOTOR_FWD_NEG, !OUTPUT_LIFTER_MOTOR_NEG_ACTIVE_STATE);

    at91_set_gpio_value(OUTPUT_LIFTER_MOTOR_FWD_POS, !OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE); 	// forward off
    at91_set_gpio_value(OUTPUT_LIFTER_MOTOR_REV_POS, !OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE); 	// reverse off

    if (direction == LIFTER_POSITION_UP)
        {
   delay_printk("%s - %s() - up\n",TARGET_NAME, __func__);

        at91_set_gpio_value(OUTPUT_LIFTER_MOTOR_FWD_POS, OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE); 	// forward on
        }
    else if (direction == LIFTER_POSITION_DOWN)
        {
   delay_printk("%s - %s() - down\n",TARGET_NAME, __func__);

        at91_set_gpio_value(OUTPUT_LIFTER_MOTOR_REV_POS, OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE); 	// reverse on
        }
    else
        {
	delay_printk("%s - %s() - error\n",TARGET_NAME, __func__);
        }

    spin_unlock_irqrestore(&motor_lock, flags);

    // when not locking, signal an event
    if (direction == LIFTER_POSITION_UP) {
        generic_output_event(EVENT_RAISE); // start of raise
    } else if (direction == LIFTER_POSITION_DOWN) {
        generic_output_event(EVENT_LOWER); // start of lower
    }
    return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_motor_off(void)
    {
    unsigned long flags;
   delay_printk("%s - %s()\n",TARGET_NAME, __func__);
    spin_lock_irqsave(&motor_lock, flags);

    // These don't get used with the SAT but we set them everytime we get the chance for safety
    at91_set_gpio_value(OUTPUT_LIFTER_MOTOR_REV_NEG, !OUTPUT_LIFTER_MOTOR_NEG_ACTIVE_STATE);
    at91_set_gpio_value(OUTPUT_LIFTER_MOTOR_FWD_NEG, !OUTPUT_LIFTER_MOTOR_NEG_ACTIVE_STATE);

    at91_set_gpio_value(OUTPUT_LIFTER_MOTOR_FWD_POS, !OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE); // forward off
    at91_set_gpio_value(OUTPUT_LIFTER_MOTOR_REV_POS, !OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE); // reverse off

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
    if (!atomic_read(&full_init))
        {
        return;
        }

   delay_printk(KERN_ERR "%s - %s() - the operation has timed out.\n",TARGET_NAME, __func__);

    // Turn the motor off
    hardware_motor_off();

    // signal an event
    generic_output_event(EVENT_ERROR); // error

    // signal that the operation has finished
    atomic_set(&operating_atomic, FALSE);

    // notify user-space
    schedule_work(&position_work);
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
irqreturn_t down_position_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

    
    
	// We get an interrupt on both edges, so we have to check to which edge
    // we are responding.
    if (at91_get_gpio_value(INPUT_LIFTER_POS_DOWN_LIMIT) == INPUT_LIFTER_POS_ACTIVE_STATE)
        {
	   delay_printk("%s - %s()\n",TARGET_NAME, __func__);

        timeout_timer_stop();

        // Turn the motor off
        hardware_motor_off();

        // signal an event
        generic_output_event(EVENT_DOWN); // finished lowering

        // signal that the operation has finished
        atomic_set(&operating_atomic, FALSE);

        // notify user-space
        schedule_work(&position_work);
        }
    else
    	{
//	delay_printk("%s - %s() - Wrong edge!\n",TARGET_NAME, __func__);
    	}

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
irqreturn_t up_position_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

    
   
	// We get an interrupt on both edges, so we have to check to which edge
    // we are responding.
    if (at91_get_gpio_value(INPUT_LIFTER_POS_UP_LIMIT) == INPUT_LIFTER_POS_ACTIVE_STATE)
        {
	delay_printk("%s - %s()\n",TARGET_NAME, __func__);

        timeout_timer_stop();

        // Turn the motor off
        hardware_motor_off();

        // signal an event
        generic_output_event(EVENT_UP); // finished raising

        // signal that the operation has finished
    	atomic_set(&operating_atomic, FALSE);

        // notify user-space
        schedule_work(&position_work);
        }
    else
    	{
//	delay_printk("%s - %s() - Wrong edge!\n",TARGET_NAME, __func__);
    	}

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;

    // These don't get used with the SAT but we set the initial value for completeness
    at91_set_gpio_output(OUTPUT_LIFTER_MOTOR_REV_NEG, !OUTPUT_LIFTER_MOTOR_NEG_ACTIVE_STATE);
    at91_set_gpio_output(OUTPUT_LIFTER_MOTOR_FWD_NEG, !OUTPUT_LIFTER_MOTOR_NEG_ACTIVE_STATE);

    // Configure motor gpio for output and set initial output
    at91_set_gpio_output(OUTPUT_LIFTER_MOTOR_FWD_POS, !OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE); // forward off
    at91_set_gpio_output(OUTPUT_LIFTER_MOTOR_REV_POS, !OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE); // reverse off

    // Configure position gpios for input and deglitch for interrupts
    at91_set_gpio_input(INPUT_LIFTER_POS_DOWN_LIMIT, INPUT_LIFTER_POS_PULLUP_STATE);
    at91_set_deglitch(INPUT_LIFTER_POS_DOWN_LIMIT, INPUT_LIFTER_POS_DEGLITCH_STATE);
    at91_set_gpio_input(INPUT_LIFTER_POS_UP_LIMIT, INPUT_LIFTER_POS_PULLUP_STATE);
    at91_set_deglitch(INPUT_LIFTER_POS_UP_LIMIT, INPUT_LIFTER_POS_DEGLITCH_STATE);

    // Set up interrupts for position inputs
    status = request_irq(INPUT_LIFTER_POS_DOWN_LIMIT, (void*)down_position_int, 0, "armor_target_down", NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
           delay_printk(KERN_ERR "request_irq() failed - invalid irq number (0x%08X) or handler\n", INPUT_LIFTER_POS_DOWN_LIMIT);
            }
        else if (status == -EBUSY)
            {
           delay_printk(KERN_ERR "request_irq(): irq number (0x%08X) is busy, change your config\n", INPUT_LIFTER_POS_DOWN_LIMIT);
            }
        return status;
        }

    status = request_irq(INPUT_LIFTER_POS_UP_LIMIT, (void*)up_position_int, 0, "armor_target_up", NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
           delay_printk(KERN_ERR "request_irq() failed - invalid irq number (0x%08X) or handler\n", INPUT_LIFTER_POS_UP_LIMIT);
            }
        else if (status == -EBUSY)
            {
           delay_printk(KERN_ERR "request_irq(): irq number (0x%08X) is busy, change your config\n", INPUT_LIFTER_POS_UP_LIMIT);
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
    at91_set_gpio_output(OUTPUT_LIFTER_MOTOR_REV_NEG, !OUTPUT_LIFTER_MOTOR_NEG_ACTIVE_STATE);
    at91_set_gpio_output(OUTPUT_LIFTER_MOTOR_FWD_NEG, !OUTPUT_LIFTER_MOTOR_NEG_ACTIVE_STATE);

    at91_set_gpio_output(OUTPUT_LIFTER_MOTOR_FWD_POS, !OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE);
    at91_set_gpio_output(OUTPUT_LIFTER_MOTOR_REV_POS, !OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE);

    free_irq(INPUT_LIFTER_POS_DOWN_LIMIT, NULL);
    free_irq(INPUT_LIFTER_POS_UP_LIMIT, NULL);
    return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
int lifter_position_set(int position)
    {
    // signal that an operation is in progress
    atomic_set(&operating_atomic, TRUE);

    // turn the motor on to the correct position
    hardware_motor_on(position);

    timeout_timer_start();

    return 0;
    }
EXPORT_SYMBOL(lifter_position_set);

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
int lifter_position_get(void)
    {
    // check if an operation is in progress...
    if (atomic_read(&operating_atomic))
        {
        return LIFTER_POSITION_MOVING;
        }

    if (at91_get_gpio_value(INPUT_LIFTER_POS_DOWN_LIMIT) == INPUT_LIFTER_POS_ACTIVE_STATE)
        {
        return LIFTER_POSITION_DOWN;
        }

    if (at91_get_gpio_value(INPUT_LIFTER_POS_UP_LIMIT) == INPUT_LIFTER_POS_ACTIVE_STATE)
        {
        return LIFTER_POSITION_UP;
        }

    return LIFTER_POSITION_ERROR_NEITHER;
    }
EXPORT_SYMBOL(lifter_position_get);

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
    return sprintf(buf, "%s\n", lifter_position[lifter_position_get()]);
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
       delay_printk("%s - %s() : user command up\n",TARGET_NAME, __func__);
        status = size;
        // TODO - need to check error condition first
        if (lifter_position_get() != LIFTER_POSITION_UP)
            {
            lifter_position_set(LIFTER_POSITION_UP);
            }
        }
    else if (sysfs_streq(buf, "down"))
        {
       delay_printk("%s - %s() : user command down\n",TARGET_NAME, __func__);
        status = size;
        // TODO - need to check error condition first
        if (lifter_position_get() != LIFTER_POSITION_DOWN)
            {
            lifter_position_set(LIFTER_POSITION_DOWN);
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
// Defines the attributes of the armor target lifter for sysfs
//---------------------------------------------------------------------------
static const struct attribute * armor_lifter_attrs[] =
    {
    &dev_attr_type.attr,
    &dev_attr_position.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the armor target lifter for sysfs
//---------------------------------------------------------------------------
const struct attribute_group armor_lifter_attr_group =
    {
    .attrs = (struct attribute **) armor_lifter_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the armor target lifter
//---------------------------------------------------------------------------
const struct attribute_group * armor_lifter_get_attr_group(void)
    {
    return &armor_lifter_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_lifter_armor =
    {
    .type       = TARGET_TYPE_LIFTER,
    .name       = TARGET_NAME,
    .dev       = NULL,
    .get_attr_group = armor_lifter_get_attr_group,
    };

//---------------------------------------------------------------------------
// Message filler handler for expose functions
//---------------------------------------------------------------------------
int pos_mfh(struct sk_buff *skb, void *pos_data) {
    // the pos_data argument is a pre-made pos_event structure
    return nla_put_u8(skb, GEN_INT8_A_MSG, *((int*)pos_data));
}

//---------------------------------------------------------------------------
// Work item to notify the user-space about a position change or error
//---------------------------------------------------------------------------
static void position_change(struct work_struct * work)
    {
    int pos_data;
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    // notify netlink userspace
    switch (lifter_position_get()) { // map internal to external values
        case LIFTER_POSITION_DOWN: pos_data = CONCEAL; break;
        case LIFTER_POSITION_UP: pos_data = EXPOSE; break;
        case LIFTER_POSITION_MOVING: pos_data = LIFTING; break;
        default: pos_data = EXPOSURE_REQ; break; //error
    }
    send_nl_message_multi(&pos_data, pos_mfh, NL_C_EXPOSE);

    // notify sysfs userspace
    target_sysfs_notify(&target_device_lifter_armor, "position");
    }

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_lifter_armor_init(void)
    {
    int retval;
   delay_printk("%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
    INIT_WORK(&position_work, position_change);
    hardware_init();
    retval=target_sysfs_add(&target_device_lifter_armor);
    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
    return retval;
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_lifter_armor_exit(void)
    {
    atomic_set(&full_init, FALSE);
    ati_flush_work(&position_work); // close any open work queue items
    hardware_exit();
    target_sysfs_remove(&target_device_lifter_armor);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_lifter_armor_init);
module_exit(target_lifter_armor_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target lifter armor module");
MODULE_AUTHOR("jpy");

