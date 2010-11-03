//---------------------------------------------------------------------------
// target_mover_infantry.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#include <linux/atmel_tc.h>
#include <linux/clk.h>
#include <linux/atmel_pwm.h>

#include "target.h"
#include "target_hardware.h"
#include "target_mover_infantry.h"

//---------------------------------------------------------------------------
#define TARGET_NAME		"infantry mover"
#define MOVER_TYPE  	"infantry"

// TODO - replace with a table based on distance and speed?
#define TIMEOUT_IN_SECONDS		120

#define MOVER_POSITION_START 		0
#define MOVER_POSITION_BETWEEN		1	// not at start or end
#define MOVER_POSITION_END			2

#define MOVER_DIRECTION_STOP		0
#define MOVER_DIRECTION_FORWARD		1
#define MOVER_DIRECTION_REVERSE		2

#define MOVER_MOVEMENT_STOPPED  		0
#define MOVER_MOVEMENT_MOVING_FORWARD	1
#define MOVER_MOVEMENT_MOVING_REVERSE	2
#define MOVER_MOVEMENT_STOPPED_FAULT	3

// the maximum allowed speed ticks 
#define NUMBER_OF_SPEEDS	20

// These map directly to the FASIT faults for movers
#define FAULT_NORMAL                                       0
#define FAULT_BOTH_LEFT_AND_RIGHT_LIMITS_ARE_ACTIVE        1
#define FAULT_INVALID_DIRECTION_REQUESTED                  2
#define FAULT_INVALID_SPEED_REQUESTED                      3
#define FAULT_SPEED_0_REQUESTED                            4
#define FAULT_STOPPED_AT_RIGHT_LIMIT                       5
#define FAULT_STOPPED_AT_LEFT_LIMIT                        6
#define FAULT_STOPPED_BY_DISTANCE_ENCODER                  7
#define FAULT_EMERGENCY_STOP                               8
#define FAULT_NO_MOVEMENT_DETECTED                         9
#define FAULT_OVER_SPEED_DETECTED                          10
#define FAULT_UNASSIGNED                                   11
#define FAULT_WRONG_DIRECTION_DETECTED                     12
#define FAULT_STOPPED_DUE_TO_STOP_COMMAND                  13

// RC - 600 khz
// RA - 50 duty of 600khz - cannot exceed RC
// RB - 50 duty of 600khz - cannot exceed RC
#define MOTOR_PWM_RC			0x001C
#define MOTOR_PWM_RA_DEFAULT	0x000E
#define MOTOR_PWM_RB_DEFAULT	0x000E

// TODO - map pwm output pin to block/channel
#define MOTOR_PWM_BLOCK			0		// block 0 : TIOA0-2, TIOB0-2 , block 1 : TIOA3-5, TIOB3-5
#define MOTOR_PWM_CHANNEL		2		// channel 0 matches TIOA0 to TIOB0, same for 1 and 2

// The external motor control for the infantry is the opposite of the armor
#if OUTPUT_MOVER_PWM_SPEED_ACTIVE_STATE == ACTIVE_LOW
#undef OUTPUT_MOVER_PWM_SPEED_ACTIVE_STATE
#define OUTPUT_MOVER_PWM_SPEED_ACTIVE_STATE ACTIVE_HIGH
#else
#undef OUTPUT_MOVER_PWM_SPEED_ACTIVE_STATE
#define OUTPUT_MOVER_PWM_SPEED_ACTIVE_STATE ACTIVE_LOW
#endif

static struct atmel_tc * motor_pwm_tc = NULL;


//---------------------------------------------------------------------------
// This variable is a parameter that can be set at module loading to reverse
// the meanings of the 'end of track' sensors and 'forward' and 'reverse'
//---------------------------------------------------------------------------
static int reverse = FALSE;
module_param(reverse, bool, S_IRUGO);

// map home and 'end of track' sensors based on the 'reverse' parameter
#define INPUT_MOVER_TRACK_HOME	(reverse ? INPUT_MOVER_END_OF_TRACK_2 : INPUT_MOVER_END_OF_TRACK_1)
#define INPUT_MOVER_TRACK_END	(reverse ? INPUT_MOVER_END_OF_TRACK_1 : INPUT_MOVER_END_OF_TRACK_2)

// map motor controller reverse and forward signals based on the 'reverse' parameter
#define OUTPUT_MOVER_FORWARD	(reverse ? OUTPUT_MOVER_DIRECTION_REVERSE : OUTPUT_MOVER_DIRECTION_FORWARD)
#define OUTPUT_MOVER_REVERSE	(reverse ? OUTPUT_MOVER_DIRECTION_FORWARD : OUTPUT_MOVER_DIRECTION_REVERSE)

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
atomic_t moving_atomic = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This atomic variable is to store the current movement,
// It is used to synchronize user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t movement_atomic = ATOMIC_INIT(MOVER_MOVEMENT_STOPPED);

//---------------------------------------------------------------------------
// This atomic variable is to store the last end limit hit
//---------------------------------------------------------------------------
atomic_t last_limit_atomic = ATOMIC_INIT(MOVER_MOVEMENT_STOPPED);

//---------------------------------------------------------------------------
// This atomic variable is to store the fault code.
//---------------------------------------------------------------------------
atomic_t fault_atomic = ATOMIC_INIT(FAULT_NORMAL);

//---------------------------------------------------------------------------
// This atomic variable is to store the speed setting.
//---------------------------------------------------------------------------
atomic_t speed_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This atomic variable is used to store which track sensor (front or rear)
// was last triggered. It is used to determine the actual direction of the
// mover. Note: Should be initialized in hardware_init() or at least after
// the 'reverse' parameter has been set.
//---------------------------------------------------------------------------
atomic_t last_track_sensor_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space of a movement
// change or error.
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
/*
static const char * mover_movement[] =
    {
    "home",
    "end",
    "forward",
    "reverse",
    "stopped",
    "emergency",
    "timeout"
    };
*/
static const char * mover_movement[] =
    {
    "stopped",
    "forward",
    "reverse",
    "fault"
    };

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
//
//---------------------------------------------------------------------------
static int hardware_motor_on(int direction)
	{
	unsigned long flags;

    spin_lock_irqsave(&motor_lock, flags);

    // turn on directional lines
    if (direction == MOVER_DIRECTION_REVERSE)
		{
		// reverse on, forward off
		at91_set_gpio_output(OUTPUT_MOVER_DIRECTION_REVERSE, OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
		at91_set_gpio_output(OUTPUT_MOVER_DIRECTION_FORWARD, !OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);

	    atomic_set(&movement_atomic, MOVER_MOVEMENT_MOVING_REVERSE);
	    printk(KERN_ALERT "%s - %s() - reverse\n",TARGET_NAME, __func__);
		}
    else if (direction == MOVER_DIRECTION_FORWARD)
		{
		// reverse off, forward on
		at91_set_gpio_output(OUTPUT_MOVER_DIRECTION_REVERSE, !OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
		at91_set_gpio_output(OUTPUT_MOVER_DIRECTION_FORWARD, OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);

    	atomic_set(&movement_atomic, MOVER_MOVEMENT_MOVING_FORWARD);
    	printk(KERN_ALERT "%s - %s() - forward\n",TARGET_NAME, __func__);
		}
    else
    	{
		printk(KERN_ALERT "%s - %s() - error\n",TARGET_NAME, __func__);
    	}

    // assert pwm line
    #if MOTOR_PWM_BLOCK == 0
        at91_set_A_periph(OUTPUT_MOVER_PWM_SPEED_THROTTLE, PULLUP_OFF);
    #else
        at91_set_B_periph(OUTPUT_MOVER_PWM_SPEED_THROTTLE, PULLUP_OFF);
    #endif

    // turn off brake
    at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, !OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);

	spin_unlock_irqrestore(&motor_lock, flags);
	return 0;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_motor_off(void)
	{
	unsigned long flags;

	printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

	spin_lock_irqsave(&motor_lock, flags);

	// de-assert the pwm line
	at91_set_gpio_output(OUTPUT_MOVER_PWM_SPEED_THROTTLE, !OUTPUT_MOVER_PWM_SPEED_ACTIVE_STATE);

	// turn on brake
	at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);

	// turn off both directional lines
	at91_set_gpio_output(OUTPUT_MOVER_DIRECTION_REVERSE, !OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
	at91_set_gpio_output(OUTPUT_MOVER_DIRECTION_FORWARD, !OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);

	atomic_set(&movement_atomic, MOVER_MOVEMENT_STOPPED);
	
	spin_unlock_irqrestore(&motor_lock, flags);
	return 0;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_movement_stop(int stop_timer)
    {
	if (stop_timer == TRUE)
		{
		timeout_timer_stop();
		}

	// turn off the motor
	hardware_motor_off();

	// signal that an operation is done
	atomic_set(&moving_atomic, FALSE);

	// notify user-space
	schedule_work(&movement_work);

	return 0;
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

	printk(KERN_ERR "%s - %s() - the operation has timed out.\n",TARGET_NAME, __func__);
	hardware_movement_stop(FALSE);
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
irqreturn_t track_sensor_home_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

    printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

    // check to see if this one needs to be ignored
    if (atomic_read(&last_limit_atomic) == MOVER_DIRECTION_REVERSE &&
        atomic_read(&movement_atomic) == MOVER_DIRECTION_FORWARD)
        {
        // ignore
        return IRQ_HANDLED;
        }

    atomic_set(&last_limit_atomic, MOVER_DIRECTION_REVERSE);
    hardware_movement_stop(TRUE);

	return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
irqreturn_t track_sensor_end_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

    printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

    // check to see if this one needs to be ignored
    if (atomic_read(&last_limit_atomic) == MOVER_DIRECTION_FORWARD &&
        atomic_read(&movement_atomic) == MOVER_DIRECTION_REVERSE)
        {
        // ignore
        return IRQ_HANDLED;
        }

    atomic_set(&last_limit_atomic, MOVER_DIRECTION_FORWARD);
    hardware_movement_stop(TRUE);

	return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// Initialize the timer counter as PWM output for motor control
//---------------------------------------------------------------------------
static int hardware_motor_pwm_init(void)
    {
	unsigned int mck_freq;

    // initialize timer counter
    motor_pwm_tc = target_timer_alloc(MOTOR_PWM_BLOCK, "gen_tc");
    printk(KERN_ALERT "timer_alloc(): %08x\n", (unsigned int) motor_pwm_tc);

    mck_freq = clk_get_rate(motor_pwm_tc->clk[MOTOR_PWM_CHANNEL]);
    printk(KERN_ALERT "mck_freq: %i\n", (unsigned int) mck_freq);

    if (!motor_pwm_tc)
		{
    	return -EINVAL;
		}

/*
    #if MOTOR_PWM_BLOCK == 0
        #if MOTOR_PWM_CHANNEL == 0
            at91_set_A_periph(AT91_PIN_PA26, PULLUP_OFF);	// TIOA0
            at91_set_B_periph(AT91_PIN_PC9, PULLUP_OFF);		// TIOB0
        #elif MOTOR_PWM_CHANNEL == 1
            at91_set_A_periph(AT91_PIN_PA27, PULLUP_OFF);	// TIOA1
            at91_set_A_periph(AT91_PIN_PC7, PULLUP_OFF);		// TIOB1
        #elif MOTOR_PWM_CHANNEL == 2
            at91_set_A_periph(AT91_PIN_PA28, PULLUP_OFF);	// TIOA2
            at91_set_A_periph(AT91_PIN_PC6, PULLUP_OFF);		// TIOB2
        #endif
    #else
        #if MOTOR_PWM_CHANNEL == 0
            at91_set_B_periph(AT91_PIN_PB0, PULLUP_OFF);		// TIOA3
            at91_set_B_periph(AT91_PIN_PB1, PULLUP_OFF);		// TIOB3
        #elif MOTOR_PWM_CHANNEL == 1
            at91_set_B_periph(AT91_PIN_PB2, PULLUP_OFF);		// TIOA4
            at91_set_B_periph(AT91_PIN_PB18, PULLUP_OFF);		// TIOB4
        #elif MOTOR_PWM_CHANNEL == 2
            at91_set_B_periph(AT91_PIN_PB3, PULLUP_OFF);		// TIOA5
            at91_set_B_periph(AT91_PIN_PB19, PULLUP_OFF);		// TIOB5
        #endif
    #endif
*/

	clk_enable(motor_pwm_tc->clk[MOTOR_PWM_CHANNEL]);

	// ~ 1 khz 50% duty

	// initialize clock
	__raw_writel(ATMEL_TC_TIMER_CLOCK2				// Master clock/4 = 132MHz/4 = 33MHz ?
					| ATMEL_TC_WAVE					// output mode
					| ATMEL_TC_ACPA_CLEAR				// set TIOA low when counter reaches "A"
					| ATMEL_TC_ACPC_SET			// set TIOA high when counter reaches "C"
					| ATMEL_TC_BCPB_CLEAR				// set TIOB low when counter reaches "B"
					| ATMEL_TC_BCPC_SET			// set TIOB high when counter reaches "C"
					| ATMEL_TC_EEVT_XC0				// set external clock 0 as the trigger
					| ATMEL_TC_WAVESEL_UP_AUTO,		// reset counter when counter reaches "C"
					motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, CMR));	// CMR register for timer 0

	__raw_writel(ATMEL_TC_CLKEN, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, CCR));
	__raw_writel(ATMEL_TC_SYNC, motor_pwm_tc->regs + ATMEL_TC_BCR);

	// These set up the freq and duty cycle
	__raw_writel(MOTOR_PWM_RA_DEFAULT, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RA)); // 0x203A 50%
	__raw_writel(MOTOR_PWM_RB_DEFAULT, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RB));
	__raw_writel(MOTOR_PWM_RC, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RC));

	// disable irqs and start timer
	__raw_writel(0xff, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, IDR));				// irq register
	__raw_writel(ATMEL_TC_SWTRG, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, CCR));	// control register

	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_set_gpio_input_irq(	int pin_number,
										int pullup_state,
										irqreturn_t (*handler)(int, void *, struct pt_regs *),
										const char * dev_name)
	{
	int status = 0;

    // Configure position gpios for input and deglitch for interrupts
    at91_set_gpio_input(pin_number, pullup_state);
    at91_set_deglitch(pin_number, TRUE);

    // Set up interrupt
    status = request_irq(pin_number, (void*)handler, 0, dev_name, NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
            printk(KERN_ERR "request_irq() failed - invalid irq number (0x%08X) or handler\n", pin_number);
            }
        else if (status == -EBUSY)
            {
            printk(KERN_ERR "request_irq(): irq number (0x%08X) is busy, change your config\n", pin_number);
            }
        return FALSE;
        }

    return TRUE;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;

    // turn on brake
    at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);

    // de-assert the pwm line
    at91_set_gpio_output(OUTPUT_MOVER_PWM_SPEED_THROTTLE, !OUTPUT_MOVER_PWM_SPEED_ACTIVE_STATE);

    if ((hardware_set_gpio_input_irq(INPUT_MOVER_TRACK_HOME, INPUT_MOVER_END_OF_TRACK_PULLUP_STATE, track_sensor_home_int, "track_sensor_home_int") == FALSE) 					||
    	(hardware_set_gpio_input_irq(INPUT_MOVER_TRACK_END, INPUT_MOVER_END_OF_TRACK_PULLUP_STATE, track_sensor_end_int, "track_sensor_end_int") == FALSE))
    	{
		return FALSE;
    	}

    // setup motor PWM output
    status = hardware_motor_pwm_init();

    // turn on motor controller (via h-bridge, so de-assert negatives first, then assert power line)
    at91_set_gpio_output(OUTPUT_MOVER_MOTOR_FWD_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
    at91_set_gpio_output(OUTPUT_MOVER_MOTOR_REV_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
    at91_set_gpio_output(OUTPUT_MOVER_MOTOR_FWD_POS, OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);

    return status;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {

	// de-assert the pwm line
	at91_set_gpio_output(OUTPUT_MOVER_PWM_SPEED_THROTTLE, !OUTPUT_MOVER_PWM_SPEED_ACTIVE_STATE);

	// turn on brake
	at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);

	// turn off both directional lines
	at91_set_gpio_output(OUTPUT_MOVER_DIRECTION_REVERSE, !OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
	at91_set_gpio_output(OUTPUT_MOVER_DIRECTION_FORWARD, !OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);

	// turn on motor controller (via h-bridge, so de-assert power first, then assert negatives)
	at91_set_gpio_output(OUTPUT_MOVER_MOTOR_FWD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
	at91_set_gpio_output(OUTPUT_MOVER_MOTOR_FWD_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
	at91_set_gpio_output(OUTPUT_MOVER_MOTOR_REV_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);

	free_irq(INPUT_MOVER_TRACK_HOME, NULL);
	free_irq(INPUT_MOVER_TRACK_END, NULL);

	target_timer_free(motor_pwm_tc);

	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_movement_set(int movement)
    {
	if ((movement == MOVER_DIRECTION_REVERSE) || (movement == MOVER_DIRECTION_FORWARD))
		{
		// TODO - check our sensors (and possibly the last command) to ensure that we can move in the requested direction
/*
		if (((movement == MOVER_DIRECTION_REVERSE) && ()) ||
				((movement == MOVER_DIRECTION_FORWARD) && ()))
			{
			return FALSE;
			}
*/

		// signal that an operation is in progress
		atomic_set(&moving_atomic, TRUE);

		hardware_motor_on(movement);

		// notify user-space
		schedule_work(&movement_work);

		timeout_timer_start();

		return TRUE;
		}
	else
		{
		return FALSE;
		}
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_movement_get(void)
    {
	return atomic_read(&movement_atomic);
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_speed_get(void)
    {
	return atomic_read(&speed_atomic);
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_speed_set(int speed)
    {
	unsigned long flags;
        int new_speed;

	printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

    if (!atomic_read(&full_init))
        {
		printk(KERN_ALERT "%s - %s() error - driver not fully initialized.\n",TARGET_NAME, __func__);
        return FALSE;
        }

    if (!motor_pwm_tc)
		{
    	return -EINVAL;
		}

	atomic_set(&speed_atomic, speed);

	spin_lock_irqsave(&motor_lock, flags);

	// These change the duty cycle
        new_speed = MOTOR_PWM_RB_DEFAULT + (((MOTOR_PWM_RC - MOTOR_PWM_RB_DEFAULT) * speed) / NUMBER_OF_SPEEDS); // minimum is MOTOR_PWM_RB_DEFAULT, max is MOTOR_PWM_RC
	__raw_writel(new_speed, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RA));
	__raw_writel(new_speed, motor_pwm_tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL, RB));

	spin_unlock_irqrestore(&motor_lock, flags);

    return TRUE;
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

    status = size;

    // We always react to the stop command
    if (sysfs_streq(buf, "stop"))
		{
		printk(KERN_ALERT "%s - %s() : user command stop\n",TARGET_NAME, __func__);

		hardware_movement_stop(TRUE);
		}

    // check if an operation is in progress, if so ignore any command other than 'stop' above
    else if (atomic_read(&moving_atomic))
		{
    	printk(KERN_ALERT "%s - %s() : operation in progress, ignoring command.\n",TARGET_NAME, __func__);
		}

    else if (sysfs_streq(buf, "forward"))
        {
    	printk(KERN_ALERT "%s - %s() : user command forward\n",TARGET_NAME, __func__);

    	hardware_movement_set(MOVER_DIRECTION_FORWARD);
        }

    else if (sysfs_streq(buf, "reverse"))
        {
    	printk(KERN_ALERT "%s - %s() : user command reverse\n",TARGET_NAME, __func__);
    	status = size;

        hardware_movement_set(MOVER_DIRECTION_REVERSE);
        }
    else
		{
    	printk(KERN_ALERT "%s - %s() : unknown user command %s\n",TARGET_NAME, __func__, buf);
		}

    return status;
    }

//---------------------------------------------------------------------------
// Handles reads to the fault attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t fault_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	int fault;
	fault = atomic_read(&fault_atomic);
	atomic_set(&fault_atomic, FAULT_NORMAL);
	return sprintf(buf, "%d\n", fault);
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

    status = size;

    if ((strict_strtol(buf, 0, &value) == 0) &&
    	(value >= 0) &&
    	(value <= NUMBER_OF_SPEEDS))
		{
		hardware_speed_set(value);
		}
	else
		{
		printk(KERN_ALERT "%s - %s() : speed out of range or malformed (%s)\n",TARGET_NAME, __func__, buf);
		}

	return status;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(type, 0444, type_show, NULL);
static DEVICE_ATTR(movement, 0644, movement_show, movement_store);
static DEVICE_ATTR(fault, 0444, fault_show, NULL);
static DEVICE_ATTR(speed, 0644, speed_show, speed_store);

//---------------------------------------------------------------------------
// Defines the attributes of the infantry target mover for sysfs
//---------------------------------------------------------------------------
static const struct attribute * infantry_mover_attrs[] =
    {
    &dev_attr_type.attr,
    &dev_attr_movement.attr,
    &dev_attr_fault.attr,
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
    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
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

