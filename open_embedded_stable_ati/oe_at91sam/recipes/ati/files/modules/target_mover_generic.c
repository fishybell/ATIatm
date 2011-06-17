//---------------------------------------------------------------------------
// target_mover_generic.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#include <linux/atmel_tc.h>
#include <linux/clk.h>
#include <linux/atmel_pwm.h>

#include "target.h"
#include "target_mover_generic.h"

#include "target_generic_output.h" /* for EVENT_### definitions */

//---------------------------------------------------------------------------
// These variables are parameters giving when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
static int mover_type = 1; // 0 = infantry, 1 = armor, 2 = infantry/h-bridge, 3 = error
module_param(mover_type, int, S_IRUGO);

static char* TARGET_NAME[] = {"old infantry mover","armor mover","infantry mover","error"};
static char* MOVER_TYPE[] = {"infantry","armor","infantry","error"};

// continue moving on leg or quad interrupt or neither
static int CONTINUE_ON[] = {2,1,3,0}; // leg = 1, quad = 2, both = 3, neither = 0

// TODO - replace with a table based on distance and speed?
static int TIMEOUT_IN_MSECONDS[] = {500,12000,500,0};
static int MOVER_DELAY_MULT[] = {3,2,3,0};

#define MOVER_POSITION_START 		0
#define MOVER_POSITION_BETWEEN		1	// not at start or end
#define MOVER_POSITION_END			2

#define MOVER_DIRECTION_STOP		0
#define MOVER_DIRECTION_FORWARD		1
#define MOVER_DIRECTION_REVERSE		2
#define MOVER_DIRECTION_STOPPED_FAULT	3

// the maximum allowed speed ticks
static int NUMBER_OF_SPEEDS[] = {10,20,10,0};

// horn on and off times (off is time to wait after mover starts moving before going off)
static int HORN_ON_IN_MSECONDS[] = {0,3500,0,0};
static int HORN_OFF_IN_MSECONDS[] = {0,8000,0,0};

// the paremeters of the velocity ramp up
static int RAMP_TIME_IN_MSECONDS[] = {750,5000,750,0};
static int RAMP_STEPS[] = {100,100,100,0};

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

static int MOTOR_PWM_FWD[] = {OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_MOTOR_FWD_POS,0};
static int MOTOR_PWM_REV[] = {OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_MOTOR_REV_POS,0};
#define MOTOR_PWM_F (reverse ? MOTOR_PWM_REV[mover_type] : MOTOR_PWM_FWD[mover_type])
#define MOTOR_PWM_R (reverse ? MOTOR_PWM_FWD[mover_type] : MOTOR_PWM_REV[mover_type])

// RC - max time (allowed by PWM code)
// END - max time (allowed by me to account for max voltage desired by motor controller : 90% of RC)
// RA - low time setting - cannot exceed RC
// RB - low time setting - cannot exceed RC
static int MOTOR_PWM_RC[] = {0x0070,0x3074,0x4000,0};
static int MOTOR_PWM_END[] = {0x0070,0x3074,0x4000,0};
static int MOTOR_PWM_RA_DEFAULT[] = {0x001C,0x04D8,0x0000,0};
static int MOTOR_PWM_RB_DEFAULT[] = {0x001C,0x04D8,0x0000,0};

// TODO - map pwm output pin to block/channel
#define PWM_BLOCK				1				// block 0 : TIOA0-2, TIOB0-2 , block 1 : TIOA3-5, TIOB3-5
static int MOTOR_PWM_CHANNEL[] = {1,1,2,0};		// channel 0 matches TIOA0 to TIOB0, same for 1 and 2
#define ENCODER_PWM_CHANNEL		0				// channel 0 matches TIOA0 to TIOB0, same for 1 and 2

#define MAX_TIME	0x10000
#define MAX_OVER	0x10000
static int RPM_K[] = {983040, 983040, 983040, 0}; // CLOCK * 60 seconds * 1/2 cycle
static int VELO_K[] = {1680, 1344, 1680, 0}; // rpm/mph*10
static int INCHES_PER_TICK[] = {314, 393, 314, 0}; // 5:1 ratio 10 inch / 2, 2:1 ratio 5 inch / 2, etc.
static int TICKS_PER_LEG[] = {2292, 1833, 2292, 0}; // 5:1 ratio 10 inch wheel 6 ft leg, 2:1 ratio 5 inch wheel 6 ft leg, etc.
#define TICKS_DIV 100

// to keep updates to the file system in check somewhat
#define POSITION_DELAY_IN_MSECONDS	1000
#define VELOCITY_DELAY_IN_MSECONDS	1000

// speed charts (TODO -- update with mover on track with load)
// static in MOVER0_PWM_TABLE = ?
// static in MOVER1_PWM_TABLE = ?
// static int MOVER2_PWM_TABLE[] = {0, 1100, 1550, 1975, 2375, 2800, 3325, 3900, 5000, 7000, 10000}; // -- first stab
static int MOVER2_PWM_TABLE[] = {0, 1350, 1800, 2300, 2700, 3200, 3700, 4500, 5500, 7500, 12000}; // -- second stab



// external motor controller polarity
static int OUTPUT_MOVER_PWM_SPEED_ACTIVE[] = {ACTIVE_HIGH,ACTIVE_LOW,ACTIVE_LOW,0};

// structure to access the timer counter registers
static struct atmel_tc * tc = NULL;

//---------------------------------------------------------------------------
// This variable is a parameter that can be set at module loading to reverse
// the meanings of the 'end of track' sensors and 'forward' and 'reverse'
//---------------------------------------------------------------------------
static int reverse = FALSE;
module_param(reverse, bool, S_IRUGO); // variable reverse, type bool, read only by user, group, other

// map home and 'end of track' sensors based on the 'reverse' parameter
#define INPUT_MOVER_TRACK_HOME	(reverse ? INPUT_MOVER_END_OF_TRACK_1 : INPUT_MOVER_END_OF_TRACK_2)
#define INPUT_MOVER_TRACK_END	(reverse ? INPUT_MOVER_END_OF_TRACK_2 : INPUT_MOVER_END_OF_TRACK_1)

static bool PWM_H_BRIDGE[] = {false,false,true,false};
static bool OUTPUT_H_BRIDGE[] = {false,true,true,false};
static bool USE_BRAKE[] = {true,false,false,false};
static bool MOTOR_CONTROL_H_BRIDGE[] = {true, false, false, false};

// Non-H-Bridge : map motor controller reverse and forward signals based on the 'reverse' parameter
#define OUTPUT_MOVER_FORWARD	(reverse ? OUTPUT_MOVER_DIRECTION_REVERSE : OUTPUT_MOVER_DIRECTION_FORWARD)
#define OUTPUT_MOVER_REVERSE	(reverse ? OUTPUT_MOVER_DIRECTION_FORWARD : OUTPUT_MOVER_DIRECTION_REVERSE)

// H-Bridge : map motor controller reverse and forward signals based on the 'reverse' parameter
#define OUTPUT_MOVER_FORWARD_POS	(reverse ? OUTPUT_MOVER_MOTOR_REV_POS : OUTPUT_MOVER_MOTOR_FWD_POS)
#define OUTPUT_MOVER_REVERSE_POS	(reverse ? OUTPUT_MOVER_MOTOR_FWD_POS : OUTPUT_MOVER_MOTOR_REV_POS)
#define OUTPUT_MOVER_FORWARD_NEG	(reverse ? OUTPUT_MOVER_MOTOR_REV_NEG : OUTPUT_MOVER_MOTOR_FWD_NEG)
#define OUTPUT_MOVER_REVERSE_NEG	(reverse ? OUTPUT_MOVER_MOTOR_FWD_NEG : OUTPUT_MOVER_MOTOR_REV_NEG)

//---------------------------------------------------------------------------
// These atomic variables is use to indicate global position changes
//---------------------------------------------------------------------------
atomic_t velocity = ATOMIC_INIT(0);
atomic_t last_t = ATOMIC_INIT(0);
atomic_t o_count = ATOMIC_INIT(0);
atomic_t delta_t = ATOMIC_INIT(0);
atomic_t position = ATOMIC_INIT(0);
atomic_t position_old = ATOMIC_INIT(0);
atomic_t legs = ATOMIC_INIT(0);
atomic_t quad_direction = ATOMIC_INIT(0);
atomic_t doing_pos = ATOMIC_INIT(FALSE);
atomic_t doing_vel = ATOMIC_INIT(FALSE);
atomic_t tc_clock = ATOMIC_INIT(4);

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
// This atomic variable is use to indicate that we are awake/asleep
//---------------------------------------------------------------------------
atomic_t sleep_atomic = ATOMIC_INIT(0); // not sleeping

//---------------------------------------------------------------------------
// This atomic variable is use to remember the timeout delay multiplier
//---------------------------------------------------------------------------
atomic_t last_mult = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate the goal speed
//---------------------------------------------------------------------------
atomic_t goal_atomic = ATOMIC_INIT(0); // final speed desired
atomic_t goal_start_atomic = ATOMIC_INIT(0); // start of ramp
atomic_t goal_step_atomic = ATOMIC_INIT(0); // last step taken

//---------------------------------------------------------------------------
// This atomic variable is to store the current movement,
// It is used to synchronize user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t movement_atomic = ATOMIC_INIT(MOVER_DIRECTION_STOP);

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
// This delayed work queue item is used to notify user-space of a
// change or error in movement, position, velocity, or position delta
//---------------------------------------------------------------------------
static struct work_struct movement_work;
static struct work_struct position_work;
static struct work_struct velocity_work;
static struct work_struct delta_work;


//---------------------------------------------------------------------------
// Declaration of the function that gets called when the position timers fire
//---------------------------------------------------------------------------
static void position_fire(unsigned long data);
static void velocity_fire(unsigned long data);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the ramp timer fires.
//---------------------------------------------------------------------------
static void ramp_fire(unsigned long data);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the horn timers fire.
//---------------------------------------------------------------------------
static void horn_on_fire(unsigned long data);
static void horn_off_fire(unsigned long data);

//---------------------------------------------------------------------------
// Declaration of functions related to pwm/speed conversion
//---------------------------------------------------------------------------
static int speed_from_pwm(int ra);		// absolute velocity, calculated
static int pwm_from_speed(int speed);	// best-guess pwm, calculated
static int current_speed(void);			// velocity/direction, measured
static int current_speed10(void);		// 10 * velocity/direction, measured

//---------------------------------------------------------------------------
// Kernel timer for the delayed update for position and velocity
//---------------------------------------------------------------------------
static struct timer_list position_timer_list = TIMER_INITIALIZER(position_fire, 0, 0);
static struct timer_list velocity_timer_list = TIMER_INITIALIZER(velocity_fire, 0, 0);

//---------------------------------------------------------------------------
// Kernel timer for the timeout.
//---------------------------------------------------------------------------
static struct timer_list timeout_timer_list = TIMER_INITIALIZER(timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// Kernel timer for the speed ramping
//---------------------------------------------------------------------------
static struct timer_list ramp_timer_list = TIMER_INITIALIZER(ramp_fire, 0, 0);

//---------------------------------------------------------------------------
// Kernel timers for the horn blaring
//---------------------------------------------------------------------------
static struct timer_list horn_on_timer_list = TIMER_INITIALIZER(horn_on_fire, 0, 0);
static struct timer_list horn_off_timer_list = TIMER_INITIALIZER(horn_off_fire, 0, 0);

//---------------------------------------------------------------------------
// Maps the movement to movement name.
//---------------------------------------------------------------------------
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
static void timeout_timer_start(int mult) {
    // remember a previous multiplier if we have no multiplier
    int lmult = atomic_read(&last_mult);
    if (mult == 1 && lmult > 1) {
       mult = lmult - 1;
       atomic_dec(&last_mult);
    }
    if (mult <= 1) {
        // standard timer
        mod_timer(&timeout_timer_list, jiffies+((TIMEOUT_IN_MSECONDS[mover_type]*HZ)/1000));
    } else {
        // (timer + horn) * mult
        mod_timer(&timeout_timer_list, jiffies+((mult*(HORN_ON_IN_MSECONDS[mover_type]+TIMEOUT_IN_MSECONDS[mover_type])*HZ)/1000));
        // remember long timeout so we don't reset to a shorter timeout
        atomic_set(&last_mult, mult);
    }
}

//---------------------------------------------------------------------------
// Stops the timeout timer.
//---------------------------------------------------------------------------
static void timeout_timer_stop(void)
    {
    del_timer(&timeout_timer_list);
    }

//---------------------------------------------------------------------------
// Callback for move events
//---------------------------------------------------------------------------
static move_event_callback move_callback = NULL;
void set_move_callback(move_event_callback handler) {
    // only allow setting the callback once
    if (handler != NULL && move_callback == NULL) {
        move_callback = handler;
        delay_printk("GENERIC MOVER: Registered callback function for move events\n");
    }
}
EXPORT_SYMBOL(set_move_callback);

static void do_event(int etype) {
    if (move_callback != NULL) {
        move_callback(etype);
    }
}

//---------------------------------------------------------------------------
// a request to turn the motor on
//---------------------------------------------------------------------------
static int hardware_motor_on(int direction)
    {
    // turn on directional lines
    if (OUTPUT_H_BRIDGE[mover_type])
        {
        // H-bridge handling
        // de-assert the neg inputs to the h-bridge
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);

        // we always turn both signals off first to ensure that both don't ever get turned
        // on at the same time
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        }
    else
        {
        // non-H-bridge handling
        if (direction == MOVER_DIRECTION_REVERSE)
            {
            // reverse on, forward off
            at91_set_gpio_output(OUTPUT_MOVER_REVERSE, OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
            at91_set_gpio_output(OUTPUT_MOVER_FORWARD, !OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
            }
        else if (direction == MOVER_DIRECTION_FORWARD)
            {
            // reverse off, forward on
            at91_set_gpio_output(OUTPUT_MOVER_REVERSE, !OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
            at91_set_gpio_output(OUTPUT_MOVER_FORWARD, OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
            }
        }

    // turn on horn and wait to do actual move
    del_timer(&horn_on_timer_list); // start horn timer over
    if (HORN_ON_IN_MSECONDS[mover_type] > 0)
        {
        at91_set_gpio_output(OUTPUT_MOVER_HORN, OUTPUT_MOVER_HORN_ACTIVE_STATE);
        mod_timer(&horn_on_timer_list, jiffies+((HORN_ON_IN_MSECONDS[mover_type]*HZ)/1000));
        }
    else
        {
        mod_timer(&horn_on_timer_list, jiffies+((10*HZ)/1000));
        }

    // setup PWM for nothing at start
    __raw_writel(1, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA)); // change to smallest value
    __raw_writel(1, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB)); // change to smallest value


    // log and set direction
    if (direction == MOVER_DIRECTION_REVERSE)
        {
        if (PWM_H_BRIDGE[mover_type]) {
            at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        } else if (OUTPUT_H_BRIDGE[mover_type]) {
            at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        }

        // assert pwm line
        #if PWM_BLOCK == 0
            at91_set_A_periph(MOTOR_PWM_R, PULLUP_OFF);
        #else
            at91_set_B_periph(MOTOR_PWM_R, PULLUP_OFF);
        #endif

        atomic_set(&movement_atomic, MOVER_DIRECTION_REVERSE);
       delay_printk("%s - %s() - reverse\n",TARGET_NAME[mover_type], __func__);
        }
    else if (direction == MOVER_DIRECTION_FORWARD)
        {
        if (PWM_H_BRIDGE[mover_type]) {
            at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        } else if (OUTPUT_H_BRIDGE[mover_type]) {
            at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        }

        #if PWM_BLOCK == 0
            at91_set_A_periph(MOTOR_PWM_F, PULLUP_OFF);
        #else
            at91_set_B_periph(MOTOR_PWM_F, PULLUP_OFF);
        #endif

        atomic_set(&movement_atomic, MOVER_DIRECTION_FORWARD);
       delay_printk("%s - %s() - forward\n",TARGET_NAME[mover_type], __func__);
        }
    else
        {
       delay_printk("%s - %s() - error\n",TARGET_NAME[mover_type], __func__);
        }

    // turn off brake?
    if (USE_BRAKE[mover_type])
        {
        at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, !OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);
        }

    return 0;
    }

//---------------------------------------------------------------------------
// immediately stop the motor, and try to stop the mover
//---------------------------------------------------------------------------
static int hardware_motor_off(void)
    {
   delay_printk("%s - %s()\n",TARGET_NAME[mover_type], __func__);

    // turn off irrelevant timers
    del_timer(&timeout_timer_list);
    del_timer(&horn_on_timer_list);
    del_timer(&horn_off_timer_list);
    del_timer(&ramp_timer_list);
    mod_timer(&horn_off_timer_list, jiffies+((10*HZ)/1000));

    // de-assert the pwm line
    at91_set_gpio_output(MOTOR_PWM_F, !OUTPUT_MOVER_PWM_SPEED_ACTIVE[mover_type]);
    at91_set_gpio_output(MOTOR_PWM_R, !OUTPUT_MOVER_PWM_SPEED_ACTIVE[mover_type]);
    __raw_writel(1, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA)); // change to smallest value
    __raw_writel(1, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB)); // change to smallest value

    // turn on brake?
    if (USE_BRAKE[mover_type])
        {
        at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);
        }

    atomic_set(&movement_atomic, MOVER_DIRECTION_STOP);

    // turn off directional lines
    if (OUTPUT_H_BRIDGE[mover_type])
        {
        // de-assert the all inputs to the h-bridge
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);

        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        }
    else
        {
        at91_set_gpio_output(OUTPUT_MOVER_DIRECTION_REVERSE, !OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_DIRECTION_FORWARD, !OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
        }

    return 0;
    }

//---------------------------------------------------------------------------
// sets up a ramping change in speed
//---------------------------------------------------------------------------
static int hardware_speed_set(int new_speed)
    {
    int old_speed, ra;

   delay_printk("%s - %s(%i)\n",TARGET_NAME[mover_type], __func__, new_speed);

    // check for full initialization
    if (!atomic_read(&full_init))
        {
               delay_printk("%s - %s() error - driver not fully initialized.\n",TARGET_NAME[mover_type], __func__);
        return FALSE;
        }
    if (!tc)
        {
        return -EINVAL;
        }

    // reset timer if we're being told to move
    if (atomic_read(&moving_atomic))
        {
        timeout_timer_stop();
        timeout_timer_start(1);

        // don't ramp if we're already going there
        old_speed = atomic_read(&goal_atomic);
        if (old_speed == new_speed)
            {
            return TRUE;
            }
        }

    // start ramp up
    old_speed = atomic_read(&speed_atomic);
    if (new_speed != old_speed)
        {
        atomic_set(&goal_atomic, new_speed); // reset goal speed
//        atomic_set(&goal_start_atomic, old_speed); // reset ramp start
        ra = __raw_readl(tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA)); // read current raw pwm value
        ra = speed_from_pwm(ra); // calculate speed from pwm value
        atomic_set(&speed_atomic, ra); // reset current speed to actual speed
        atomic_set(&goal_start_atomic, ra); // reset ramp start to actual speed
       delay_printk("%s - %s : old (%i), new (%i), are(%i)\n",TARGET_NAME[mover_type], __func__, old_speed, new_speed, ra);
        del_timer(&ramp_timer_list); // start ramp over
        if (new_speed < old_speed)
            {
            atomic_set(&goal_step_atomic, -1); // stepping backwards
            }
        else
            {
            atomic_set(&goal_step_atomic, 1); // stepping forwards
            }

        // create events
        if (new_speed == 0) {
            do_event(EVENT_COAST); // start of coasting
        } else {
            do_event(EVENT_MOVE); // start of moving
        }

        // are we being told to move?
        if (atomic_read(&moving_atomic))
            {
            // start the ramp up/down of the mover immediately
            mod_timer(&ramp_timer_list, jiffies+(((RAMP_TIME_IN_MSECONDS[mover_type]*HZ)/1000)/RAMP_STEPS[mover_type]));
            }
        }

    return TRUE;
    }

//---------------------------------------------------------------------------
// stops all motion as quickly as possible
//---------------------------------------------------------------------------
static int hardware_movement_stop(int stop_timer)
    {
    int speed;
    if (stop_timer == TRUE)
        {
        timeout_timer_stop();
        }

    // stop ramping, remember goal speed, and reset to 0 (new speed)
    del_timer(&ramp_timer_list); // start ramp over
    speed = atomic_read(&goal_atomic);
    atomic_set(&speed_atomic, 0);

    // turn off the motor
    hardware_motor_off();

    // signal that an operation is done
    atomic_set(&moving_atomic, FALSE);

    // reset speed ramping
    hardware_speed_set(speed);

    // notify user-space
    schedule_work(&movement_work);

    return 0;
    }

//---------------------------------------------------------------------------
// The function that gets called when the position timer fires.
//---------------------------------------------------------------------------
static void position_fire(unsigned long data)
    {
    if (!atomic_read(&full_init))
        {
        return;
        }
    atomic_set(&doing_pos, FALSE);
    schedule_work(&position_work); // notify the system
    }

//---------------------------------------------------------------------------
// The function that gets called when the velocity timer fires.
//---------------------------------------------------------------------------
static void velocity_fire(unsigned long data)
    {
    if (!atomic_read(&full_init))
        {
        return;
        }
    atomic_set(&doing_vel, FALSE);
    schedule_work(&velocity_work); // notify the system
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

   delay_printk(KERN_ERR "%s - %s() - the operation has timed out.\n",TARGET_NAME[mover_type], __func__);

    if (atomic_read(&goal_atomic) == 0) {
       do_event(EVENT_STOPPED); // timeout was part of coasting or stopping
    } else {
       do_event(EVENT_ERROR); // timeout wasn't part of coasting
    }

    hardware_movement_stop(FALSE);
    }

//---------------------------------------------------------------------------
// passed a leg sensor
//---------------------------------------------------------------------------
irqreturn_t leg_sensor_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }
    // only handle the interrupt when sensor 1 is active
    if (at91_get_gpio_value(INPUT_MOVER_TRACK_SENSOR_1) == INPUT_MOVER_TRACK_SENSOR_ACTIVE_STATE)
        {
        // reset the timeout timer?
        if (CONTINUE_ON[mover_type] & 1) {
            timeout_timer_stop();
            timeout_timer_start(1);
        }

        // is the sensor 2 active or not?
        if (at91_get_gpio_value(INPUT_MOVER_TRACK_SENSOR_2) == INPUT_MOVER_TRACK_SENSOR_ACTIVE_STATE)
            {
            // both active, going forward
            atomic_inc(&legs); // gain a leg or lose a leg
            }
        else
            {
            // only sensor 1 is active, going backward
            atomic_dec(&legs); // gain a leg or lose a leg
            }
        // calculate position based on number of times we've passed a track leg
        atomic_set(&position, (atomic_read(&legs) * TICKS_PER_LEG[mover_type])/TICKS_DIV); // this overwrites the value received from the quad encoder
        if (atomic_read(&doing_pos) == FALSE)
            {
            atomic_set(&doing_pos, TRUE);
            mod_timer(&position_timer_list, jiffies+((POSITION_DELAY_IN_MSECONDS*HZ)/1000));
            }
        }
    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// the quad encoder pin tripped
//---------------------------------------------------------------------------
irqreturn_t quad_encoder_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    u32 status, rb, cv, this_t, dn1, dn2;
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }
    if (!tc) return IRQ_HANDLED;

    status = __raw_readl(tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, SR)); // status register
    this_t = __raw_readl(tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, RA));
    rb = __raw_readl(tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, RB));
    cv = __raw_readl(tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, CV));

//delay_printk("O:%i A:%i l:%i t:%i c:%i o:%08x\n", status & ATMEL_TC_COVFS, status & ATMEL_TC_LDRAS, atomic_read(&last_t), this_t, cv, atomic_read(&o_count) );

    // Overlflow caused IRQ?
    if ( status & ATMEL_TC_COVFS )
        {
        atomic_set(&o_count, atomic_read(&o_count) + MAX_TIME);
        }

    // Pin A going high caused IRQ?
    if ( status & ATMEL_TC_LDRAS )
        {
        // reset the timeout timer?
        if (CONTINUE_ON[mover_type] & 2) {
            timeout_timer_stop();
            timeout_timer_start(1);
        }

        // change position
        if ((status & ATMEL_TC_MTIOB && !reverse) || (!(status & ATMEL_TC_MTIOB) && reverse))
            {
            atomic_dec(&position);
            atomic_set(&quad_direction, -1);
            }
        else
            {
            atomic_inc(&position);
            atomic_set(&quad_direction, 1);
            }
        atomic_set(&delta_t, this_t + atomic_read(&o_count));
        atomic_set(&o_count, 0);
        dn1 = atomic_read(&delta_t);
        dn2 = atomic_read(&quad_direction);
        if (dn1 * dn2 == 0)
            {
            return IRQ_HANDLED;
            }
        atomic_set(&velocity, dn1 * dn2);
        atomic_set(&last_t, this_t);
        if (atomic_read(&doing_pos) == FALSE)
            {
            atomic_set(&doing_pos, TRUE);
            mod_timer(&position_timer_list, jiffies+((POSITION_DELAY_IN_MSECONDS*HZ)/1000));
            }
        if (atomic_read(&doing_vel) == FALSE)
            {
            atomic_set(&doing_vel, TRUE);
            mod_timer(&velocity_timer_list, jiffies+((VELOCITY_DELAY_IN_MSECONDS*HZ)/1000));
            }
        }
    else
        {
        // Pin A did not go high
        if ( atomic_read(&o_count) >= MAX_OVER )
            {
            atomic_set(&velocity, 0);
            if (atomic_read(&doing_vel) == FALSE)
                {
                atomic_set(&doing_vel, TRUE);
                mod_timer(&velocity_timer_list, jiffies+((VELOCITY_DELAY_IN_MSECONDS*HZ)/1000));
                }
            }
        }

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// track sensor "home"
//---------------------------------------------------------------------------
irqreturn_t track_sensor_home_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

   delay_printk("%s - %s() : %i\n",TARGET_NAME[mover_type], __func__, atomic_read(&movement_atomic));

    // reset to "home" position
    atomic_set(&legs, 0);
    atomic_set(&position, 0);

    // check to see if this one needs to be ignored
    if (atomic_read(&movement_atomic) == MOVER_DIRECTION_FORWARD)
        {
        // ...then ignore switch
        return IRQ_HANDLED;
        }

    do_event(EVENT_STOP); // started stopping
    atomic_set(&goal_atomic, 0); // reset goal speed
    hardware_movement_stop(FALSE);

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// track sensor "end"
//---------------------------------------------------------------------------
irqreturn_t track_sensor_end_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

   delay_printk("%s - %s() : %i\n",TARGET_NAME[mover_type], __func__, atomic_read(&movement_atomic));

    // check to see if this one needs to be ignored
    if (atomic_read(&movement_atomic) == MOVER_DIRECTION_REVERSE)
        {
        // ...then ignore switch
        return IRQ_HANDLED;
        }

    do_event(EVENT_STOP); // started stopping
    atomic_set(&goal_atomic, 0); // reset goal speed
    hardware_movement_stop(FALSE);

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// set PINs to be used by peripheral A and/or B
//---------------------------------------------------------------------------
static void init_quad_pins(void)
    {
    #if PWM_BLOCK == 0
        #if ENCODER_PWM_CHANNEL == 0
            at91_set_A_periph(AT91_PIN_PA26, 0);	// TIOA0
            at91_set_gpio_input(AT91_PIN_PA26, 1);	// TIOA0
            at91_set_B_periph(AT91_PIN_PC9, 0);		// TIOB0
            at91_set_gpio_input(AT91_PIN_PC9, 1);	// TIOB0
        #elif ENCODER_PWM_CHANNEL == 1
            at91_set_A_periph(AT91_PIN_PA27, 0);	// TIOA1
            at91_set_gpio_input(AT91_PIN_PA27, 1);	// TIOA1
            at91_set_A_periph(AT91_PIN_PC7, 0);		// TIOB1
            at91_set_gpio_input(AT91_PIN_PC7, 1);	// TIOB1
        #elif ENCODER_PWM_CHANNEL == 2
            at91_set_A_periph(AT91_PIN_PA28, 0);	// TIOA2
            at91_set_gpio_input(AT91_PIN_PA28, 1);	// TIOA2
            at91_set_A_periph(AT91_PIN_PC6, 0);		// TIOB2
            at91_set_gpio_input(AT91_PIN_PC6, 1);	// TIOB2
        #endif
    #else
        #if ENCODER_PWM_CHANNEL == 0
            at91_set_B_periph(AT91_PIN_PB0, 0);		// TIOA3
            at91_set_gpio_input(AT91_PIN_PB0, 1);	// TIOA3
            at91_set_B_periph(AT91_PIN_PB1, 0);		// TIOB3
            at91_set_gpio_input(AT91_PIN_PB1, 1);	// TIOB3
        #elif ENCODER_PWM_CHANNEL == 1
            at91_set_B_periph(AT91_PIN_PB2, 0);		// TIOA4
            at91_set_gpio_input(AT91_PIN_PB2, 1);	// TIOA4
            at91_set_B_periph(AT91_PIN_PB18, 0);	// TIOB4
            at91_set_gpio_input(AT91_PIN_PB18, 1);	// TIOB4
        #elif ENCODER_PWM_CHANNEL == 2
            at91_set_B_periph(AT91_PIN_PB3, 0);		// TIOA5
            at91_set_gpio_input(AT91_PIN_PB3, 1);	// TIOA5
            at91_set_B_periph(AT91_PIN_PB19, 0);	// TIOB5
            at91_set_gpio_input(AT91_PIN_PB19, 1);	// TIOB5
        #endif
    #endif
    }

//---------------------------------------------------------------------------
// Initialize the timer counter as PWM output for motor control
//---------------------------------------------------------------------------
static int hardware_pwm_init(void)
    {
    unsigned int mck_freq;
    int status = 0;

    // initialize timer counter
    tc = target_timer_alloc(PWM_BLOCK, "gen_tc");
   delay_printk("timer_alloc(): %08x\n", (unsigned int) tc);

    if (!tc)
        {
        return -EINVAL;
        }

    mck_freq = clk_get_rate(tc->clk[MOTOR_PWM_CHANNEL[mover_type]]);
   delay_printk("mck_freq: %i\n", (unsigned int) mck_freq);

    // initialize quad encoder pins
    init_quad_pins();

/*
    #if PWM_BLOCK == 0
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
//            at91_set_B_periph(AT91_PIN_PB3, 0);		// TIOA5
            at91_set_B_periph(AT91_PIN_PB19, 0);	// TIOB5
        #endif
    #endif
*/

    if (clk_enable(tc->clk[ENCODER_PWM_CHANNEL]) != 0)
            {
           delay_printk(KERN_ERR "ENCODER clk_enable() failed\n");
            return -EINVAL;
            }

    if (clk_enable(tc->clk[MOTOR_PWM_CHANNEL[mover_type]]) != 0)
            {
           delay_printk(KERN_ERR "MOTOR clk_enable() failed\n");
            return -EINVAL;
            }

    // initialize quad timer
    __raw_writel(ATMEL_TC_TIMER_CLOCK5				// Slow clock = 33 khz
                    | ATMEL_TC_ETRGEDG_RISING		// Trigger on the rising and falling edges
                    | ATMEL_TC_ABETRG				// Trigger on TIOA
                    | ATMEL_TC_LDRA_RISING,			// Load RA on rising edge
                    tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, CMR));	// channel module register

    // setup encoder irq
printk("going to request irq: %i\n", tc->irq[ENCODER_PWM_CHANNEL]);
    status = request_irq(tc->irq[ENCODER_PWM_CHANNEL], (void*)quad_encoder_int, IRQF_DISABLED, "quad_encoder_int", NULL);
printk("requested irq: %i\n", status);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
           delay_printk(KERN_ERR "request_irq(): Bad irq number or handler\n");
            }
        else if (status == -EBUSY)
            {
           delay_printk(KERN_ERR "request_irq(): IRQ is busy, change your config\n");
            }
        target_timer_free(tc);
        return status;
        }

    // enable specific interrupts for the encoder
    __raw_writel(ATMEL_TC_COVFS						// interrupt on counter overflow
                    | ATMEL_TC_LDRAS,				// interrupt on loading RA
                    tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, IER)); // interrupt enable register
    __raw_writel(ATMEL_TC_TC0XC0S_NONE				// no signal on XC0
                    | ATMEL_TC_TC1XC1S_NONE			// no signal on XC1
                    | ATMEL_TC_TC2XC2S_NONE,		// no signal on XC2
                    tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, BMR)); // block mode register
    __raw_writel(0xffff, tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, RC));

printk("bytes written: %04x\n", __raw_readl(tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, IMR)));

     // initialize pwm output timer
     switch (mover_type)
        {
        case 0:
        // initialize infantry clock
        __raw_writel(ATMEL_TC_TIMER_CLOCK1			// Master clock/2 = 132MHz/2 ~ 66MHz
                    | ATMEL_TC_WAVE					// output mode
                    | ATMEL_TC_ACPA_CLEAR			// set TIOA low when counter reaches "A"
                    | ATMEL_TC_ACPC_SET				// set TIOA high when counter reaches "C"
                    | ATMEL_TC_BCPB_CLEAR			// set TIOB low when counter reaches "B"
                    | ATMEL_TC_BCPC_SET				// set TIOB high when counter reaches "C"
                    | ATMEL_TC_EEVT_XC0				// set external clock 0 as the trigger
                    | ATMEL_TC_WAVESEL_UP_AUTO,		// reset counter when counter reaches "C"
                    tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], CMR));	// CMR register for timer
        break;

        case 1:
        // initialize armor clock
        __raw_writel(ATMEL_TC_TIMER_CLOCK2			// Master clock/8 = 132MHz/8 ~ 16MHz
                    | ATMEL_TC_WAVE					// output mode
                    | ATMEL_TC_ACPA_SET				// set TIOA high when counter reaches "A"
                    | ATMEL_TC_ACPC_CLEAR			// set TIOA low when counter reaches "C"
                    | ATMEL_TC_BCPB_SET				// set TIOB high when counter reaches "B"
                    | ATMEL_TC_BCPC_CLEAR			// set TIOB low when counter reaches "C"
                    | ATMEL_TC_EEVT_XC0				// set external clock 0 as the trigger
                    | ATMEL_TC_WAVESEL_UP_AUTO,		// reset counter when counter reaches "C"
                    tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], CMR));	// CMR register for timer
        break;
        case 2:
        // initialize infantry/h-bridge  clock
        __raw_writel(ATMEL_TC_TIMER_CLOCK4			// Master clock/128 = 132MHz/128 ~ 1MHz
                    | ATMEL_TC_WAVE					// output mode
                    | ATMEL_TC_ACPA_SET				// set TIOA low when counter reaches "A"
                    | ATMEL_TC_ACPC_CLEAR			// set TIOA high when counter reaches "C"
                    | ATMEL_TC_BCPB_SET				// set TIOB low when counter reaches "B"
                    | ATMEL_TC_BCPC_CLEAR			// set TIOB high when counter reaches "C"
                    | ATMEL_TC_EEVT_XC0				// set external clock 0 as the trigger
                    | ATMEL_TC_WAVESEL_UP_AUTO,		// reset counter when counter reaches "C"
                    tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], CMR));	// CMR register for timer
        break;

        default: return -EINVAL; break;
        }

     // initialize clock timer
    __raw_writel(ATMEL_TC_CLKEN, tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, CCR));
    __raw_writel(ATMEL_TC_CLKEN, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], CCR));
    __raw_writel(ATMEL_TC_SYNC, tc->regs + ATMEL_TC_BCR);

    // These set up the freq and duty cycle
    __raw_writel(MOTOR_PWM_RA_DEFAULT[mover_type], tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
    __raw_writel(MOTOR_PWM_RB_DEFAULT[mover_type], tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB));
    __raw_writel(MOTOR_PWM_RC[mover_type], tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RC));

    // disable irqs and start output timer
    __raw_writel(0xff, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], IDR));				// irq register
    __raw_writel(ATMEL_TC_SWTRG, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], CCR));		// control register

    // stat input timer
    __raw_writel(ATMEL_TC_SWTRG, tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, CCR));	// control register

    return status;
    }

//---------------------------------------------------------------------------
// set an IRQ for a GPIO pin to call a handler function
//---------------------------------------------------------------------------
static int hardware_set_gpio_input_irq(int pin_number,
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
           delay_printk(KERN_ERR "request_irq() failed - invalid irq number (0x%08X) or handler\n", pin_number);
            }
        else if (status == -EBUSY)
            {
           delay_printk(KERN_ERR "request_irq(): irq number (0x%08X) is busy, change your config\n", pin_number);
            }
        return FALSE;
        }

    return TRUE;
    }

//---------------------------------------------------------------------------
// called to initialize driver
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;
   delay_printk("%s reverse: %i\n",__func__,  reverse);

    // turn on brake?
    if (USE_BRAKE[mover_type])
        {
        at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);
        }

    // always put the H-bridge circuitry in the right state from the beginning
    if (OUTPUT_H_BRIDGE[mover_type]) {
        // de-assert the neg inputs to the h-bridge
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);

        // configure motor gpio for output and set initial output
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
    } else {
        // if we don't use the H-bridge for direction, turn on motor controller
        //  (via h-bridge, so de-assert negatives first, then assert power line)
        at91_set_gpio_output(OUTPUT_MOVER_MOTOR_FWD_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_MOTOR_REV_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_MOTOR_FWD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_MOTOR_REV_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        if (MOTOR_CONTROL_H_BRIDGE[mover_type]) {
            at91_set_gpio_output(OUTPUT_MOVER_MOTOR_FWD_POS, OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE); // motor controller on
        }
    }

    // de-assert the pwm line
    at91_set_gpio_output(MOTOR_PWM_F, !OUTPUT_MOVER_PWM_SPEED_ACTIVE[mover_type]);
    at91_set_gpio_output(MOTOR_PWM_R, !OUTPUT_MOVER_PWM_SPEED_ACTIVE[mover_type]);

    if (HORN_ON_IN_MSECONDS[mover_type] > 0)
        {
        // configure horn for output and set initial state
        at91_set_gpio_output(OUTPUT_MOVER_HORN, !OUTPUT_MOVER_HORN_ACTIVE_STATE);
        }

    // setup PWM input/output
    status = hardware_pwm_init();

    // install track sensor interrupts
    if ((hardware_set_gpio_input_irq(INPUT_MOVER_TRACK_HOME, INPUT_MOVER_END_OF_TRACK_PULLUP_STATE, track_sensor_home_int, "track_sensor_home_int") == FALSE) ||
        (hardware_set_gpio_input_irq(INPUT_MOVER_TRACK_END, INPUT_MOVER_END_OF_TRACK_PULLUP_STATE, track_sensor_end_int, "track_sensor_end_int") == FALSE))
        {
        return FALSE;
        }

    // Configure leg sensor gpios for input and deglitch for interrupts
    at91_set_gpio_input(INPUT_MOVER_TRACK_SENSOR_2, INPUT_MOVER_TRACK_SENSOR_PULLUP_STATE); // other leg sensor is also an input, just no irq

    // install leg sensor interrupts
    if ((hardware_set_gpio_input_irq(INPUT_MOVER_TRACK_SENSOR_1, INPUT_MOVER_TRACK_SENSOR_PULLUP_STATE, leg_sensor_int, "leg_sensor_int") == FALSE)) {
        return FALSE;
    }

    return status;
    }

//---------------------------------------------------------------------------
// called on driver shutdown
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
    // turn on brake?
    if (USE_BRAKE[mover_type])
        {
        at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);
        }

    // always put the H-bridge circuitry in the right state on shutdown
    if (OUTPUT_H_BRIDGE[mover_type]) {
        // de-assert the neg inputs to the h-bridge
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);

        // configure motor gpio for output and set initial output
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
    } else {
        // if we don't use the H-bridge for direction, turn off motor controller
        //  (via h-bridge, so de-assert negatives first, then de-assert power line)
        at91_set_gpio_output(OUTPUT_MOVER_MOTOR_FWD_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_MOTOR_REV_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        if (MOTOR_CONTROL_H_BRIDGE[mover_type]) {
            at91_set_gpio_output(OUTPUT_MOVER_MOTOR_FWD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE); // motor controller off
        }
    }

    // change pwm back to gpio
    at91_set_gpio_output(MOTOR_PWM_F, !OUTPUT_MOVER_PWM_SPEED_ACTIVE[mover_type]);
    at91_set_gpio_output(MOTOR_PWM_R, !OUTPUT_MOVER_PWM_SPEED_ACTIVE[mover_type]);

    del_timer(&timeout_timer_list);
    del_timer(&horn_on_timer_list);
    del_timer(&horn_off_timer_list);
    del_timer(&ramp_timer_list);
    del_timer(&position_timer_list);
    del_timer(&velocity_timer_list);

    free_irq(tc->irq[ENCODER_PWM_CHANNEL], NULL);
    free_irq(INPUT_MOVER_TRACK_HOME, NULL);
    free_irq(INPUT_MOVER_TRACK_END, NULL);
    free_irq(INPUT_MOVER_TRACK_SENSOR_1, NULL);

    target_timer_free(tc);

    return 0;
    }

//---------------------------------------------------------------------------
// set moving forward or reverse
//---------------------------------------------------------------------------
static int hardware_movement_set(int movement)
    {
    if ((movement == MOVER_DIRECTION_REVERSE) || (movement == MOVER_DIRECTION_FORWARD))
        {

        // signal that an operation is in progress
        atomic_set(&moving_atomic, TRUE);

        hardware_motor_on(movement);

        // notify user-space
        schedule_work(&movement_work);

        timeout_timer_start(MOVER_DELAY_MULT[mover_type]); // initial timeout timer twice as long as normal

        return TRUE;
        }
    else
        {
        return FALSE;
        }
    }

//---------------------------------------------------------------------------
// get current direction
//---------------------------------------------------------------------------
static int hardware_movement_get(void)
    {
    return atomic_read(&movement_atomic);
    }

//---------------------------------------------------------------------------
// get current speed
//---------------------------------------------------------------------------
static int hardware_speed_get(void)
    {
    return atomic_read(&speed_atomic);
    }

//---------------------------------------------------------------------------
// The function that gets called when the horn on timer fires.
//---------------------------------------------------------------------------
static void horn_on_fire(unsigned long data)
    {
    if (!atomic_read(&full_init))
        {
        return;
        }
   delay_printk("%s - %s()\n",TARGET_NAME[mover_type], __func__);

    // start the ramp up/down of the mover
    mod_timer(&ramp_timer_list, jiffies+(((RAMP_TIME_IN_MSECONDS[mover_type]*HZ)/1000)/RAMP_STEPS[mover_type]));

    // turn off the horn later?
    if (HORN_ON_IN_MSECONDS[mover_type] > 0)
        {
        mod_timer(&horn_off_timer_list, jiffies+((HORN_OFF_IN_MSECONDS[mover_type]*HZ)/1000));
        }
    }

//---------------------------------------------------------------------------
// The function that gets called when the horn on timer fires.
//---------------------------------------------------------------------------
static void horn_off_fire(unsigned long data)
    {
    if (!atomic_read(&full_init))
        {
        return;
        }
   delay_printk("%s - %s()\n",TARGET_NAME[mover_type], __func__);

    // turn off horn?
    if (HORN_ON_IN_MSECONDS[mover_type] > 0)
        {
        at91_set_gpio_output(OUTPUT_MOVER_HORN, !OUTPUT_MOVER_HORN_ACTIVE_STATE);
        }
    }

//---------------------------------------------------------------------------
// Helper function to map pwm values to speed values
//---------------------------------------------------------------------------
static int pwm_from_speed(int speed) {
    // limit to max speed
    if (speed > NUMBER_OF_SPEEDS[mover_type]) {
        speed = NUMBER_OF_SPEEDS[mover_type];
    }
    // limit to min speed
    if (speed < 0) {
        speed = 0;
    }
    switch (mover_type) {
        case 0:
        case 1:
            // use straight percentage
            return MOTOR_PWM_RB_DEFAULT[mover_type] + ((speed * (MOTOR_PWM_END[mover_type] - MOTOR_PWM_RB_DEFAULT[mover_type])) / NUMBER_OF_SPEEDS[mover_type]);
        case 2:
            // use lookup chart
            return MOVER2_PWM_TABLE[speed];
    }
    return MOTOR_PWM_RB_DEFAULT[mover_type];
}

//---------------------------------------------------------------------------
// Helper function to map speed values to pwm values
//---------------------------------------------------------------------------
static int speed_from_pwm(int ra) {
    int i;
    // limit to max pwm
    if (ra > MOTOR_PWM_END[mover_type]) {
        ra = MOTOR_PWM_END[mover_type];
    }
    // limit to min pwm
    if (ra < MOTOR_PWM_RB_DEFAULT[mover_type]) {
        ra = MOTOR_PWM_RB_DEFAULT[mover_type];
    }
    switch (mover_type) {
        case 0:
        case 1:
            // use straight percentage
            return (NUMBER_OF_SPEEDS[mover_type]+(1+ra-MOTOR_PWM_RB_DEFAULT[mover_type])*NUMBER_OF_SPEEDS[mover_type])/(MOTOR_PWM_END[mover_type] - MOTOR_PWM_RB_DEFAULT[mover_type]); // calculate actual speed from pwm value (the additional NUMBER_OF_SPEEDS and 1 are to fix rounding issues due to previous loss of precision)
        case 2:
            // use lookup chart
            for (i = NUMBER_OF_SPEEDS[mover_type]; i > 0; i--) {
               if (MOVER2_PWM_TABLE[i] <= ra) {
                  return i;
               }
            }
            break;
    }
    return 0;
}

//---------------------------------------------------------------------------
// Helper function to get current measured speed (slight lag behind actual speed)
//---------------------------------------------------------------------------
static int current_speed() {
    return current_speed10()/10;
}

static int current_speed10() {

    int vel = atomic_read(&velocity);
    if (vel != 0) {
        vel = (100*RPM_K[mover_type]/vel)/VELO_K[mover_type];
    }

    return vel;
}

//---------------------------------------------------------------------------
// Get/set functions for external control from within kernel
//---------------------------------------------------------------------------
int mover_speed_get(void) {
   return current_speed();
}
EXPORT_SYMBOL(mover_speed_get);

int mover_speed_set(int speed) {
   // check to see if we're sleeping or not
   if (atomic_read(&sleep_atomic) == 1) { return 0; }

   // can we go the requested speed?
   if (abs(speed) > NUMBER_OF_SPEEDS[mover_type]) {
      return 0; // nope
   }

   // first set the desired speed
   hardware_speed_set(abs(speed));

   // next fire off movement
   if (speed < 0) {
      hardware_movement_set(MOVER_DIRECTION_REVERSE);
   } else if (speed > 0) {
      hardware_movement_set(MOVER_DIRECTION_FORWARD);
   } // setting 0 will cause the mover to "coast" if it isn't already stopped

   return 1;
}
EXPORT_SYMBOL(mover_speed_set);
 
int mover_speed_stop() {
    do_event(EVENT_STOP); // started stopping
    atomic_set(&goal_atomic, 0); // reset goal speed
    hardware_movement_stop(FALSE);
    return 1;
}
EXPORT_SYMBOL(mover_speed_stop);

extern int mover_position_get() {
    int pos = atomic_read(&position);
    return ((INCHES_PER_TICK[mover_type]*pos)/TICKS_DIV)/12; // inches to feet
}
EXPORT_SYMBOL(mover_position_get);

//---------------------------------------------------------------------------
// Sleep the device
//---------------------------------------------------------------------------
int mover_sleep_set(int value) {
delay_printk("mover_sleep_set(%i)\n", value);
   if (value == 1) {
      // start sleeping by stopping
      mover_speed_stop();
   }
   atomic_set(&sleep_atomic, value);
   return 1;
}
EXPORT_SYMBOL(mover_sleep_set);

//---------------------------------------------------------------------------
// Get device sleep state
//---------------------------------------------------------------------------
int mover_sleep_get(void) {
delay_printk("mover_sleep_get()\n");
   return atomic_read(&sleep_atomic);
}
EXPORT_SYMBOL(mover_sleep_get);


//---------------------------------------------------------------------------
// The function that gets called when the ramp timer fires.
//---------------------------------------------------------------------------
static void ramp_fire(unsigned long data) {
    int goal_start, goal_step, goal_end, new_speed, ticks_change, ramp, start_speed, direction, speed_steps;
    if (!atomic_read(&full_init)) {
        return;
    }

    goal_start = atomic_read(&goal_start_atomic);
    goal_step = atomic_read(&goal_step_atomic);
    goal_end = atomic_read(&goal_atomic);

    // calculate speed based on percent of full difference
    ticks_change = abs(pwm_from_speed(goal_end) - pwm_from_speed(goal_start));

    // calculate start speed
    start_speed = pwm_from_speed(goal_start);

    // calculate number of steps to take based on the amount of speed change
    speed_steps = (RAMP_STEPS[mover_type] * abs(goal_end - goal_start)) / 2; // 1/2 normal steps * number of speed change

    // find direction
    if (goal_end < goal_start) {
        direction = -1;
        goal_step *= -1;
    } else {
        direction = 1;
    }

    // do ramp function in positive world
    if (goal_step <= speed_steps/2) {
        // calculate change for this step in the lower-half of the ramp (ramp based on simple y=x^2 curve)
        ramp = ((goal_step * goal_step) * (ticks_change/2)) / ((speed_steps/2) * (speed_steps/2));
    } else {
        // calculate change for this step in the upper-half of the ramp (ramp based on simple y=-x^2 curve)
        ramp = (((speed_steps-goal_step) * (speed_steps-goal_step)) * (ticks_change/2)) / ((speed_steps/2) * (speed_steps/2)); // (speed_steps-goal_step) = stepping backwards to create illusion of -x^2 curve
        ramp = ticks_change - ramp; // flip upside down to complete -x^2 curve
    }

    // reverse ramp for negative world
    if (direction == -1) {
       goal_step *= -1;
    }

    // add to original start of ramp
    if (goal_step < 0) {
        // stepping backwards
        //new_speed = start_speed - ramp;
        new_speed = start_speed - ramp;
        atomic_set(&goal_step_atomic, goal_step - 1);
    } else {
        // stepping forwards
        new_speed = start_speed + ramp;
        atomic_set(&goal_step_atomic, goal_step + 1);
    }

    //delay_printk("%s - %s : %i, %i, %i %i\n",TARGET_NAME[mover_type], __func__, ticks_change, start_speed, ramp, new_speed);

    // take another step?
    if (abs(goal_step) < speed_steps) {
        mod_timer(&ramp_timer_list, jiffies+(((RAMP_TIME_IN_MSECONDS[mover_type]*HZ)/1000)/RAMP_STEPS[mover_type])); // use original number of steps here to allow speed_steps to stretch the time
    } else {
        // done moving
        atomic_set(&speed_atomic, goal_end); // reached new speed value

        // create events
        if (goal_end == 0) {
            do_event(EVENT_STOP); // started stopping (probably finished stopping too, but that's handled elsewhere)
        } else {
            do_event(EVENT_MOVING); // reached moving goal
        }
    }

    // These change the pwm duty cycle
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB));
}

//---------------------------------------------------------------------------
// Handles reads to the position attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t position_show(struct device *dev, struct device_attribute *attr, char *buf) {
    return sprintf(buf, "%i\n", mover_position_get());
}

// Handles reads to the velocity attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t velocity_show(struct device *dev, struct device_attribute *attr, char *buf) {
    int vel = current_speed10();
    if (vel != 0) {
        return sprintf(buf, "%i.%i\n", vel/10, abs(vel)%10);
    } else {
        return sprintf(buf, "%i.%i\n", 0, 0);
    }
}

// Handles reads to the velocity attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t rpm_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    int vel = atomic_read(&velocity);
    if (vel != 0) {
        return sprintf(buf, "%i\n", RPM_K[mover_type]/vel);
    } else {
        return sprintf(buf, "%i\n", 0);
    }
    }

// Handles reads to the delta attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t delta_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%i\n", atomic_read(&delta_t));
    }

//---------------------------------------------------------------------------
// Handles reads to the type attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t type_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", MOVER_TYPE[mover_type]);
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
    delay_printk("%s - %s() : user command stop\n",TARGET_NAME[mover_type], __func__);

        do_event(EVENT_STOP); // started stopping
        atomic_set(&goal_atomic, 0); // reset goal speed
        hardware_movement_stop(FALSE);
        }

    // check if an operation is in progress, if so ignore any command other than 'stop' above
    else if (atomic_read(&moving_atomic))
        {
    delay_printk("%s - %s() : operation in progress, ignoring command.\n",TARGET_NAME[mover_type], __func__);
        }

    else if (sysfs_streq(buf, "forward"))
        {
    delay_printk("%s - %s() : user command forward\n",TARGET_NAME[mover_type], __func__);

        hardware_movement_set(MOVER_DIRECTION_FORWARD);
        }

    else if (sysfs_streq(buf, "reverse"))
        {
    delay_printk("%s - %s() : user command reverse\n",TARGET_NAME[mover_type], __func__);
        status = size;

        hardware_movement_set(MOVER_DIRECTION_REVERSE);
        }
    else
        {
    delay_printk("%s - %s() : unknown user command %s\n",TARGET_NAME[mover_type], __func__, buf);
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
    atomic_set(&fault_atomic, FAULT_NORMAL); // reset fault on receipt by user-space
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
// Handles reads to the ra attribute through sysfs (for debugging only)
//---------------------------------------------------------------------------
static ssize_t ra_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
        int ra;
    ra = __raw_readl(tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
    return sprintf(buf, "%d\n", ra);
    }

//---------------------------------------------------------------------------
// Handles writes to the ra attribute through sysfs (for debugging only)
//---------------------------------------------------------------------------
static ssize_t ra_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    long value;
    ssize_t status;

    status = strict_strtol(buf, 0, &value);
    if (status == 0)
        { 
    __raw_writel(value, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
    __raw_writel(value, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB));
    // de-assert the neg inputs to the h-bridge
    at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
    at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);

    // we always turn both signals off first to ensure that both don't ever get turned
    // on at the same time
    at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
    at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);

        // assert pwm line
        #if PWM_BLOCK == 0
            at91_set_A_periph(MOTOR_PWM_F, PULLUP_OFF);
        #else
            at91_set_B_periph(MOTOR_PWM_F, PULLUP_OFF);
        #endif
        }

    if (!PWM_H_BRIDGE[mover_type]) {
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
    } else {
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
    }

    return status;
    }

//---------------------------------------------------------------------------
// Handles reads to the ra attribute through sysfs (for debugging only)
//---------------------------------------------------------------------------
static ssize_t rc_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
        int rc;
    rc = __raw_readl(tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RC));
    return sprintf(buf, "%d\n", rc);
    }

//---------------------------------------------------------------------------
// Handles writes to the ra attribute through sysfs (for debugging only)
//---------------------------------------------------------------------------
static ssize_t rc_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
    long value;
    ssize_t status;

    status = strict_strtol(buf, 0, &value);
    if (status == 0) {
        MOTOR_PWM_RC[mover_type] = value;
        MOTOR_PWM_END[mover_type] = value;
    __raw_writel(MOTOR_PWM_RC[mover_type], tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RC));

    }
    return status;
}

//---------------------------------------------------------------------------
// Handles reads to the ra attribute through sysfs (for debugging only)
//---------------------------------------------------------------------------
static ssize_t clock_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
        int rc;
    rc = atomic_read(&tc_clock);
    return sprintf(buf, "%d\n", rc);
    }

//---------------------------------------------------------------------------
// Handles writes to the ra attribute through sysfs (for debugging only)
//---------------------------------------------------------------------------
static ssize_t clock_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
    long value;
    ssize_t status;

    status = strict_strtol(buf, 0, &value);
    if (status == 0) {
        atomic_set(&tc_clock, value);

     switch (value) {
         case 1: value = ATMEL_TC_TIMER_CLOCK1; break;
         case 2: value = ATMEL_TC_TIMER_CLOCK2; break;
         case 3: value = ATMEL_TC_TIMER_CLOCK3; break;
         case 4: value = ATMEL_TC_TIMER_CLOCK4; break;
         case 5: value = ATMEL_TC_TIMER_CLOCK5; break;
     }

     // initialize pwm output timer
     switch (mover_type)
        {
        case 0:
        // initialize infantry clock
        __raw_writel(value			// new clock
                    | ATMEL_TC_WAVE					// output mode
                    | ATMEL_TC_ACPA_CLEAR			// set TIOA low when counter reaches "A"
                    | ATMEL_TC_ACPC_SET				// set TIOA high when counter reaches "C"
                    | ATMEL_TC_BCPB_CLEAR			// set TIOB low when counter reaches "B"
                    | ATMEL_TC_BCPC_SET				// set TIOB high when counter reaches "C"
                    | ATMEL_TC_EEVT_XC0				// set external clock 0 as the trigger
                    | ATMEL_TC_WAVESEL_UP_AUTO,		// reset counter when counter reaches "C"
                    tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], CMR));	// CMR register for timer
        break;

        case 1:
        // initialize armor clock
        __raw_writel(value			// new clock
                    | ATMEL_TC_WAVE					// output mode
                    | ATMEL_TC_ACPA_SET				// set TIOA high when counter reaches "A"
                    | ATMEL_TC_ACPC_CLEAR			// set TIOA low when counter reaches "C"
                    | ATMEL_TC_BCPB_SET				// set TIOB high when counter reaches "B"
                    | ATMEL_TC_BCPC_CLEAR			// set TIOB low when counter reaches "C"
                    | ATMEL_TC_EEVT_XC0				// set external clock 0 as the trigger
                    | ATMEL_TC_WAVESEL_UP_AUTO,		// reset counter when counter reaches "C"
                    tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], CMR));	// CMR register for timer
        break;
        case 2:
        // initialize infantry/h-bridge clock
        __raw_writel(value			// new clock
                    | ATMEL_TC_WAVE					// output mode
                    | ATMEL_TC_ACPA_SET				// set TIOA low when counter reaches "A"
                    | ATMEL_TC_ACPC_CLEAR			// set TIOA high when counter reaches "C"
                    | ATMEL_TC_BCPB_SET				// set TIOB low when counter reaches "B"
                    | ATMEL_TC_BCPC_CLEAR			// set TIOB high when counter reaches "C"
                    | ATMEL_TC_EEVT_XC0				// set external clock 0 as the trigger
                    | ATMEL_TC_WAVESEL_UP_AUTO,		// reset counter when counter reaches "C"
                    tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], CMR));	// CMR register for timer
        break;

        default: return -EINVAL; break;
        }

    }
    return status;
}

//---------------------------------------------------------------------------
// Handles reads to the ra attribute through sysfs (for debugging only)
//---------------------------------------------------------------------------
static ssize_t hz_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    // find clock
    int rc = atomic_read(&tc_clock);
    switch (rc) {
       case 1: rc = 66500000; break;
       case 2: rc = 16625000; break;
       case 3: rc = 4156250; break;
       case 4: rc = 1039062; break;
       default: rc = 1; break;
    }
    rc = rc / __raw_readl(tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RC)); // convert to hz
    return sprintf(buf, "%d\n", rc);
    }

//---------------------------------------------------------------------------
// Handles writes to the ra attribute through sysfs (for debugging only)
//---------------------------------------------------------------------------
static ssize_t hz_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
    long value;
    ssize_t status;
    int ra, rc=1, nc, p; // ra, rc and new clock

delay_printk("Attempting to change hz\n");

    status = strict_strtol(buf, 0, &value);
    if (status == 0) {
        if (rc > 2000) { return 0;}
        if (rc < 200) { return 0;}

        nc = atomic_read(&tc_clock);
delay_printk("changing hz to %i\n", value);

     // find correct clock multiplier
     while (nc>0 && nc<5) {
       switch (nc) {
           case 1: rc = 66500000; break;
           case 2: rc = 16625000; break;
           case 3: rc = 4156250; break;
           case 4: rc = 1039062; break;
       }

       if (rc / value > 0xffff) {
           nc--; // clock too high for frequency
           // keep going
       } else if (rc / value < 1000) {
           nc++; // clock too low for frequency
           // keep going
       } else {
           break;
       }
     }
delay_printk("changing clock to %i\n", nc);

     switch (nc) {
         case 1: nc = ATMEL_TC_TIMER_CLOCK1; break;
         case 2: nc = ATMEL_TC_TIMER_CLOCK2; break;
         case 3: nc = ATMEL_TC_TIMER_CLOCK3; break;
         case 4: nc = ATMEL_TC_TIMER_CLOCK4; break;
         default :  // too big or too small
            return 0;
            break;
     }
     atomic_set(&tc_clock, nc); // set new clock value


     // find new ra value that is the same percentage as the old value
     ra = __raw_readl(tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
     p = __raw_readl(tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RC));
     ra = (p*1000) / ra; // get higher precision with 1000 multiplier

     // find and set new rc value
     rc = rc / value;
     MOTOR_PWM_RC[mover_type] = rc;
     MOTOR_PWM_END[mover_type] = rc;
     __raw_writel(MOTOR_PWM_RC[mover_type], tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RC));
     ra = (rc*1000) / ra; // get higher precision with 1000 multiplier
     __raw_writel(ra, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));

     // set new clock value
     switch (mover_type)
        {
        case 0:
        // initialize infantry clock
        __raw_writel(nc			// new clock
                    | ATMEL_TC_WAVE					// output mode
                    | ATMEL_TC_ACPA_CLEAR			// set TIOA low when counter reaches "A"
                    | ATMEL_TC_ACPC_SET				// set TIOA high when counter reaches "C"
                    | ATMEL_TC_BCPB_CLEAR			// set TIOB low when counter reaches "B"
                    | ATMEL_TC_BCPC_SET				// set TIOB high when counter reaches "C"
                    | ATMEL_TC_EEVT_XC0				// set external clock 0 as the trigger
                    | ATMEL_TC_WAVESEL_UP_AUTO,		// reset counter when counter reaches "C"
                    tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], CMR));	// CMR register for timer
        break;

        case 1:
        // initialize armor clock
        __raw_writel(nc			// new clock
                    | ATMEL_TC_WAVE					// output mode
                    | ATMEL_TC_ACPA_SET				// set TIOA high when counter reaches "A"
                    | ATMEL_TC_ACPC_CLEAR			// set TIOA low when counter reaches "C"
                    | ATMEL_TC_BCPB_SET				// set TIOB high when counter reaches "B"
                    | ATMEL_TC_BCPC_CLEAR			// set TIOB low when counter reaches "C"
                    | ATMEL_TC_EEVT_XC0				// set external clock 0 as the trigger
                    | ATMEL_TC_WAVESEL_UP_AUTO,		// reset counter when counter reaches "C"
                    tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], CMR));	// CMR register for timer
        break;
        case 2:
        // initialize infantry/h-bridge clock
        __raw_writel(nc			// new clock
                    | ATMEL_TC_WAVE					// output mode
                    | ATMEL_TC_ACPA_SET				// set TIOA low when counter reaches "A"
                    | ATMEL_TC_ACPC_CLEAR			// set TIOA high when counter reaches "C"
                    | ATMEL_TC_BCPB_SET				// set TIOB low when counter reaches "B"
                    | ATMEL_TC_BCPC_CLEAR			// set TIOB high when counter reaches "C"
                    | ATMEL_TC_EEVT_XC0				// set external clock 0 as the trigger
                    | ATMEL_TC_WAVESEL_UP_AUTO,		// reset counter when counter reaches "C"
                    tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], CMR));	// CMR register for timer
        break;

delay_printk("done\n");
        default: return -EINVAL; break;
        }

    }
delay_printk("failed with %i\n", status);
    return status;
}

//---------------------------------------------------------------------------
// Handles writes to the speed attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t speed_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    long value;
    ssize_t status;

    status = strict_strtol(buf, 0, &value);
    if ((status == 0) &&
           (value >= 0) &&
           (value <= NUMBER_OF_SPEEDS[mover_type]))
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
static DEVICE_ATTR(fault, 0444, fault_show, NULL);
static DEVICE_ATTR(speed, 0644, speed_show, speed_store);
static DEVICE_ATTR(ra, 0644, ra_show, ra_store);
static DEVICE_ATTR(rc, 0644, rc_show, rc_store);
static DEVICE_ATTR(clock, 0644, clock_show, clock_store);
static DEVICE_ATTR(hz, 0644, hz_show, hz_store);
static DEVICE_ATTR(position, 0444, position_show, NULL);
static DEVICE_ATTR(velocity, 0444, velocity_show, NULL);
static DEVICE_ATTR(rpm, 0444, rpm_show, NULL);
static DEVICE_ATTR(delta, 0444, delta_show, NULL);


//---------------------------------------------------------------------------
// Defines the attributes of the generic target mover for sysfs
//---------------------------------------------------------------------------
static const struct attribute * generic_mover_attrs[] =
    {
    &dev_attr_type.attr,
    &dev_attr_movement.attr,
    &dev_attr_fault.attr,
    &dev_attr_speed.attr,
    &dev_attr_ra.attr,
    &dev_attr_rc.attr,
    &dev_attr_clock.attr,
    &dev_attr_hz.attr,
    &dev_attr_position.attr,
    &dev_attr_velocity.attr,
    &dev_attr_rpm.attr,
    &dev_attr_delta.attr,
     NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the generic target mover for sysfs
//---------------------------------------------------------------------------
const struct attribute_group generic_mover_attr_group =
    {
    .attrs = (struct attribute **) generic_mover_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the generic target mover
//---------------------------------------------------------------------------
const struct attribute_group * generic_mover_get_attr_group(void)
    {
    return &generic_mover_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_mover_generic =
    {
    .type              = TARGET_TYPE_MOVER,
    .name              = NULL,
    .dev               = NULL,
    .get_attr_group    = generic_mover_get_attr_group,
    };

//---------------------------------------------------------------------------
// Work item to notify the user-space about positoin, velocity, delta, and movement
//---------------------------------------------------------------------------
static void do_position(struct work_struct * work)
        {
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }

        // only notify sysfs if we've move at least half a leg
        if (abs(atomic_read(&position_old) - atomic_read(&position)) > (TICKS_PER_LEG[mover_type]/TICKS_DIV/2))
            {
            atomic_set(&position_old, atomic_read(&position)); 
            do_event(EVENT_POSITION); // notify mover driver
            target_sysfs_notify(&target_device_mover_generic, "position");
            }
        }

static void do_velocity(struct work_struct * work)
        {
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
        target_sysfs_notify(&target_device_mover_generic, "velocity");
        target_sysfs_notify(&target_device_mover_generic, "rpm");
        }

static void do_delta(struct work_struct * work)
        {
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
        target_sysfs_notify(&target_device_mover_generic, "delta");
        }

static void movement_change(struct work_struct * work)
    {
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    target_sysfs_notify(&target_device_mover_generic, "movement");
    }

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_mover_generic_init(void)
    {
    int retval;

    // initialize hardware registers
    hardware_init();

    // initialize delayed work items
    INIT_WORK(&position_work, do_position);
    INIT_WORK(&velocity_work, do_velocity);
    INIT_WORK(&delta_work, do_delta);
    INIT_WORK(&movement_work, movement_change);

    // initialize sysfs structure
    target_device_mover_generic.name = TARGET_NAME[mover_type]; // set name in structure here as we can't initialize on a non-constant
    retval = target_sysfs_add(&target_device_mover_generic);

    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
    return retval;
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_mover_generic_exit(void)
    {
    atomic_set(&full_init, FALSE);
    ati_flush_work(&position_work); // close any open work queue items
    ati_flush_work(&velocity_work); // close any open work queue items
    ati_flush_work(&delta_work); // close any open work queue items
    ati_flush_work(&movement_work); // close any open work queue items
    hardware_exit();
    target_sysfs_remove(&target_device_mover_generic);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_mover_generic_init);
module_exit(target_mover_generic_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target mover generic module");
MODULE_AUTHOR("ndb");

