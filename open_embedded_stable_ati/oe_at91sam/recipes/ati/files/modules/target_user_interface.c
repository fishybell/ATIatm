//---------------------------------------------------------------------------
// target_user_interface.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "netlink_kernel.h"
#include "target_user_interface.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"user interface"

#define BIT_DOUBLE_IN_MSECONDS		2000
#define BIT_READ_IN_MSECONDS		50
#define BIT_CANCEL_IN_MSECONDS		5000
#define MOVE_WAIT_IN_MSECONDS		2000

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

// for moving a mover
#define FORWARD_BUTTON	0
#define REVERSE_BUTTON	1
#define TEST_BUTTON	2
#define MOVING_FWD	0
#define MOVING_REV	1
#define MOVING_STOP	2

#define TESTING_ON_EVAL
#ifdef TESTING_ON_EVAL
	#undef INPUT_TEST_BUTTON_ACTIVE_STATE
	#undef INPUT_TEST_BUTTON_PULLUP_STATE
	#undef INPUT_TEST_BUTTON_DEGLITCH_STATE
	#undef INPUT_TEST_BUTTON

        #undef INPUT_MOVER_TEST_BUTTON_ACTIVE_STATE
        #undef INPUT_MOVER_TEST_BUTTON_PULLUP_STATE
        #undef INPUT_MOVER_TEST_BUTTON_DEGLITCH_STATE
        #undef INPUT_MOVER_TEST_BUTTON_FWD
        #undef INPUT_MOVER_TEST_BUTTON_REV

	#undef OUTPUT_TEST_INDICATOR_ACTIVE_STATE
	#undef OUTPUT_TEST_INDICATOR

	#define	INPUT_TEST_BUTTON_ACTIVE_STATE		ACTIVE_LOW
	#define INPUT_TEST_BUTTON_PULLUP_STATE		PULLUP_ON
	#define INPUT_TEST_BUTTON_DEGLITCH_STATE	DEGLITCH_ON
	#define	INPUT_TEST_BUTTON 					AT91_PIN_PB30 //A31 is BP4 on dev. board

        #define INPUT_MOVER_TEST_BUTTON_ACTIVE_STATE            ACTIVE_LOW
        #define INPUT_MOVER_TEST_BUTTON_PULLUP_STATE            PULLUP_ON
        #define INPUT_MOVER_TEST_BUTTON_DEGLITCH_STATE          DEGLITCH_ON
        #define INPUT_MOVER_TEST_BUTTON_FWD                     AT91_PIN_PA30
        #define INPUT_MOVER_TEST_BUTTON_REV                     AT91_PIN_PA31

	#define	OUTPUT_TEST_INDICATOR_ACTIVE_STATE	ACTIVE_LOW
	#define	OUTPUT_TEST_INDICATOR 				AT91_PIN_PB8	// LED on dev. board

#endif // TESTING_ON_EVAL

//---------------------------------------------------------------------------
// This variable is a parameter that can be set to enable/disable move_button
//---------------------------------------------------------------------------
static int mover = FALSE;
module_param(mover, bool, S_IRUGO);

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
// This atomic variable is use to indicate that an the bit button has been
// pressed. It gets cleared when user-space reads
//---------------------------------------------------------------------------
atomic_t move_button_atomic = ATOMIC_INIT(MOVING_STOP);

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
// Declaration of the function that gets called when the move timeout fires.
//---------------------------------------------------------------------------
static void move_timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the bit timeout.
//---------------------------------------------------------------------------
static struct timer_list bit_timeout_timer_list = TIMER_INITIALIZER(bit_timeout_fire, 0, 0);
static struct timer_list move_timeout_timer_list = TIMER_INITIALIZER(move_timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space that the bit
// button has been pressed.
//---------------------------------------------------------------------------
static struct work_struct bit_button_work;
static struct work_struct move_button_work;

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
static const char * move_status[] =
    {
    "forward",
    "reverse",
    "stop"
    };

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
// handles movement button interrupts
//---------------------------------------------------------------------------
static void move_button_int(int button)
    {
    //int bit_value, fwd_value, rev_value;
    int fwd_value, rev_value;
    if (!atomic_read(&full_init))
        {
        return;
        }

    // if we're not a mover, don't bother
    if (!mover)
        {
        return;
        }

    delay_printk("%s - %s(%i)\n",TARGET_NAME, __func__, button);

    // look at all three values
    //bit_value = (at91_get_gpio_value(INPUT_TEST_BUTTON) == INPUT_TEST_BUTTON_ACTIVE_STATE);
    fwd_value = (at91_get_gpio_value(INPUT_MOVER_TEST_BUTTON_FWD) == INPUT_MOVER_TEST_BUTTON_ACTIVE_STATE);
    rev_value = (at91_get_gpio_value(INPUT_MOVER_TEST_BUTTON_REV) == INPUT_MOVER_TEST_BUTTON_ACTIVE_STATE);

    // cancel timer
    del_timer(&move_timeout_timer_list);

    // check failure values (a button went from on to off)
    //if ((!bit_value) ||
    if ((button == FORWARD_BUTTON && !fwd_value) ||
        (button == REVERSE_BUTTON && !rev_value))
        {

    //delay_printk("%s - %s fail: (!%i) || (%i == %i && !%i) || (%i == %i && !%i)\n",TARGET_NAME, __func__, bit_value,button,FORWARD_BUTTON,fwd_value,button,REVERSE_BUTTON,rev_value);
   delay_printk("%s - %s fail: (%i == %i && !%i) || (%i == %i && !%i)\n",TARGET_NAME, __func__, button,FORWARD_BUTTON,fwd_value,button,REVERSE_BUTTON,rev_value);
        // were we moving?
        if (atomic_read(&move_button_atomic) != MOVING_STOP)
            {
            // stop
            atomic_set(&move_button_atomic, MOVING_STOP);

            // notify user-space
            schedule_work(&move_button_work);
            }
        return;
        }

    // if a move button was pressed, check again later to see if it's still pressed
    if (button != TEST_BUTTON)
        {
        // start timer
        mod_timer(&move_timeout_timer_list, jiffies+(MOVE_WAIT_IN_MSECONDS*HZ/1000));
        }

    }
irqreturn_t fwd_button_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    move_button_int(FORWARD_BUTTON);
    return IRQ_HANDLED;
    }
irqreturn_t rev_button_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    move_button_int(REVERSE_BUTTON);
    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// The function that gets called when the move timeout fires.
//---------------------------------------------------------------------------
static void move_timeout_fire(unsigned long data)
    {
    int bit_value, fwd_value, rev_value;
    // if we're not a mover, don't bother
   delay_printk("%s - %s\n",TARGET_NAME, __func__);
    if (!mover)
        {
        return;
        }

    // we've waited several seconds, and we havne't been canceled, check which way we're moving
    //bit_value = (at91_get_gpio_value(INPUT_TEST_BUTTON) == INPUT_TEST_BUTTON_ACTIVE_STATE);
    fwd_value = (at91_get_gpio_value(INPUT_MOVER_TEST_BUTTON_FWD) == INPUT_MOVER_TEST_BUTTON_ACTIVE_STATE);
    rev_value = (at91_get_gpio_value(INPUT_MOVER_TEST_BUTTON_REV) == INPUT_MOVER_TEST_BUTTON_ACTIVE_STATE);

    // check for an error state
    /*if (!bit_value)
        {
       delay_printk("%s - %s - NONE\n",TARGET_NAME, __func__);
        move_button_int(TEST_BUTTON);
        return;
        }*/

    // check which way we're going
    if (fwd_value && !rev_value)
        {
        // forward
       delay_printk("%s - %s - FORWARD\n",TARGET_NAME, __func__);
        atomic_set(&move_button_atomic, MOVING_FWD);
        }
    else if (!fwd_value && rev_value)
        {
        // reverse
       delay_printk("%s - %s - REVERSE\n",TARGET_NAME, __func__);
        atomic_set(&move_button_atomic, MOVING_REV);
        }
    else
        {
        // error
       delay_printk("%s - %s - STOP\n",TARGET_NAME, __func__);
        move_button_int(TEST_BUTTON);
        return;
        }

    // notify user-space
    schedule_work(&move_button_work);
    }
//---------------------------------------------------------------------------
// The function that gets called when the bit timeout fires.
//---------------------------------------------------------------------------
static void bit_timeout_fire(unsigned long data)
    {
   delay_printk("%s - %s\n",TARGET_NAME, __func__);
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
   delay_printk("%s - %s\n",TARGET_NAME, __func__);
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
    delay_printk("%s - %s(%i)\n",TARGET_NAME, __func__, 0);

    // tell the movement code that the test button has changed
    //move_button_int(TEST_BUTTON);

    // check if the button has been pressed but not read by user-space
    value = atomic_read(&bit_button_atomic);
    delay_printk("%s - %s(%i)\n",TARGET_NAME, __func__, value);
    if (value == BUTTON_STATUS_PRESSED)
	{
	return IRQ_HANDLED;
	}

    // handle the actual read after a short timeout to let the button settle
    del_timer(&bit_read_timeout_timer_list);
    mod_timer(&bit_read_timeout_timer_list, jiffies+(BIT_READ_IN_MSECONDS*HZ/1000));

    delay_printk("%s - %s(%i)\n",TARGET_NAME, __func__, 3);
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

   delay_printk("%s - %s()\n",TARGET_NAME, __func__);

    // We get an interrupt on both edges, so we have to check to which edge
    //  we are responding.
    if (at91_get_gpio_value(INPUT_TEST_BUTTON) == INPUT_TEST_BUTTON_ACTIVE_STATE)
        {
       delay_printk("%s - ACTIVE\n",TARGET_NAME);
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
       delay_printk("%s - INACTIVE\n",TARGET_NAME);
        if (value == BUTTON_STATUS_UNCLEAR)
            {
            // signal that the wake-up button has been pressed
            atomic_set(&bit_button_atomic, ++value);

            // schedule a timeout in case we don't double-click
            mod_timer(&bit_timeout_timer_list, jiffies+(BIT_DOUBLE_IN_MSECONDS*HZ/1000));
            }
        }

   delay_printk("%s - %i\n",TARGET_NAME, value);
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;
   delay_printk("%s mover: %i\n",__func__, mover);

    // configure bit status gpio for output and set initial output
    at91_set_gpio_output(OUTPUT_TEST_INDICATOR, !OUTPUT_TEST_INDICATOR_ACTIVE_STATE);

    // Configure button gpios for input and deglitch for interrupts
    at91_set_gpio_input(INPUT_TEST_BUTTON, INPUT_TEST_BUTTON_PULLUP_STATE);
    at91_set_deglitch(INPUT_TEST_BUTTON, INPUT_TEST_BUTTON_DEGLITCH_STATE);

    // if we're not a mover, don't bother
    if (mover)
        {
        at91_set_gpio_input(INPUT_MOVER_TEST_BUTTON_FWD, INPUT_MOVER_TEST_BUTTON_PULLUP_STATE);
        at91_set_deglitch(INPUT_MOVER_TEST_BUTTON_FWD, INPUT_MOVER_TEST_BUTTON_DEGLITCH_STATE);
        at91_set_gpio_input(INPUT_MOVER_TEST_BUTTON_REV, INPUT_MOVER_TEST_BUTTON_PULLUP_STATE);
        at91_set_deglitch(INPUT_MOVER_TEST_BUTTON_REV, INPUT_MOVER_TEST_BUTTON_DEGLITCH_STATE);
        }

    status = request_irq(INPUT_TEST_BUTTON, (void*)bit_button_int, 0, "user_interface_bit_button", NULL);
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

    // if we're not a mover, don't bother
    if (mover)
        {
        status = request_irq(INPUT_MOVER_TEST_BUTTON_FWD, (void*)fwd_button_int, 0, "user_interface_bit_button", NULL);
        if (status != 0)
            {
            if (status == -EINVAL)
                {
               delay_printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_MOVER_TEST_BUTTON_FWD);
                }
            else if (status == -EBUSY)
                {
            delay_printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_MOVER_TEST_BUTTON_FWD);
                }

            return status;
            }
        status = request_irq(INPUT_MOVER_TEST_BUTTON_REV, (void*)rev_button_int, 0, "user_interface_bit_button", NULL);
        if (status != 0)
            {
            if (status == -EINVAL)
                {
            delay_printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_MOVER_TEST_BUTTON_REV);
                }
            else if (status == -EBUSY)
                {
            delay_printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_MOVER_TEST_BUTTON_REV);
                }

            return status;
            }
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
    // if we're not a mover, don't bother
    if (mover)
        {
        del_timer(&move_timeout_timer_list);
        free_irq(INPUT_MOVER_TEST_BUTTON_FWD, NULL);
        free_irq(INPUT_MOVER_TEST_BUTTON_REV, NULL);
        }
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
// Handles reads to the move_button attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t move_button_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    int status;
    status = atomic_read(&move_button_atomic);
    return sprintf(buf, "%s\n", move_status[status]);
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
    delay_printk("%s - %s() : user command on\n",TARGET_NAME, __func__);
    	del_timer(&blink_timeout_timer_list);
        hardware_bit_status_on();
        }
    else if (sysfs_streq(buf, "off"))
        {
    delay_printk("%s - %s() : user command off\n",TARGET_NAME, __func__);
    	del_timer(&blink_timeout_timer_list);
        hardware_bit_status_off();
        }
    else if (sysfs_streq(buf, "blink"))
		{
	delay_printk("%s - %s() : user command blink\n",TARGET_NAME, __func__);
		mod_timer(&blink_timeout_timer_list, jiffies+(100*HZ/1000));
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
static DEVICE_ATTR(bit_button, 0444, bit_button_show, NULL);
static DEVICE_ATTR(move_button, 0444, move_button_show, NULL);
static DEVICE_ATTR(bit_status, 0644, bit_status_show, bit_status_store);

//---------------------------------------------------------------------------
// Defines the attributes of the target user interface for sysfs
//---------------------------------------------------------------------------
static const struct attribute * user_interface_attrs[] =
    {
    &dev_attr_bit_button.attr,
    &dev_attr_move_button.attr,
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
// Message filler handler for bit functions
//---------------------------------------------------------------------------
int bit_mfh(struct sk_buff *skb, void *bit_data) {
    // the bit_data argument is a pre-made bit_event structure
    return nla_put(skb, BIT_A_MSG, sizeof(struct bit_event), (struct bit_event*)bit_data);
}

//---------------------------------------------------------------------------
// Work item to notify the user-space about a bit button press
//---------------------------------------------------------------------------
static void bit_pressed(struct work_struct * work)
	{
    // notify netlink userspace
    struct bit_event bit_data;
    bit_data.bit_type = BIT_TEST;
    bit_data.is_on = atomic_read(&bit_button_atomic) == BUTTON_STATUS_PRESSED;
    send_nl_message_multi(&bit_data, bit_mfh, NL_C_BIT);

    // notify sysfs userspace
	target_sysfs_notify(&target_device_user_interface, "bit_button");
	}

//---------------------------------------------------------------------------
// Work item to notify the user-space about a move button press
//---------------------------------------------------------------------------
static void move_pressed(struct work_struct * work)
	{
    // notify netlink userspace
    struct bit_event bit_data;
    switch (atomic_read(&move_button_atomic)) {
        case MOVING_FWD: bit_data.bit_type = BIT_MOVE_FWD; break;
        case MOVING_REV: bit_data.bit_type = BIT_MOVE_REV; break;
        default: /* any other value = stop */
        case MOVING_STOP: bit_data.bit_type = BIT_MOVE_STOP; break;
    }
    bit_data.is_on = 1; // move event is always on
    send_nl_message_multi(&bit_data, bit_mfh, NL_C_BIT);

    // notify sysfs userspace
	target_sysfs_notify(&target_device_user_interface, "move_button");
	}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_user_interface_init(void)
    {
    int retval;
delay_printk("%s(): %s - %s\n",__func__,  __DATE__, __TIME__);

	hardware_init();

	INIT_WORK(&bit_button_work, bit_pressed);
	INIT_WORK(&move_button_work, move_pressed);

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

