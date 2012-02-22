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

#include "netlink_kernel.h"
#include "target.h"
#include "fasit/faults.h"
#include "target_mover_generic.h"

#include "target_generic_output.h" /* for EVENT_### definitions */

//#define TESTING_ON_EVAL
//#define TESTING_MAX
//#define ACCEL_TEST
//#define WOBBLE_DETECT
//#define SPIN_DETECT 100
//#define STALL_DETECT
//#define DEBUG_PID

//---------------------------------------------------------------------------
// These variables are parameters giving when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
static int mover_type = 1; // 0 = infantry/36v KBB controller, 1 = armor, 2 = infantry/36v KDZ controller, 3 = error
module_param(mover_type, int, S_IRUGO);
static int kp_m = -1, kp_d = -1,  ki_m = -1, ki_d = -1, kd_m = -1, kd_d = -1, min_effort = -1, max_accel = -1, max_deccel = -1;
module_param(kp_m, int, S_IRUGO);
module_param(kp_d, int, S_IRUGO);
module_param(ki_m, int, S_IRUGO);
module_param(ki_d, int, S_IRUGO);
module_param(kd_m, int, S_IRUGO);
module_param(kd_d, int, S_IRUGO);
module_param(min_effort, int, S_IRUGO);
module_param(max_accel, int, S_IRUGO);
module_param(max_deccel, int, S_IRUGO);

static char* TARGET_NAME[] = {"old infantry mover","armor mover","48v infantry mover","48v soft-reverse infantry mover","error"};
static char* MOVER_TYPE[] = {"infantry","armor","infantry","infantry","error"};

// continue moving on leg or quad interrupt or neither
static int CONTINUE_ON[] = {2,1,2,2,0}; // leg = 1, quad = 2, both = 3, neither = 0

// TODO - replace with a table based on distance and speed?
static int TIMEOUT_IN_MSECONDS[] = {1000,12000,1000,1000,0};
static int MOVER_DELAY_MULT[] = {6,2,6,6,0};

#define MOVER_POSITION_START 		0
#define MOVER_POSITION_BETWEEN		1	// not at start or end
#define MOVER_POSITION_END			2

#define MOVER_DIRECTION_STOP		0
#define MOVER_DIRECTION_FORWARD		1
#define MOVER_DIRECTION_REVERSE		2
#define MOVER_DIRECTION_STOPPED_FAULT	3

#define MOVER_SENSOR_UNKNOWN  0
#define MOVER_SENSOR_HOME  1
#define MOVER_SENSOR_END  2

// the maximum allowed speed ticks
//static int NUMBER_OF_SPEEDS[] = {10,20,10,10,0};
static int NUMBER_OF_SPEEDS[] = {100,200,200,200,0};
static int MIN_SPEED[] = {15,20,10,10,0}; // minimum acceptible input speed (15 =1 1/2 mph, 20 = 2 mph)

// horn on and off times (off is time to wait after mover starts moving before going off)
static int HORN_ON_IN_MSECONDS[] = {0,3500,0,0,0};
static int HORN_OFF_IN_MSECONDS[] = {0,8000,0,0,0};

// the paremeters of the velocity ramp up
#ifdef TESTING_MAX
static int RAMP_UP_TIME_IN_MSECONDS[] = {1,1,1,1,0};
static int RAMP_DOWN_TIME_IN_MSECONDS[] = {1,1,1,1,0};
#else
static int RAMP_UP_TIME_IN_MSECONDS[] = {5,1000,5,250,0};
static int RAMP_DOWN_TIME_IN_MSECONDS[] = {1,100,1,200,0};
#endif
//static int RAMP_STEPS[] = {25,100,25,25,0};
static int RAMP_STEPS[] = {3,5,3,5,0};
static int IGNORE_RAMP[] = {0,0,0,0,0}; // MITs currently completely ignore the ramp function
static int RAMP_MIN_SPEED[] = {0,0,0,0,0}; // minimum speed to start ramping from

// the parameters needed for PID speed control
// old method
#if 0
static int PID_HZ[]        = {20,100,20,20,1};
static int PID_DELTA_T[]   = {50,10,50,50,1}; // inversely proportial to HZ (ie 100 dt = 1000 ms / 100 hz)
static int PID_GAIN_MULT[] = {10,1000,1,1,0}; // proportional gain numerator
static int PID_GAIN_DIV[]  = {275,1000,15,15,1}; // proportional gain denominator
static int PID_TI_MULT[]   = {25,1,2500,2500,1}; // time integral numerator
static int PID_TI_DIV[]    = {1,1,1,1,1}; // time integral denominator
static int PID_TD_MULT[]   = {1,1,2,1,1}; // time derivitive numerator
static int PID_TD_DIV[]    = {10,1,1,1,1}; // time derivitive denominator
#endif
// new method - used Ziegler-Nichols method to determine (found Ku of 1, Tu of 0.9 seconds:29490 ticks off track on type 0, tested on type 2 on track) -- standard
// new method - used Ziegler-Nichols method to determine (found Ku of 2/3, Tu of 1.5 seconds:49152 ticks off track on type 1) -- standard
// static int PID_KP_MULT[]   = {3, 2, 3, 3, 0}; // proportional gain numerator
// static int PID_KP_DIV[]    = {5, 5, 5, 5, 0}; // proportional gain denominator
// static int PID_KI_MULT[]   = {2, 1, 2, 5, 0}; // integral gain numerator
// static int PID_KI_DIV[]    = {24575, 61440, 24575, 24575, 0}; // integral gain denominator
// static int PID_KD_MULT[]   = {8847, 12288, 8847, 8847, 0}; // derivitive gain numerator
// static int PID_KD_DIV[]    = {4, 5, 4, 4, 0}; // derivitive gain denominator
// new method - used Ziegler-Nichols method to determine (found Ku of 1, Tu of 0.9 seconds:29490 ticks off track on type 0, tested on type 2 on track) -- no overshoot (36v MIT has ku of 4/3 and tu of .29)
// new method - used Ziegler-Nichols method to determine (found Ku of 2/3, Tu of 1.5 seconds:49152 ticks off track on type 1) -- no overshoot -- due to throttle cut-off from motor controller, had to adjust by hand afterwards
static int PID_KP_MULT[]   = {1, 2, 1, 1, 0}; // proportional gain numerator
static int PID_KP_DIV[]    = {4, 15, 3, 3, 0}; // proportional gain denominator
static int PID_KI_MULT[]   = {15, 1, 15, 15, 0}; // integral gain numerator
static int PID_KI_DIV[]    = {475150, 184320, 47515, 47515, 0}; // integral gain denominator
static int PID_KD_MULT[]   = {190060, 32768, 190060, 190060, 0}; // derivitive gain numerator
static int PID_KD_DIV[]    = {15, 15, 15, 15, 0}; // derivitive gain denominator
#ifdef TESTING_MAX
static int MIN_EFFORT[]    = {1000, 1000, 1000, 1000, 0}; // minimum effort given to ensure motor moves
#else
static int MIN_EFFORT[]    = {115, 175, 1, 1, 0}; // minimum effort given to ensure motor moves
#endif
static int SPEED_AFTER[]   = {1, 3, 1, 1, 0}; // clamp effort if we hit this many correct values in a row
static int SPEED_CHANGE[]  = {40, 30, 40, 1, 0}; // unclamp if the error is bigger than this
static int ADJUST_PID_P[]  = {0, 0, 0, 0, 0}; // adjust MIT's proportional gain as percentage of final speed / max speed
static int MAX_ACCEL[]     = {500, 1000, 1000, 1000, 0}; // maximum effort change in one step
static int MAX_DECCEL[]     = {500, 1000, 1000, 1000, 0}; // maximum effort change in one step

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

static int MOTOR_PWM_FWD[] = {OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,0}; // would be OUTPUT_MOVER_MOTOR_FWD_POS if PWM on H-bridge
static int MOTOR_PWM_REV[] = {OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,0}; // would be OUTPUT_MOVER_MOTOR_REV_POS if PWM on H-bridge
#define MOTOR_PWM_F (reverse ? MOTOR_PWM_REV[mover_type] : MOTOR_PWM_FWD[mover_type])
#define MOTOR_PWM_R (reverse ? MOTOR_PWM_FWD[mover_type] : MOTOR_PWM_REV[mover_type])

// RC - max time (allowed by PWM code)
// END - max time (allowed by me to account for max voltage desired by motor controller : 90% of RC)
// RA - low time setting - cannot exceed RC
// RB - low time setting - cannot exceed RC
static int MOTOR_PWM_RC[] = {0x1180,0x3074,0x1180,0x1180,0};
static int MOTOR_PWM_END[] = {0x1180,0x3074,0x1180,0x1180,0};
//static int MOTOR_PWM_RA_DEFAULT[] = {0x0320,0x04D8,0x0000,0x0000,0};
//static int MOTOR_PWM_RB_DEFAULT[] = {0x0320,0x04D8,0x0000,0x0000,0};
static int MOTOR_PWM_RA_DEFAULT[] = {0x0320,0x0001,0x0001,0x001,0};
static int MOTOR_PWM_RB_DEFAULT[] = {0x0320,0x0001,0x0001,0x001,0};

// TODO - map pwm output pin to block/channel
#define PWM_BLOCK				1				// block 0 : TIOA0-2, TIOB0-2 , block 1 : TIOA3-5, TIOB3-5
static int MOTOR_PWM_CHANNEL[] = {1,1,1,1,0};		// channel 0 matches TIOA0 to TIOB0, same for 1 and 2
#define ENCODER_PWM_CHANNEL		0				// channel 0 matches TIOA0 to TIOB0, same for 1 and 2

#define MAX_TIME	0x10000
#define MAX_OVER	0x10000
static int RPM_K[] = {1966080, 1966080, 1966080, 1966080, 0}; // CLOCK * 60 seconds
static int ENC_PER_REV[] = {2, 360, 2, 2, 0}; // 2 = encoder click is half a revolution, or 360 ticks per revolution
static int VELO_K[] = {1680, 1344, 1680, 1680, 0}; // rpm/mph*10
static int INCHES_PER_TICK[] = {628, 786, 628, 628, 0}; // 5:1 ratio 10 inch, 2:1 ratio 5 inch, etc.
static int TICKS_PER_LEG[] = {2292, 1833, 2292, 2292, 0}; // 5:1 ratio 10 inch wheel 6 ft leg, 2:1 ratio 5 inch wheel 6 ft leg, etc.
#define TICKS_DIV 100

// to keep updates to the file system in check somewhat
#define POSITION_DELAY_IN_MSECONDS	200
#define VELOCITY_DELAY_IN_MSECONDS	200

// speed charts (TODO -- update with mover on track with load)
// static in MOVER0_PWM_TABLE = ?
// static in MOVER1_PWM_TABLE = ?
// static int MOVER2_PWM_TABLE[] = {0, 1100, 1550, 1975, 2375, 2800, 3325, 3900, 5000, 7000, 10000}; // -- first stab
// static int MOVER2_PWM_TABLE[] = {0, 1350, 1800, 2300, 2700, 3200, 3700, 4500, 5500, 7500, 12000}; // -- second stab



// external motor controller polarity
static int OUTPUT_MOVER_PWM_SPEED_ACTIVE[] = {ACTIVE_HIGH,ACTIVE_LOW,ACTIVE_LOW,ACTIVE_LOW,0};

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

static bool PWM_H_BRIDGE[] = {false,false,false,false,false};
static bool DIRECTIONAL_H_BRIDGE[] = {false,true,true,false,false};
static bool USE_BRAKE[] = {true,false,false,true,false};
static bool CONTACTOR_H_BRIDGE[] = {false, false, false, true, false};
static bool MOTOR_CONTROL_H_BRIDGE[] = {true, false, false, false, false};

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
atomic_t found_home = ATOMIC_INIT(0);
atomic_t velocity = ATOMIC_INIT(0);
atomic_t last_t = ATOMIC_INIT(0);
atomic_t o_count = ATOMIC_INIT(0);
atomic_t delta_t = ATOMIC_INIT(0);
atomic_t position = ATOMIC_INIT(0);
atomic_t position_old = ATOMIC_INIT(0);
atomic_t velocity_old = ATOMIC_INIT(0);
atomic_t legs = ATOMIC_INIT(0);
atomic_t quad_direction = ATOMIC_INIT(0);
atomic_t doing_pos = ATOMIC_INIT(FALSE);
atomic_t doing_vel = ATOMIC_INIT(FALSE);
atomic_t tc_clock = ATOMIC_INIT(2);

// This is used to know which sensor was last set so we can determine
// invalid direction request. If we are home we do not want to 
// allow reverse.
atomic_t last_sensor = ATOMIC_INIT(MOVER_SENSOR_UNKNOWN);

// This is used to determine if we have exceeded the track length as
// determined by the home and end flags.
atomic_t internal_length = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that an operation is in progress,
// i.e. the mover has been commanded to move. It is used to synchronize
// user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t moving_atomic = ATOMIC_INIT(0); // 0 = not moving, 1 = told to move, 2 = started moving, 3 = detected movement

//---------------------------------------------------------------------------
// This atomic variable is use to remember a speed of reverse direction to
// the current direction (so we don't kill the mover by slamming it in rev)
//---------------------------------------------------------------------------
atomic_t reverse_speed = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

// old method
#if 0
//---------------------------------------------------------------------------
// This atomic variable is for our registration with netlink_provider
//---------------------------------------------------------------------------
atomic_t driver_id = ATOMIC_INIT(-1);
#endif

// new method
//---------------------------------------------------------------------------
// Forward definition of pid function
//---------------------------------------------------------------------------
static void pid_step(int delta_t);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are awake/asleep
//---------------------------------------------------------------------------
atomic_t sleep_atomic = ATOMIC_INIT(0); // not sleeping

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
//static int speed_from_pwm(int ra);		// absolute velocity, calculated
//static int pwm_from_speed(int speed);	// best-guess pwm, calculated
//static int current_speed(void);			// velocity/direction, measured
static int current_speed10(void);		// 10 * velocity/direction, measured

//---------------------------------------------------------------------------
// Declaration of functions related to effort/speed/pwm conversions
//---------------------------------------------------------------------------
static int percent_from_speed(int speed);	// 1000 * percent, calculated
static int pwm_from_effort(int effort);		// absolute pwm, calculated

//---------------------------------------------------------------------------
// Variables for PID speed control
//---------------------------------------------------------------------------
spinlock_t pid_lock = SPIN_LOCK_UNLOCKED;
#define MAX_PID_ERRORS 5
int pid_errors[MAX_PID_ERRORS];  // previous errors (fully calculated for use in summing integral part of PID)
int pid_last_effort = 0;			// prior effort
int pid_last_error = 0;				// prior error
int pid_error = 0;					// current error
int pid_correct_count = 0;       // number of correct speeds (used to clamp effort)
atomic_t pid_set_point = ATOMIC_INIT(0); // set point for speed to maintain

#ifdef WOBBLE_DETECT
int pid_last_speeds[3];          // prior speeds (for detecting wobble)
struct timespec pid_last_time;   // prior peak time (for detecting wobble)
#endif
struct timespec time_start;   // time module was loaded

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
// Message filler handler for failure messages
//---------------------------------------------------------------------------
int error_mfh(struct sk_buff *skb, void *msg) {
    // the msg argument is a null-terminated string
    return nla_put_string(skb, GEN_STRING_A_MSG, msg);
}

//---------------------------------------------------------------------------
// Starts the timeout timer.
//---------------------------------------------------------------------------
static void timeout_timer_start(int mult) {
    if (mult <= 1) {
        // standard timer
        mod_timer(&timeout_timer_list, jiffies+((TIMEOUT_IN_MSECONDS[mover_type]*HZ)/1000));
    } else {
        // (timer + horn) * mult
        mod_timer(&timeout_timer_list, jiffies+((mult*(HORN_ON_IN_MSECONDS[mover_type]+TIMEOUT_IN_MSECONDS[mover_type])*HZ)/1000));
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
static move_event_callback move_fault_callback = NULL;
void set_move_callback(move_event_callback handler, move_event_callback faulthandler) {
    // only allow setting the callback once
    if (handler != NULL && move_callback == NULL) {
        move_callback = handler;
        delay_printk("GENERIC MOVER: Registered callback function for move events\n");
    }
    if (faulthandler != NULL && move_fault_callback == NULL) {
        move_fault_callback = faulthandler;
        delay_printk("GENERIC MOVER: Registered callback function for move faults\n");
    }
}
EXPORT_SYMBOL(set_move_callback);

static void do_event(int etype) {
#ifndef DEBUG_PID
    if (move_callback != NULL) {
        move_callback(etype);
    }
#endif
}

static void do_fault(int etype) {
#ifndef DEBUG_PID
    if (move_fault_callback != NULL) {
        move_fault_callback(etype);
    }
#endif
}

//---------------------------------------------------------------------------
// a request to turn the motor on
//---------------------------------------------------------------------------
static int hardware_motor_on(int direction)
    {
    // turn on directional lines
    if (DIRECTIONAL_H_BRIDGE[mover_type])
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
        if (CONTACTOR_H_BRIDGE[mover_type] && !MOTOR_CONTROL_H_BRIDGE[mover_type]) {
#ifndef DEBUG_PID
//          send_nl_message_multi("Contacter on!", error_mfh, NL_C_FAILURE);
#endif
            at91_set_gpio_output(OUTPUT_MOVER_MOTOR_REV_POS, OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE); // main contacter on
        }
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

    // did we just start moving from a stop?
    if (current_speed10() == 0) {	//changed from current_speed
        // turn on horn and wait to do actual move
        del_timer(&horn_on_timer_list); // start horn timer over
        if (HORN_ON_IN_MSECONDS[mover_type] > 0)
            {
#ifndef DEBUG_PID
//send_nl_message_multi("Horn on!", error_mfh, NL_C_FAILURE);
#endif
            at91_set_gpio_output(OUTPUT_MOVER_HORN, OUTPUT_MOVER_HORN_ACTIVE_STATE);
            mod_timer(&horn_on_timer_list, jiffies+((HORN_ON_IN_MSECONDS[mover_type]*HZ)/1000));
            }
        else
            {
            mod_timer(&horn_on_timer_list, jiffies+((10*HZ)/1000));
            }
    }


    // log and set direction
    if (direction == MOVER_DIRECTION_REVERSE)
        {
        if (PWM_H_BRIDGE[mover_type]) {
            at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        } else if (DIRECTIONAL_H_BRIDGE[mover_type]) {
#ifndef DEBUG_PID
          //send_nl_message_multi("Reverse contacter on!", error_mfh, NL_C_FAILURE);
#endif
            at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        }

        if (!CONTACTOR_H_BRIDGE[mover_type]) {
        // assert pwm line
        #if PWM_BLOCK == 0
            at91_set_A_periph(MOTOR_PWM_R, PULLUP_OFF);
        #else
            at91_set_B_periph(MOTOR_PWM_R, PULLUP_OFF);
        #endif
        }

        atomic_set(&movement_atomic, MOVER_DIRECTION_REVERSE);
       delay_printk("%s - %s() - reverse\n",TARGET_NAME[mover_type], __func__);
        }
    else if (direction == MOVER_DIRECTION_FORWARD)
        {
        if (PWM_H_BRIDGE[mover_type]) {
            at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        } else if (DIRECTIONAL_H_BRIDGE[mover_type]) {
#ifndef DEBUG_PID
          //send_nl_message_multi("Forward contacter on!", error_mfh, NL_C_FAILURE);
#endif
            at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        }

        if (!CONTACTOR_H_BRIDGE[mover_type]) {
        #if PWM_BLOCK == 0
            at91_set_A_periph(MOTOR_PWM_F, PULLUP_OFF);
        #else
            at91_set_B_periph(MOTOR_PWM_F, PULLUP_OFF);
        #endif
        }

        atomic_set(&movement_atomic, MOVER_DIRECTION_FORWARD);
       delay_printk("%s - %s() - forward\n",TARGET_NAME[mover_type], __func__);
        }
    else
        {
       delay_printk("%s - %s() - error\n",TARGET_NAME[mover_type], __func__);
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
#ifndef DEBUG_PID
//send_nl_message_multi("Brake on!", error_mfh, NL_C_FAILURE);
#endif
    at91_set_gpio_output(MOTOR_PWM_F, !OUTPUT_MOVER_PWM_SPEED_ACTIVE[mover_type]);
    at91_set_gpio_output(MOTOR_PWM_R, !OUTPUT_MOVER_PWM_SPEED_ACTIVE[mover_type]);
    __raw_writel(1, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA)); // change to smallest value
    __raw_writel(1, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB)); // change to smallest value

    // pid stop
    pid_last_effort = 0;

    // turn on brake?
    if (USE_BRAKE[mover_type])
        {
        at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);
        }

    atomic_set(&movement_atomic, MOVER_DIRECTION_STOP);

    // turn off directional lines
    if (DIRECTIONAL_H_BRIDGE[mover_type])
        {
#ifndef DEBUG_PID
          //send_nl_message_multi("Directional contacters off!", error_mfh, NL_C_FAILURE);
#endif
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
        if (CONTACTOR_H_BRIDGE[mover_type] && !MOTOR_CONTROL_H_BRIDGE[mover_type]) {
#ifndef DEBUG_PID
//          send_nl_message_multi("Contacter off!", error_mfh, NL_C_FAILURE);
#endif
            at91_set_gpio_output(OUTPUT_MOVER_MOTOR_REV_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE); // main contacter off
        }
        }

    return 0;
    }

//---------------------------------------------------------------------------
// sets up a ramping change in speed
//---------------------------------------------------------------------------
static int hardware_speed_set(int new_speed)
    {
    int old_speed, ra, ramp_time=RAMP_UP_TIME_IN_MSECONDS[mover_type];
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

    // set to minimum
    if (new_speed <= 0) {
        new_speed = 0;
    } else if (new_speed < MIN_SPEED[mover_type]) {
        new_speed = MIN_SPEED[mover_type];
    }

    // check to see if we need to ramp if we're already moving
    if (atomic_read(&moving_atomic) > 0)
        {
        // don't ramp if we're already going to the requested speed
        old_speed = atomic_read(&goal_atomic);
        if (old_speed == new_speed)
            {
#ifndef DEBUG_PID
//send_nl_message_multi("Ignored speed, was already going to that speed", error_mfh, NL_C_FAILURE);
#endif
            return TRUE;
            }
        }

    // start ramp up
    old_speed = atomic_read(&speed_atomic);
    if (new_speed != old_speed)
        {
        atomic_set(&goal_atomic, new_speed); // reset goal speed
////        atomic_set(&goal_start_atomic, old_speed); // reset ramp start
//        ra = __raw_readl(tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA)); // read current raw pwm value
//        ra = speed_from_pwm(ra); // calculate speed from pwm value
//        atomic_set(&speed_atomic, ra); // reset current speed to actual speed
//        atomic_set(&goal_start_atomic, ra); // reset ramp start to actual speed
       ra = abs(current_speed10());  //changed from current_speed
       ra = max(ra, RAMP_MIN_SPEED[mover_type]); // use minimum of measured and minimum ramp speed
       atomic_set(&goal_start_atomic, ra); // reset ramp start to actual measured speed
       delay_printk("%s - %s : old (%i), new (%i), are(%i)\n",TARGET_NAME[mover_type], __func__, old_speed, new_speed, ra);
        del_timer(&ramp_timer_list); // start ramp over
        if (new_speed < old_speed)
            {
            atomic_set(&goal_step_atomic, -1); // stepping backwards
            ramp_time=RAMP_DOWN_TIME_IN_MSECONDS[mover_type];
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

#ifndef DEBUG_PID
//send_nl_message_multi("Might do ramp while moving", error_mfh, NL_C_FAILURE);
#endif
        // are we being told to change movement?
        if (atomic_read(&moving_atomic) >= 1)
            {
#ifndef DEBUG_PID
//send_nl_message_multi("Going to do ramp while moving", error_mfh, NL_C_FAILURE);
#endif
            // start the ramp up/down of the mover immediately
            mod_timer(&ramp_timer_list, jiffies+(((10*HZ)/1000)));
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

    // reset PID set point
    atomic_set(&pid_set_point, 0);

    // reset velocity
    atomic_set(&velocity, 0);

// new method
    // reset PID errors and update internal pid variables
    memset(pid_errors, 0, sizeof(int)*MAX_PID_ERRORS);
#ifdef WOBBLE_DETECT
    memset(pid_last_speeds, 0, sizeof(int)*3);
    pid_last_time = current_kernel_time();
#endif
    pid_step(MAX_TIME); // use maximum delta_t

    // turn off the motor
    hardware_motor_off();

    // signal that an operation is done
#ifndef DEBUG_PID
//send_nl_message_multi("\n---------Changing moving_atomic to 0\n", error_mfh, NL_C_FAILURE);
#endif
    atomic_set(&moving_atomic, 0);

    // reset speed ramping
    delay_printk("hardware_movement_stop - speed: %i\n", speed);
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

    do_event(EVENT_TIMED_OUT); // reached timeout
    
    if (atomic_read(&goal_atomic) == 0) {
       do_event(EVENT_STOPPED); // timeout was part of coasting or stopping
    } else {
       do_event(EVENT_ERROR); // timeout wasn't part of coasting
       do_fault(ERR_no_movement);
    }

    hardware_movement_stop(FALSE);

    if (atomic_read(&goal_atomic) == 0) {
       int rev_speed = atomic_read(&reverse_speed);
       if (rev_speed != 0) {
//          char *msg = kmalloc(128, GFP_KERNEL);
//          snprintf(msg, 128, "Finishing reverse (%i)", current_speed10());
//          send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
//          kfree(msg);
          // finish reversing now
          mover_speed_set(rev_speed);
       }
    }
    }

//---------------------------------------------------------------------------
// passed a leg sensor
//---------------------------------------------------------------------------
irqreturn_t leg_sensor_int(int irq, void *dev_id, struct pt_regs *regs)
    {
//    return IRQ_HANDLED;
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
#ifdef ACCEL_TEST
    int pos;
#endif
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
// new method
        // no longer considered moving, ramp will adjust accordingly if it's still running
        if (atomic_read(&moving_atomic) == 3) {
#ifndef DEBUG_PID
//send_nl_message_multi("\n---------Changing moving_atomic to 2 from 3\n", error_mfh, NL_C_FAILURE);
#endif
            atomic_set(&moving_atomic, 2);
        }

        // call pid_step to change motor at each input of encoder
        pid_step(MAX_TIME);
        }

    // Pin A going high caused IRQ?
    if ( status & ATMEL_TC_LDRAS ) {
//    if (at91_get_gpio_value(INPUT_MOVER_SPEED_SENSOR_1) == NPUT_MOVER_SPEED_SENSOR_ACTIVE_STATE)
//        {
        // reset the timeout timer?
        if (CONTINUE_ON[mover_type] & 2) {
            timeout_timer_stop();
            timeout_timer_start(1);
        }

        // now considered moving, ramp will now only adjust the PID number
#ifndef DEBUG_PID
//send_nl_message_multi("\n---------Changing moving_atomic to 3\n", error_mfh, NL_C_FAILURE);
#endif
        atomic_set(&moving_atomic, 3);

        // detect direction, change position
        if (reverse) {
            // reverse flag set
            if (status & ATMEL_TC_MTIOB) {
                // wheel going backwards, but reversed, so we're going forward
                atomic_inc(&position);
                atomic_set(&quad_direction, 1);
            } else {
                // wheel going forwards, but reversed, so we're going backwards
                atomic_dec(&position);
                atomic_set(&quad_direction, -1);
            }
        } else {
            // reverse flag not set
            if (status & ATMEL_TC_MTIOB) {
                // wheel going backwards, so we're going reversed
                atomic_dec(&position);
                atomic_set(&quad_direction, -1);
            } else {
                // wheel going forward, so we're going forward
                atomic_inc(&position);
                atomic_set(&quad_direction, 1);
            }
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
#ifdef ACCEL_TEST
        // did we reach the target speed?
        if (abs(current_speed10()) >= 15) { // 7.45 mph is 12 kph
            // display current position
            pos = ((INCHES_PER_TICK[mover_type]*atomic_read(&position))/(ENC_PER_REV[mover_type]*TICKS_DIV));
            delay_printk("\
#************************************#\n\
# Reached 14kph @ %2i feet, %2i inches #\n\
#************************************#\n\
", pos/12, pos%12);
            // stop mover
            mover_speed_stop();
        if (atomic_read(&doing_pos) == FALSE)
            {
            atomic_set(&doing_pos, TRUE);
            do_event(EVENT_POSITION); // notify mover driver
            mod_timer(&position_timer_list, jiffies+((1*HZ)/1000)); // do as quickly as possible
            }
        if (atomic_read(&doing_vel) == FALSE)
            {
            atomic_set(&doing_vel, TRUE);
            do_event(EVENT_MOVING); // notify mover driver
            mod_timer(&velocity_timer_list, jiffies+((1*HZ)/1000)); // do as quickly as possible
            }
        } else {
#endif
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
#ifdef ACCEL_TEST
        // we only do updates to position and velocity in the acceleration test when we reach the target speed...this is the end of the else
        }
#endif
// new method
        // call pid_step to change motor at each input of encoder
        pid_step(this_t);
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
    if (atomic_read(&found_home) == 0) {
#ifndef DEBUG_PID
send_nl_message_multi("Did home reset", error_mfh, NL_C_FAILURE);
#endif
        atomic_set(&found_home, 1); // only "home" the mover once per reset
        atomic_set(&legs, 0);
        atomic_set(&position, 0);
#ifndef DEBUG_PID
    } else {
send_nl_message_multi("Ignored home reset", error_mfh, NL_C_FAILURE);
#endif
    }

    // check to see if this one needs to be ignored
    if (atomic_read(&movement_atomic) == MOVER_DIRECTION_FORWARD)
        {
        // ...then ignore switch
        return IRQ_HANDLED;
        }

    atomic_set(&last_sensor, MOVER_SENSOR_HOME); // home sensor
    do_event(EVENT_HOME_LIMIT); // triggered on home limit
    do_fault(ERR_stop_left_limit); // triggered on home limit
    do_event(EVENT_STOP); // started stopping
#ifndef TESTING_MAX
    atomic_set(&goal_atomic, 0); // reset goal speed
    hardware_movement_stop(FALSE);
#endif

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

   // If we have not set our internal track length, set it now if we can
   // so we can turn off the motor after we have traveled more that this distance
   // plus a fudge factor.
   if (atomic_read(&internal_length) == 0){
      atomic_set(&internal_length, atomic_read(&position));
   }

    atomic_set(&last_sensor, MOVER_SENSOR_END); // end sensor
    do_event(EVENT_END_LIMIT); // triggered on end limit
    do_fault(ERR_stop_right_limit); // triggered on home limit
    do_event(EVENT_STOP); // started stopping
#ifndef TESTING_MAX
    atomic_set(&goal_atomic, 0); // reset goal speed
    hardware_movement_stop(FALSE);
#endif

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
static void hardware_pwm_exit(void) {

   // disable clock registers
   __raw_writel(ATMEL_TC_CLKDIS, tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, CCR));

   // disable specific interrupts for the encoder A
   __raw_writel(ATMEL_TC_COVFS                            // interrupt on counter overflow
              | ATMEL_TC_LDRAS,                           // interrupt on loading RA
                tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, IDR)); // interrupt disable register

   // free softwrae irqs
   free_irq(tc->irq[ENCODER_PWM_CHANNEL], NULL);

   // disable clocks
   clk_disable(tc->clk[ENCODER_PWM_CHANNEL]);

   // free timer counter
   target_timer_free(tc);
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
        case 3:
        // initialize infantry/48v soft-reverse clock
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
        // initialize infantry/48-v hard-reverse clock
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
    if (DIRECTIONAL_H_BRIDGE[mover_type]) {
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
#ifndef TESTING_ON_EVAL
    if ((hardware_set_gpio_input_irq(INPUT_MOVER_TRACK_HOME, INPUT_MOVER_END_OF_TRACK_PULLUP_STATE, track_sensor_home_int, "track_sensor_home_int") == FALSE) ||
        (hardware_set_gpio_input_irq(INPUT_MOVER_TRACK_END, INPUT_MOVER_END_OF_TRACK_PULLUP_STATE, track_sensor_end_int, "track_sensor_end_int") == FALSE))
        {
        return FALSE;
        }
#endif

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
    if (DIRECTIONAL_H_BRIDGE[mover_type]) {
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

#ifndef TESTING_ON_EVAL
    free_irq(INPUT_MOVER_TRACK_HOME, NULL);
    free_irq(INPUT_MOVER_TRACK_END, NULL);
#endif
    free_irq(INPUT_MOVER_TRACK_SENSOR_1, NULL);

    hardware_pwm_exit();

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
#ifndef DEBUG_PID
//send_nl_message_multi("\n---------Changing moving_atomic to 1\n", error_mfh, NL_C_FAILURE);
#endif
        atomic_set(&moving_atomic, 1);

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

    // signal that an operation is in progress
#ifndef DEBUG_PID
//send_nl_message_multi("\n---------Changing moving_atomic to 2\n", error_mfh, NL_C_FAILURE);
#endif
    atomic_set(&moving_atomic, 2); // now considered moving

    // start the ramp up/down of the mover
    mod_timer(&ramp_timer_list, jiffies+((10*HZ)/1000));


    // setup PWM for nothing at start (disables brakes on MAT?)
#ifndef DEBUG_PID
//send_nl_message_multi("Brake off!", error_mfh, NL_C_FAILURE);
#endif
    __raw_writel(1, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA)); // change to smallest value
    __raw_writel(1, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB)); // change to smallest value

    // turn off brake?
    if (USE_BRAKE[mover_type])
        {
        at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, !OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);
        }

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
#ifndef DEBUG_PID
//send_nl_message_multi("Horn off!", error_mfh, NL_C_FAILURE);
#endif
   delay_printk("%s - %s()\n",TARGET_NAME[mover_type], __func__);

    // turn off horn?
    if (HORN_ON_IN_MSECONDS[mover_type] > 0)
        {
        at91_set_gpio_output(OUTPUT_MOVER_HORN, !OUTPUT_MOVER_HORN_ACTIVE_STATE);
        }
    }

//---------------------------------------------------------------------------
// Helper function to map 1000*percent values from speed values
//---------------------------------------------------------------------------
static int percent_from_speed(int speed) {
    // limit to max speed
    if (speed > NUMBER_OF_SPEEDS[mover_type]) {
        speed = NUMBER_OF_SPEEDS[mover_type];
    }
    // limit to min speed
    if (speed < 0) {
        speed = 0; // limit to zero
    }
    return (1000*speed/NUMBER_OF_SPEEDS[mover_type]); // 1000 * percent = 0 to 1000
}

//---------------------------------------------------------------------------
// Helper function to map pwm values from effort values
//---------------------------------------------------------------------------
static int pwm_from_effort(int effort) {
    // limit to max effort
    if (effort > 1000) {
        effort = 1000;
    }
    // limit to min effort
    if (effort < 0) {
        effort = 0;
    }
    // use straight percentage
    return MOTOR_PWM_RB_DEFAULT[mover_type] + (
      (effort * (MOTOR_PWM_END[mover_type] - MOTOR_PWM_RB_DEFAULT[mover_type]))
      / 1000);
}

//---------------------------------------------------------------------------
// Helper function to map pwm values to speed values
//---------------------------------------------------------------------------
#if 0
static int pwm_from_speed(int speed) {
    delay_printk("pwm_from_speed(%i)\n", speed);
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
#endif

//---------------------------------------------------------------------------
// Helper function to map speed values to pwm values
//---------------------------------------------------------------------------
#if 0
static int speed_from_pwm(int ra) {
    int i;
    delay_printk("speed_from_pwm(%i)\n", ra);
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
#endif

//---------------------------------------------------------------------------
// Helper function to get current measured speed (slight lag behind actual speed)
//---------------------------------------------------------------------------
/*static int current_speed() {
    return current_speed10()/10;
}*/

static int current_speed10() {
    int vel = atomic_read(&velocity);
//    if (vel != 0) {
//        vel = (100*RPM_K[mover_type]/vel)/VELO_K[mover_type];
//    }
    if (vel > 0) {
        vel = (100*(RPM_K[mover_type]/ENC_PER_REV[mover_type])/vel)/VELO_K[mover_type];
    } else if (vel < 0) {
        vel = -1 * ((100*(RPM_K[mover_type]/ENC_PER_REV[mover_type])/abs(vel))/VELO_K[mover_type]);
    }

    return vel;
}

static int mover_speed_reverse(int speed) {
//   send_nl_message_multi("Started reversing...", error_mfh, NL_C_FAILURE);
   atomic_set(&reverse_speed, speed);
   if (RAMP_DOWN_TIME_IN_MSECONDS[mover_type] > 1) {
#ifndef DEBUG_PID
          //send_nl_message_multi("Slow reverse", error_mfh, NL_C_FAILURE);
#endif
      // coast if we're ramping
      hardware_speed_set(0);
   } else {
#ifndef DEBUG_PID
          //send_nl_message_multi("Quick reverse", error_mfh, NL_C_FAILURE);
#endif
      // stop "now" if we're not
      atomic_set(&goal_atomic, 0); // reset goal speed
      hardware_movement_stop(FALSE);
   }
   return 1;
}

//---------------------------------------------------------------------------
// Get/set functions for external control from within kernel
//---------------------------------------------------------------------------
int mover_speed_get(void) {
   delay_printk("mover_speed_get\n");
   return current_speed10();  // changed from current_speed
}
EXPORT_SYMBOL(mover_speed_get);

int mover_speed_set(int speed) {
   int o_speed = current_speed10();

   // check to see if we're sleeping or not
   if (atomic_read(&sleep_atomic) == 1) { return 0; }
   
   // special function for reversing
   atomic_set(&reverse_speed, 0);
   if ((o_speed > 0 && speed < 0) || (o_speed < 0 && speed > 0)) {
      // can't use a scenario, because a scenario may cause a reverse
      return mover_speed_reverse(speed);
   }

   // can we go the requested speed?
   if (abs(speed) > NUMBER_OF_SPEEDS[mover_type]) {
      do_fault(ERR_invalid_speed_req); // The fasit spec has this message
      return 0; // nope
   }
   else if (speed == 0) {
      do_fault(ERR_speed_zero_req); // The fasit spec has this message
      // do not return, we will just coast
   }
   else if (abs(speed) < MIN_SPEED[mover_type]) {
      do_fault(ERR_invalid_speed_req); // The fasit spec has this message
      // do not return, there is logic in the 
      // hardware_speed_set function to handle this.
   }

   // first select desired movement movement
   if (speed < 0) {
      // if we are home, do not allow reverse
      if (atomic_read(&last_sensor) == MOVER_SENSOR_HOME) {
         do_fault(ERR_invalid_direction_req);
         return 0;
      }
      hardware_movement_set(MOVER_DIRECTION_REVERSE);
   } else if (speed > 0) {
      // if we are at the end, do not allow forward
      if (atomic_read(&last_sensor) == MOVER_SENSOR_END) {
         do_fault(ERR_invalid_direction_req);
         return 0;
      }
      hardware_movement_set(MOVER_DIRECTION_FORWARD);
   } else { 
      // setting 0 will cause the mover to "coast" if it isn't already stopped
#ifndef DEBUG_PID
//          send_nl_message_multi("Started coasting...", error_mfh, NL_C_FAILURE);
#endif
   }

// Since we are moving the last_sensor needs to be unknown
   atomic_set(&last_sensor, MOVER_SENSOR_UNKNOWN);

   hardware_speed_set(abs(speed));

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
    return ((INCHES_PER_TICK[mover_type]*pos)/(ENC_PER_REV[mover_type]*TICKS_DIV))/12; // inches to feet
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
// The function that gets called when the ramp timer fires (adjusts speed up or down gradually)
//---------------------------------------------------------------------------
static void ramp_fire(unsigned long data) {
    int goal_start, goal_step, goal_end, new_speed, ticks_change, ramp, start_speed, direction, speed_steps, ramp_time=RAMP_UP_TIME_IN_MSECONDS[mover_type];
    if (!atomic_read(&full_init)) {
        return;
    }

#ifndef DEBUG_PID
//send_nl_message_multi("...doing ramp...", error_mfh, NL_C_FAILURE);
#endif

    goal_start = atomic_read(&goal_start_atomic);
    goal_step = atomic_read(&goal_step_atomic);
    goal_end = atomic_read(&goal_atomic);

    // calculate speed based on percent of full difference
    ticks_change = abs(percent_from_speed(goal_end) - percent_from_speed(goal_start));

    // calculate start speed
    start_speed = percent_from_speed(goal_start);

    // calculate number of steps to take based on the amount of speed change
    if (IGNORE_RAMP[mover_type]) {
        // don't ramp MITs
        speed_steps = 1;
    } else {
        // ramp MATs
        speed_steps = (RAMP_STEPS[mover_type] * abs(goal_end - goal_start)) / 10; // normal steps * number of speed change (in 10s)
#ifdef TESTING_MAX
        speed_steps = 1;
#endif
    }

    // find direction
    if (goal_end < goal_start) {
        direction = -1;
        goal_step *= -1;
        ramp_time = RAMP_DOWN_TIME_IN_MSECONDS[mover_type];
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
        if (IGNORE_RAMP[mover_type]) {
            mod_timer(&ramp_timer_list, jiffies+(1*HZ)/1000); // move immediately if we're ignoring the ramp
        } else {
#ifdef TESTING_MAX
            mod_timer(&ramp_timer_list, jiffies+(1*HZ)/1000); // move immediately if we're ignoring the ramp
#else
            mod_timer(&ramp_timer_list, jiffies+(((ramp_time*HZ)/1000)/RAMP_STEPS[mover_type])); // use original number of steps here to allow speed_steps to stretch the time
#endif
        }
    } else {
        // done moving
        atomic_set(&speed_atomic, goal_end); // reached new speed value

        // create events
        if (goal_end == 0) {
            do_event(EVENT_STOP); // started stopping (probably finished stopping too, but that's handled elsewhere)
        } else {
#ifndef WOBBLE_DETECT
#ifndef ACCEL_TEST
            do_event(EVENT_MOVING); // reached moving goal
#endif
#endif
        }
    }

    // set desired percentage
    atomic_set(&pid_set_point, new_speed);

// new method
    // call initial pid_step if we are just starting to ramp and we're moving slowly
    if (abs(goal_step) <= 1 && start_speed < 2) {
#ifndef DEBUG_PID
//          char *msg = kmalloc(128, GFP_KERNEL);
//          snprintf(msg, 128, "ramp start %i: %i => %i", goal_step, goal_start, goal_end);
//          send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
//          kfree(msg);
//send_nl_message_multi("Started moving!", error_mfh, NL_C_FAILURE);
#endif
        if (CONTACTOR_H_BRIDGE[mover_type]) {
            if (direction == -1) {
        // assert pwm line
        #if PWM_BLOCK == 0
            at91_set_A_periph(MOTOR_PWM_R, PULLUP_OFF);
        #else
            at91_set_B_periph(MOTOR_PWM_R, PULLUP_OFF);
        #endif
            } else {
        #if PWM_BLOCK == 0
            at91_set_A_periph(MOTOR_PWM_F, PULLUP_OFF);
        #else
            at91_set_B_periph(MOTOR_PWM_F, PULLUP_OFF);
        #endif
            }
        }
        pid_step(MAX_TIME); // use maximum delta_t
    } else if (atomic_read(&moving_atomic) < 3) {
        int clock = RPM_K[mover_type] / 60; // RPM_K is clock ticks per minute - find clock by doing reverse
        int ticks = (clock / ramp_time) / speed_steps;
#ifndef DEBUG_PID
//          char *msg = kmalloc(128, GFP_KERNEL);
//          snprintf(msg, 128, "ramp continue %i: pid_step(%i:%i)", goal_step, ticks, new_speed);
//          send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
//          kfree(msg);
#endif
          pid_step(ticks); // use calculated time
    } else {
#ifndef DEBUG_PID
//          char *msg = kmalloc(128, GFP_KERNEL);
//          snprintf(msg, 128, "ramp skipped %i: %i", goal_step, new_speed);
//          send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
//          kfree(msg);
#endif
    }
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
        return sprintf(buf, "%i\n", (RPM_K[mover_type]/ENC_PER_REV[mover_type])/vel);
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
    else if (atomic_read(&moving_atomic) > 0)
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
    // turn on directional lines
    if (DIRECTIONAL_H_BRIDGE[mover_type]) {
        // H-bridge handling
        // de-assert the neg inputs to the h-bridge
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);

        // we always turn both signals off first to ensure that both don't ever get turned
        // on at the same time
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
    } else {
        // forward for non-h-bridge
        // reverse off, forward on
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE, !OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD, OUTPUT_MOVER_DIRECTION_ACTIVE_STATE);
    }

    // forward for H-Bridge stuff
    if (PWM_H_BRIDGE[mover_type]) {
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
    } else if (DIRECTIONAL_H_BRIDGE[mover_type]) {
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
    }

        // assert pwm line
        #if PWM_BLOCK == 0
            at91_set_A_periph(MOTOR_PWM_F, PULLUP_OFF);
        #else
            at91_set_B_periph(MOTOR_PWM_F, PULLUP_OFF);
        #endif
        }

    // turn off brake?
    if (USE_BRAKE[mover_type]) {
        at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, !OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);
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
    delay_printk("speed_store");

    status = strict_strtol(buf, 0, &value);
    if ((status == 0) &&
           (value >= 0) &&
           (value <= NUMBER_OF_SPEEDS[mover_type]))
        {
        delay_printk("speed_store - speed: %i\n", value);
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
#ifndef WOBBLE_DETECT
#ifndef ACCEL_TEST
            do_event(EVENT_POSITION); // notify mover driver
#endif
#endif
            target_sysfs_notify(&target_device_mover_generic, "position");
            }
            // See if our position is out of bounds
            // The numbers 100 and -100 are randomly picked 
            if (atomic_read(&internal_length) != 0){
               if (atomic_read(&position) > ( atomic_read(&internal_length) + 100) ||
                        atomic_read(&position) < -100){
               do_fault(ERR_stop_by_distance);
               hardware_speed_set(0);
               }
            }
        }

static void do_velocity(struct work_struct * work) {
    int vel;
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    // use the calculated speed to see if we've changed a significant digit or not
    vel = current_speed10();
    
    // only notify sysfs if we've changed velocity
    if (abs(atomic_read(&velocity_old) - vel) > 0) {
        atomic_set(&velocity_old, vel); 
        target_sysfs_notify(&target_device_mover_generic, "velocity");
#ifndef WOBBLE_DETECT
#ifndef ACCEL_TEST
        do_event(EVENT_IS_MOVING); // notify mover driver
#endif
#endif
        target_sysfs_notify(&target_device_mover_generic, "rpm");
    }
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
// PID loop step
//---------------------------------------------------------------------------
// old method
#if 0
static void pid_step(void) {
#endif

// new method
static void pid_step(int delta_t) {
    int new_speed=1, input_speed=0, error_sum=0, i, pid_effort=0, pid_p=0, pid_i=0, pid_d=0;
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    new_speed = atomic_read(&pid_set_point); // read desired pid set point

    // read input speed (adjust speed10 value to 1000*percent)
    input_speed = percent_from_speed(abs(current_speed10()));
#ifdef DEBUG_PID
    if (1) {
        char *msg = kmalloc(128, GFP_KERNEL);
        struct timespec time_now = current_kernel_time();
        struct timespec time_d = timespec_sub(time_now, time_start);
        //snprintf(msg, 128, "setpoint:%i; detect:%i", new_speed, input_speed);
        snprintf(msg, 128, "%i,%i,%i,%i", (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000), new_speed, input_speed, delta_t);
        delay_printk(msg);
        send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
        kfree(msg);
    }
#endif

    // We have just a few microseconds to complete operations, so die if we can't lock
    if (!spin_trylock(&pid_lock)) {
        return;
    }

#ifndef DEBUG_PID
#ifdef WOBBLE_DETECT
    // move last speeds if we've changed speeds
    if (input_speed != pid_last_speeds[2]) {
    pid_last_speeds[0] = pid_last_speeds[1];
    pid_last_speeds[1] = pid_last_speeds[2];
    pid_last_speeds[2] = input_speed;

    // detect wobble peak
    if (pid_last_speeds[0] < pid_last_speeds[1] &&
        pid_last_speeds[1] > pid_last_speeds[2]) { // pid_last_speeds[1] is the peak
        struct timespec time_now = current_kernel_time();
        struct timespec time_d = timespec_sub(time_now, pid_last_time);
        char *msg = kmalloc(128, GFP_KERNEL);
        pid_last_time = time_now; // remember last time as now
        snprintf(msg, 128, "Wobble @ %i : (%i) %i=>%i (%i)\n", (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000), new_speed, pid_last_speeds[1], input_speed, pid_last_speeds[1] - input_speed); // print wobble time and amount to track if we're wobbling steady
        delay_printk(msg);
        // send to userspace as a "failure" message
        send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
        kfree(msg);
    }

#ifdef SPIN_DETECT
    if ((input_speed - pid_last_speeds[0]) > SPIN_DETECT) {
        send_nl_message_multi("~~~Wheel spin detected~~~", error_mfh, NL_C_FAILURE);
    }
#endif
#ifdef STALL_DETECT
    } else if (pid_last_speeds[0] == pid_last_speeds[1] && pid_last_speeds[1] == pid_last_speeds[2] && input_speed == 0 && new_speed > 0) {
        send_nl_message_multi("~~~Stall detected~~~", error_mfh, NL_C_FAILURE);
#endif
    }
// end of wobble detect
#endif
// end of not DEBUG PID
#endif


// old method
# if 0
    // move error values
    pid_last_last_error = pid_last_error;
    pid_last_error = pid_error;
    pid_error = new_speed - input_speed; // set point - input

    // move effort values
    pid_last_effort = pid_effort;

    // macro definitions to make equation below more clear
#define dt_Ti (((1000 * PID_DELTA_T[mover_type] * PID_TI_DIV[mover_type]) / PID_TI_MULT[mover_type]) / 1000)
#define Td_dt (((1000 * PID_TD_MULT[mover_type]) / (PID_TD_DIV[mover_type] * PID_DELTA_T[mover_type])) / 1000)

    // descrete PID algorithm gleamed from wikipedia
    pid_effort = pid_last_effort + (
        (PID_GAIN_MULT[mover_type]
            * (
              ((1 + dt_Ti + Td_dt) * pid_error) +
              ((-1 - (2 * Td_dt) * pid_last_error)) +
              (Td_dt * pid_last_last_error)
              )
        ) / PID_GAIN_DIV[mover_type]
    );
#endif

// new method
    // move error values
    pid_last_error = pid_error;
    pid_error = new_speed - input_speed; // set point - input

    // put calculated current error to end of list
    pid_errors[MAX_PID_ERRORS-1] = (delta_t * (pid_last_error - pid_error)) / 2;

    // calculate sum of past errors
    for (i=0; i<MAX_PID_ERRORS; i++) {
       error_sum += pid_errors[i];
    }

    // are we correct?
    if (pid_error == 0) {
       pid_correct_count++; // correct one more time
    } else if (abs(pid_error) >= SPEED_CHANGE[mover_type]) {
if (pid_correct_count > 0) {
#ifndef DEBUG_PID
//send_nl_message_multi("UN-CLAMPED!", error_mfh, NL_C_FAILURE);
#endif
//delay_printk("UN-CLAMPED!");
}
       pid_correct_count = 0; // no longer correct
    }

    // check to see if we've found the right effort for this speed (and haven't left the given error range)
    if (pid_correct_count >= SPEED_AFTER[mover_type] && abs(pid_error) < SPEED_CHANGE[mover_type]) {
       // clamp effort to same as last time
#if 0
          char *msg = kmalloc(128, GFP_KERNEL);
          snprintf(msg, 128, "Clamped at correct speed: %i %i-%i\n", pid_correct_count, new_speed, input_speed);
          send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
          kfree(msg);
#endif
       pid_effort = pid_last_effort;
//delay_printk("CLAMPED!");
    } else {
       // do the PID calculations

       // individual pid steps to make full equation more clean
       pid_p = (((1000 * kp_m * pid_error) / kp_d) / 1000);
       if (ADJUST_PID_P[mover_type]) {
          // adjust PID proportional gain as percentage (top speed = 100%)
          pid_p *= new_speed;
          pid_p /= 1000;
       }
       pid_i = (((1000 * ki_m * error_sum) / ki_d) / 1000);
       pid_d = (((1000 * kd_m * (pid_error - pid_last_error)) / (kd_d * delta_t)) / 1000);

       // descrete PID algorithm gleamed from Scott's brain
       pid_effort = pid_last_effort + pid_p + pid_i + pid_d;
    }

    // max effort to 100%
    pid_effort = min(pid_effort, 1000);

    // min effort to positive numbers only -- TODO -- negative effort uses reverse motor techniques to apply negative pressure
    if (new_speed == 0) {
       pid_effort = max(pid_effort, 0); // minimum when stopping is 0
    } else {
       pid_effort = max(pid_effort, min_effort); // minimum when moving
       if (pid_effort == min_effort) {
#ifndef DEBUG_PID
//          char *msg = kmalloc(128, GFP_KERNEL);
//          snprintf(msg, 128, "Used minimum effort: %i\n", pid_last_effort);
//          send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
//          kfree(msg);
#endif
       }
    }

    // if we're accelerating (not negative acceleration) check maximum acceleration allowed
    if (pid_effort > pid_last_effort && (pid_effort - pid_last_effort) > max_accel) {
#ifndef DEBUG_PID
//        char *msg = kmalloc(128, GFP_KERNEL);
//        snprintf(msg, 128, "Clamped at max accel: %i+%i\n", pid_last_effort, pid_effort - pid_last_effort);
//        send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
//        kfree(msg);
#endif
        // clamp at max acceleration...
        pid_effort = pid_last_effort + max_accel;
    }

    // if we're deccelerating (negative acceleration) check maximum decceleration allowed
    if (pid_effort < pid_last_effort && (pid_last_effort - pid_effort) > max_deccel) {
#ifndef DEBUG_PID
//        char *msg = kmalloc(128, GFP_KERNEL);
//        snprintf(msg, 128, "Clamped at max deccel: %i+%i\n", pid_last_effort, pid_effort - pid_last_effort);
//        send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
//        kfree(msg);
#endif
        // clamp at max acceleration...
        pid_effort = pid_last_effort - max_deccel;
    }

// old method
#if 0
    // clamp everything at 0 if we're truly stopped
    if (new_speed == 0 && input_speed == 0 && pid_error == 0 && pid_last_error == 0 && pid_last_last_error == 0) {
        pid_effort = 0;
        pid_last_effort = 0;
    }
#endif

// new method
    // move past errors back in error buffer
    for (i=0; i<MAX_PID_ERRORS-1; i++) {
       pid_errors[i] = pid_errors[i+1];
    }

    // convert effort to pwm
    new_speed = pwm_from_effort(pid_effort);

#if 0
    if (pid_effort != pid_last_effort) {
        char *msg = kmalloc(128, GFP_KERNEL);
        snprintf(msg, 128, "Changed effort: %i => %i\n", pid_last_effort, pid_effort);
        send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
        kfree(msg);
    }
#endif
    // These change the pwm duty cycle
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB));

    // move effort values
    pid_last_effort = pid_effort;

    // unlock for next time
    spin_unlock(&pid_lock);
    //delay_printk(" r: %i; f: %i; e: %i\n", new_speed, pid_effort, pid_error);
// old method
#if 0
    delay_printk(" r: %i; f: %i; e: %i; p: %i/%i; i: %i/%i; d: %i/%i\n", new_speed, pid_effort, pid_error,
                 PID_GAIN_MULT[mover_type], PID_GAIN_DIV[mover_type], PID_TI_MULT[mover_type], PID_TI_DIV[mover_type], PID_TD_MULT[mover_type], PID_TD_DIV[mover_type]);
#endif
// new method
#ifdef DEBUG_PID
    if (1) {
        char *msg = kmalloc(256, GFP_KERNEL);
        snprintf(msg,256," n:%i; u:%i; e:%i; p:%i:%i/%i; i:%i:%i/%i; d:%i:%i/%i\n", new_speed, pid_effort, pid_error,
                 pid_p, kp_m, kp_d,
                 pid_i, ki_m, ki_d,
                 pid_d, kd_m, kd_d);
        delay_printk(msg);
//        send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
        kfree(msg);
    }
#endif
}


//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_mover_generic_init(void)
    {
// old method
#if 0
    struct heartbeat_object hb_obj;
    int d_id;
    struct nl_driver driver = {&hb_obj, NULL, 0, NULL}; // only heartbeat object
#endif
    int retval;

    // for debug
    time_start = current_kernel_time();
    switch (mover_type) {
        case 0: atomic_set(&tc_clock, 1); break;
        case 1: atomic_set(&tc_clock, 2); break;
        case 2: atomic_set(&tc_clock, 4); break;
    }

    // find actual PID values to use based on given mover type or manually given module parameters
    if (kp_m < 0) {kp_m = PID_KP_MULT[mover_type];}
    if (kp_d < 0) {kp_d = PID_KP_DIV[mover_type];}
    if (ki_m < 0) {ki_m = PID_KI_MULT[mover_type];}
    if (ki_d < 0) {ki_d = PID_KI_DIV[mover_type];}
    if (kd_m < 0) {kd_m = PID_KD_MULT[mover_type];}
    if (kd_d < 0) {kd_d = PID_KD_DIV[mover_type];}
    if (min_effort < 0) {min_effort = MIN_EFFORT[mover_type];}
    if (max_accel < 0) {max_accel = MAX_ACCEL[mover_type];}
    if (max_deccel < 0) {max_deccel = MAX_DECCEL[mover_type];}

    // initialize hardware registers
    hardware_init();

    // initialize delayed work items
    INIT_WORK(&position_work, do_position);
    INIT_WORK(&velocity_work, do_velocity);
    INIT_WORK(&delta_work, do_delta);
    INIT_WORK(&movement_work, movement_change);

// old method
#if 0
    // setup heartbeat for polling
    hb_obj_init_nt(&hb_obj, pid_step, PID_HZ[mover_type]); // heartbeat object calling pid_step()
    d_id = install_nl_driver(&driver);
    atomic_set(&driver_id, d_id);
#endif

// new method
    // initialize pid stuff
    memset(pid_errors, 0, sizeof(int)*MAX_PID_ERRORS);

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
// old method
#if 0
    uninstall_nl_driver(atomic_read(&driver_id));
#endif
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

