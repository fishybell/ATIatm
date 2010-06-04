//---------------------------------------------------------------------------
// target_mover_infantry.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#include <linux/atmel_tc.h>
#include <linux/clk.h>


#include "target.h"
#include "target_mover_infantry.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"infantry mover"
#define MOVER_TYPE  	"infantry"

// TODO - replace with a table based on distance?
#define TIMEOUT_IN_SECONDS		5

#define MOVER_DIRECTION_STOP	0
#define MOVER_DIRECTION_FORWARD	1
#define MOVER_DIRECTION_REVERSE	2

#define MOVER_MOVEMENT_END   	0
#define MOVER_MOVEMENT_MOVING 	1
#define MOVER_MOVEMENT_STOPPED  2
#define MOVER_MOVEMENT_ERROR    3

#define PIN_MOVEMENT_ACTIVE   			0       		// Active low
#define PIN_MOVEMENT_END    			AT91_PIN_PA30   // BP3 on dev. board
#define PIN_MOVEMENT_EMERGENCY_STOP		AT91_PIN_PA31   // BP4 on dev. board

#define PIN_MOTOR_ACTIVE    	0       		// Active low

#ifdef DEV_BOARD_REVB
	#define PIN_MOTOR_CONTROL    	AT91_PIN_PA6
#else
	#define PIN_MOTOR_CONTROL    	AT91_PIN_PB8
#endif

// TODO - real hardware will require different pins
/*
 * #define PIN_MOVEMENT_DISTANCE
 * #define PIN_MOVEMENT_DOCKED
 * #define PIN_MOTOR_ENABLE
 * #define PIN_MOTOR_CONTROL_PWM
 *
 * #define PIN_MOTOR_CONTROL_FORWARD
 * #define PIN_MOTOR_CONTROL_REVERSE
 *
 * #define PIN_BRAKE_CONTROL
 */

#define MOTOR_PWM_BLOCK			1		// block 0 : TIOA0-2, TIOB0-2 , block 1 : TIOA3-5, TIOB3-5
#define MOTOR_PWM_CHANNEL		2		// channel 0 matches TIOA0 to TIOB0, same for 1 and 2

static struct atmel_tc * motor_pwm_tc;

//---------------------------------------------------------------------------
// This lock protects against motor control from simultaneously access from
// IRQs, timeout timer and user-space.
//---------------------------------------------------------------------------
static DEFINE_SPINLOCK(motor_lock);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that an operation is in progress,
// i.e. the mover has been commanded to move. It is used to synchronize
// user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t operating_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space of a movement
// change or error detected by the IRQs or the timeout timer.
//---------------------------------------------------------------------------
static struct work_struct movement_work;

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the timeout.
//---------------------------------------------------------------------------
static struct timer_list timeout_timer_list = TIMER_INITIALIZER(timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// Maps the movement to movement name.
//---------------------------------------------------------------------------
static const char * mover_movement[] =
    {
    "end",
    "moving",
    "stopped",
    "error"
    };

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_motor_on(int direction)
	{
	unsigned long flags;

    spin_lock_irqsave(&motor_lock, flags);

    if (direction == MOVER_DIRECTION_REVERSE)
		{
        // TODO - real infantry mover requires us to control multiple pins
		at91_set_gpio_value(PIN_MOTOR_CONTROL, PIN_MOTOR_ACTIVE); // Turn motor on
		}
    else if (direction == MOVER_DIRECTION_FORWARD)
		{
        // TODO - real infantry mover requires us to control multiple pins
    	at91_set_gpio_value(PIN_MOTOR_CONTROL, PIN_MOTOR_ACTIVE); // Turn motor on
		}

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

    // TODO - infantry mover requires us to control multiple pins
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
    schedule_work(&movement_work);
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
irqreturn_t end_movement_int(int irq, void *dev_id, struct pt_regs *regs)
    {
#ifdef TEST_PWM
    unsigned int start;
    if (!motor_pwm_tc) return IRQ_HANDLED;
    printk(KERN_ALERT "@: x%04x\n", __raw_readl(motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, CV)));
    if (at91_get_gpio_value(PIN_MOVEMENT_END) == PIN_MOVEMENT_ACTIVE)
        {
        start = __raw_readl(motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RA));
	if (start > 0x0002)
            {
            start -= 0x1000;
            printk(KERN_ALERT "moving back 0x1000 to 0x%04x\n", start);
            __raw_writel(start, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RA));
//            __raw_writel(start / 2, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RB));
            }
        }
    return IRQ_HANDLED;
#else
	// We get an interrupt on both edges, so we have to check to which edge
	// we are responding.
    if (at91_get_gpio_value(PIN_MOVEMENT_END) == PIN_MOVEMENT_ACTIVE)
        {
    	timeout_timer_stop();

    	printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

        // Turn the motor off
        hardware_motor_off();

        // signal that the operation has finished
        atomic_set(&operating_atomic, 0);

        // notify user-space
        schedule_work(&movement_work);
        }

    return IRQ_HANDLED;
#endif // TEST_PWM
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
irqreturn_t emergency_stop_movement_int(int irq, void *dev_id, struct pt_regs *regs)
    {
#ifdef TEST_PWM
    unsigned int start;
    if (!motor_pwm_tc) return IRQ_HANDLED;
    printk(KERN_ALERT "@: x%04x\n", __raw_readl(motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, CV)));
    if (at91_get_gpio_value(PIN_MOVEMENT_EMERGENCY_STOP) == PIN_MOVEMENT_ACTIVE)
        {
        start = __raw_readl(motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RA));
	if (start < 0x8002)
            {
            start += 0x1000;
            printk(KERN_ALERT "moving forward 0x1000 to 0x%04x\n", start);
            __raw_writel(start, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RA));
//            __raw_writel(start / 2, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RB));
            }
        }
    return IRQ_HANDLED;
#else
	// We get an interrupt on both edges, so we have to check to which edge
	// we are responding.
    if (at91_get_gpio_value(PIN_MOVEMENT_EMERGENCY_STOP) == PIN_MOVEMENT_ACTIVE)
        {
    	timeout_timer_stop();

    	printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

        // Turn the motor off
        hardware_motor_off();

        // signal that the operation has finished
    	atomic_set(&operating_atomic, 0);

        // notify user-space
        schedule_work(&movement_work);
        }

    return IRQ_HANDLED;
#endif // TEST_PWM
    }

//---------------------------------------------------------------------------
// Initialize the timer counter as PWM output for motor control
//---------------------------------------------------------------------------
static int hardware_motor_pwm_init(void)
    {
    // initialize timer counter
    motor_pwm_tc = target_timer_alloc(MOTOR_PWM_BLOCK, "gen_tc");
    printk(KERN_ALERT "timer_alloc(): %08x\n", (unsigned int) motor_pwm_tc);

    if (!motor_pwm_tc)
		{
    	return -EINVAL;
		}

    #if MOTOR_PWM_BLOCK == 0
        #if MOTOR_PWM_CHANNEL == 0
            at91_set_A_periph(AT91_PIN_PA26, 0);	// TIOA0
            at91_set_B_periph(AT91_PIN_PC9, 0);		// TIOB0
        #elif MOTOR_PWM_CHANNEL == 1
            at91_set_A_periph(AT91_PIN_PA27, 0);	// TIOA1
            at91_set_A_periph(AT91_PIN_PC7, 0);		// TIOB1
        #elif MOTOR_PWM_CHANNEL == 2
            at91_set_A_periph(AT91_PIN_PA28, 0);	// TIOA2
            at91_set_A_periph(AT91_PIN_PC6, 0);		// TIOB2
        #endif
    #else
        #if MOTOR_PWM_CHANNEL == 0
            at91_set_B_periph(AT91_PIN_PB0, 0);		// TIOA3
            at91_set_B_periph(AT91_PIN_PB1, 0);		// TIOB3
        #elif MOTOR_PWM_CHANNEL == 1
            at91_set_B_periph(AT91_PIN_PB2, 0);		// TIOA4
            at91_set_B_periph(AT91_PIN_PB18, 0);	// TIOB4
        #elif MOTOR_PWM_CHANNEL == 2
            at91_set_B_periph(AT91_PIN_PB3, 0);		// TIOA5
            at91_set_B_periph(AT91_PIN_PB19, 0);	// TIOB5
        #endif
    #endif

	clk_enable(motor_pwm_tc->clk[MOTOR_PWM_CHANNEL]);

	// initialize clock
	__raw_writel(ATMEL_TC_TIMER_CLOCK1				// Master clock / 2 : 66 mhz
					| ATMEL_TC_WAVE					// output mode
					| ATMEL_TC_ACPA_SET				// set TIOA high when counter reaches "A"
					| ATMEL_TC_ACPC_CLEAR			// set TIOA low when counter reaches "C"
					| ATMEL_TC_BCPB_SET				// set TIOB high when counter reaches "B"
					| ATMEL_TC_BCPC_CLEAR			// set TIOB low when counter reaches "C"
					| ATMEL_TC_EEVT_XC0				// set external clock 0 as the trigger
					| ATMEL_TC_WAVESEL_UP_AUTO,		// reset counter when counter reaches "C"
					motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, CMR));	// CMR register for timer 0

	__raw_writel(ATMEL_TC_CLKEN, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, CCR));
	__raw_writel(ATMEL_TC_SYNC, motor_pwm_tc->regs + ATMEL_TC_BCR);
	__raw_writel(0x0002, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RA));
	__raw_writel(0x7ff0, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RB));
	__raw_writel(0x8002, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RC));

	// disable irqs and start timer
	__raw_writel(0xff, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, IDR));				// irq register
	__raw_writel(ATMEL_TC_SWTRG, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, CCR));	// control register

	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;

    // TODO - real infantry mover requires us to control multiple pins
    // configure motor gpio for output and set initial output
    at91_set_gpio_output(PIN_MOTOR_CONTROL, !PIN_MOTOR_ACTIVE);

    // Configure movement gpios for input and deglitch for interrupts
    at91_set_gpio_input(PIN_MOVEMENT_END, 1);
    at91_set_deglitch(PIN_MOVEMENT_END, 1);
    at91_set_gpio_input(PIN_MOVEMENT_EMERGENCY_STOP, 1);
    at91_set_deglitch(PIN_MOVEMENT_EMERGENCY_STOP, 1);

    status = request_irq(PIN_MOVEMENT_END, (void*)end_movement_int, 0, "infantry_target_end", NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
            printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", PIN_MOVEMENT_END);
            }
        else if (status == -EBUSY)
            {
            printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", PIN_MOVEMENT_END);
            }
        return status;
        }

    status = request_irq(PIN_MOVEMENT_EMERGENCY_STOP, (void*)emergency_stop_movement_int, 0, "infantry_target_emergency_stop", NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
        	printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", PIN_MOVEMENT_EMERGENCY_STOP);
            }
        else if (status == -EBUSY)
            {
        	printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", PIN_MOVEMENT_EMERGENCY_STOP);
            }

        return status;
        }

    // setup motor pwm output
    status = hardware_motor_pwm_init();

    return status;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
	free_irq(PIN_MOVEMENT_END, NULL);
	free_irq(PIN_MOVEMENT_EMERGENCY_STOP, NULL);
	target_timer_free(motor_pwm_tc);
	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_movement_set(int movement)
    {
	if (movement == MOVER_DIRECTION_STOP)
		{
		// TODO - add some state checking
		timeout_timer_stop();

		// turn off the motor
		hardware_motor_off();

		// signal that an operation is done
		atomic_set(&operating_atomic, 0);

        // notify user-space
        schedule_work(&movement_work);
		}
	else
		{
		// signal that an operation is in progress
		atomic_set(&operating_atomic, 1);

		hardware_motor_on(movement);

		timeout_timer_start();
		}

	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_movement_get(void)
    {
    // check if an operation is in progress...
    if (atomic_read(&operating_atomic))
		{
		return MOVER_MOVEMENT_MOVING;
		}

    if (at91_get_gpio_value(PIN_MOVEMENT_EMERGENCY_STOP) == PIN_MOVEMENT_ACTIVE)
        {
        return MOVER_MOVEMENT_ERROR;
        }

    if (at91_get_gpio_value(PIN_MOVEMENT_END) == PIN_MOVEMENT_ACTIVE)
        {
        return MOVER_MOVEMENT_END;
        }

    return MOVER_MOVEMENT_STOPPED;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_speed_get(void)
    {
    // check if an operation is in progress...
    if (atomic_read(&operating_atomic))
		{
    	// TODO - return actual speed
		return 10;
		}

    return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_speed_set(int speed)
    {
    // check if an operation is in progress...
    if (atomic_read(&operating_atomic))
		{
		return 0;
		}

    // TODO - check for limits and set actual speed
    return speed;
    }

//---------------------------------------------------------------------------
// Handles reads to the type attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t type_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", MOVER_TYPE);
    }

//---------------------------------------------------------------------------
// Handles reads to the movement attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t movement_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", mover_movement[hardware_movement_get()]);
    }

//---------------------------------------------------------------------------
// Handles writes to the movement attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t movement_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    ssize_t status;
    int current_movement;

    // check if an operation is in progress, if so ignore any command
    if (atomic_read(&operating_atomic))
		{
    	status = size;
		}
    else if (sysfs_streq(buf, "stop"))
		{
		printk(KERN_ALERT "%s - %s() : user command stop\n",TARGET_NAME, __func__);
		status = size;

		// TODO - keep track of which end we might be at...
		current_movement = hardware_movement_get();
		if ((current_movement != MOVER_MOVEMENT_MOVING) &&
			(current_movement != MOVER_MOVEMENT_ERROR))
			{
			hardware_movement_set(MOVER_DIRECTION_STOP);
			}
		}
    else if (sysfs_streq(buf, "forward"))
        {
    	printk(KERN_ALERT "%s - %s() : user command forward\n",TARGET_NAME, __func__);
        status = size;

        // TODO - keep track of which end we might be at...
        current_movement = hardware_movement_get();
        if ((current_movement != MOVER_MOVEMENT_MOVING) &&
        	(current_movement != MOVER_MOVEMENT_ERROR))
            {
        	hardware_movement_set(MOVER_DIRECTION_FORWARD);
            }
        }
    else if (sysfs_streq(buf, "reverse"))
        {
    	printk(KERN_ALERT "%s - %s() : user command reverse\n",TARGET_NAME, __func__);
    	status = size;

        // TODO - keep track of which end we might be at...
        current_movement = hardware_movement_get();
        if ((current_movement != MOVER_MOVEMENT_MOVING) &&
        	(current_movement != MOVER_MOVEMENT_ERROR))
            {
        	hardware_movement_set(MOVER_DIRECTION_REVERSE);
            }
        }
    else
		{
    	status = size;
		}

    return status;
    }

//---------------------------------------------------------------------------
// Handles reads to the speed attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t speed_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	return sprintf(buf, "%d\n", hardware_speed_get());
    }

//---------------------------------------------------------------------------
// Handles writes to the speed attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t speed_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
	long value;
    ssize_t status;

	status = strict_strtol(buf, 0, &value);
	if (status == 0)
		{
		hardware_speed_set(value);
		status = size;
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
static DEVICE_ATTR(movement, 0644, movement_show, movement_store);
static DEVICE_ATTR(speed, 0644, speed_show, speed_store);

//---------------------------------------------------------------------------
// Defines the attributes of the infantry target mover for sysfs
//---------------------------------------------------------------------------
static const struct attribute * infantry_mover_attrs[] =
    {
    &dev_attr_type.attr,
    &dev_attr_movement.attr,
    &dev_attr_speed.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the infantry target mover for sysfs
//---------------------------------------------------------------------------
const struct attribute_group infantry_mover_attr_group =
    {
    .attrs = (struct attribute **) infantry_mover_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the infantry target mover
//---------------------------------------------------------------------------
const struct attribute_group * infantry_mover_get_attr_group(void)
    {
    return &infantry_mover_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_mover_infantry =
    {
    .type     		= TARGET_TYPE_MOVER,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= infantry_mover_get_attr_group,
    };

//---------------------------------------------------------------------------
// Work item to notify the user-space about a movement change or error
//---------------------------------------------------------------------------
static void movement_change(struct work_struct * work)
	{
	target_sysfs_notify(&target_device_mover_infantry, "movement");
	}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_mover_infantry_init(void)
    {
	printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
	hardware_init();
	INIT_WORK(&movement_work, movement_change);
    return target_sysfs_add(&target_device_mover_infantry);
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_mover_infantry_exit(void)
    {
	hardware_exit();
    target_sysfs_remove(&target_device_mover_infantry);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_mover_infantry_init);
module_exit(target_mover_infantry_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target mover infantry module");
MODULE_AUTHOR("jpy");

