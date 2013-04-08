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
#include "target_battery.h"

#include "target_generic_output.h" /* for EVENT_### definitions */

// Mover type defines
#define MATOLD 0
#define MAT 1
#define MIT 2
#define MITP 3
#define MITPAMC 4
#define IS_MIT ( mover_type == MIT || mover_type == MITP || mover_type == MITPAMC )
#define IS_MAT ( mover_type == MATOLD || mover_type == MAT )

//#define TESTING_ON_EVAL
//#define TESTING_MAX
//#define ACCEL_TEST
//#define WOBBLE_DETECT
//#define SPIN_DETECT 100
//#define STALL_DETECT
//#define PRINT_DEBUG
//#define SEND_DEBUG
//#define SEND_PID
//#define SEND_POS

#if defined(SEND_DEBUG) || defined(SEND_PID) || defined(SEND_POS)
#define SENDUSERCONNMSG  sendUserConnMsg
static void sendUserConnMsg( char *fmt, ...);
#else
#define SENDUSERCONNMSG(...)  //
#endif

#ifdef PRINT_DEBUG
#define DELAY_PRINTK  delay_printk
#else
#define DELAY_PRINTK(...)  //
#endif

//---------------------------------------------------------------------------
// These variables are parameters giving when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
static int mover_type = 1; // 0 = armor 2 ticks, 1 = armor 360 ticks, 2 = infantry/48v KDZ controller, 3 = error
static int dock_loc = 2; // 0 = home end of track, 1 = end away from home, 2 = no dock
// This is for the "Home" location, where everything starts from.
// This may or may not be the same place as the dock
static int home_loc = 0; // 0 = home end of track, 1 = end away from home
static int track_len = 10; // default to 10 meters
module_param(mover_type, int, S_IRUGO);
module_param(dock_loc, int, S_IRUGO);
module_param(home_loc, int, S_IRUGO);
module_param(track_len, int, S_IRUGO);
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

static char* TARGET_NAME[] = {"armor mover 2 ticks","armor mover 360 ticks","48v infantry mover","48v infantry mover portable","48v infantry mover horizontal advance motion control","48v soft-reverse infantry mover","error"};
static char* MOVER_TYPE[] = {"armor","armor","infantry","infantry","infantry","infantry","error"};

// continue moving on leg or quad interrupt or neither
static int CONTINUE_ON[] = {3,1,2,2,2,2,0}; // leg = 1, quad = 2, both = 3, neither = 0

// TODO - replace with a table based on distance and speed?
static int TIMEOUT_IN_MSECONDS[] = {12000,12000,2000,2000,2000,2000,0};
static int MOVER_DELAY_MULT[] = {2,2,2,2,2,2,0};

static int DOCK_RETRY_COUNT[] = {2, 2, 3, 1, 1, 1, 0}; // the more we retry, the more likely we are to trip a breaker

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
#define MOVER_SENSOR_DOCK  3

#define MOVER_SPEED_SELECTIONS  5
// not used? -- static int MIT_SPEEDS[] = {0,7,15,23,33,0}; // minimum acceptible input speed (15 =1 1/2 mph, 20 = 2 mph)
// the maximum allowed speed ticks
//static int NUMBER_OF_SPEEDS[] = {10,20,10,10,0};
static int NUMBER_OF_SPEEDS[] = {200,200,200,200,200,200,0};
static int MIN_SPEED[] = {10,10,7,3,10,10,0}; // minimum acceptible input speed (15 =1 1/2 mph, 20 = 2 mph)

// horn on and off times (off is time to wait after mover starts moving before going off)
static int HORN_ON_IN_MSECONDS[] = {3500,3500,0,0,0,0,0};
static int HORN_OFF_IN_MSECONDS[] = {8000,8000,0,0,0,0,0};

// getting going effort stuff
//static int UNDER_WAY[] = {5, 5, 1, 5, 0}; /* how many encoder clicks before we let go of the getting going effort */
//static int UNDER_WAY_MERGE[] = {5, 5, 5, 5, 0}; /* how many pid_step()s get called where I merge the gg effort and the pid_effort */
//static int GETTING_GOING_EFFORT[] = {500, 250, 500, 500, 0};

// the parameters needed for PID speed control
// old method
// not used? -- static int PID_HZ[]        = {20,32767,20,20,1};
// not used? -- static int PID_DELTA_T[]   = {50,10,50,50,1}; // inversely proportial to HZ (ie 100 dt = 1000 ms / 100 hz)
// not used? -- static int PID_GAIN_MULT[] = {10,1000,1,1,0}; // proportional gain numerator
// not used? -- static int PID_GAIN_DIV[]  = {275,1000,15,15,1}; // proportional gain denominator
// not used? -- static int PID_TI_MULT[]   = {25,1,2500,2500,1}; // time integral numerator
// not used? -- static int PID_TI_DIV[]    = {1,1,1,1,1}; // time integral denominator
// not used? -- static int PID_TD_MULT[]   = {1,1,2,1,1}; // time derivitive numerator
// not used? -- static int PID_TD_DIV[]    = {10,1,1,1,1}; // time derivitive denominator
// end old method
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
static int PID_KP_MULT[]   = {3, 3, 20, 3, 1, 1, 0}; // proportional gain numerator
static int PID_KP_DIV[]    = {1, 1, 1, 5, 5, 5, 0}; // proportional gain denominator
static int PID_KI_MULT[]   = {1, 1, 0, 0, 0, 15, 0}; // integral gain numerator
static int PID_KI_DIV[]    = {3, 3, 1, 1, 1, 47515, 0}; // integral gain denominator
static int PID_KD_MULT[]   = {0, 0, 0, 0, 0, 190060, 0}; // derivitive gain numerator
static int PID_KD_DIV[]    = {15, 15, 1, 1, 1, 1, 0}; // derivitive gain denominator
#ifdef TESTING_MAX
static int MIN_EFFORT[]    = {1000, 1000, 1000, 1000, 1000, 1000, 0}; // minimum effort given to ensure motor moves
#else
static int MIN_EFFORT[]    = {175, 175, 1, 1, 1, 1, 0}; // minimum effort given to ensure motor moves
#endif
// not used? -- static int SPEED_AFTER[]   = {1, 3, 1, 1, 0}; // clamp effort if we hit this many correct values in a row
// not used? -- static int SPEED_CHANGE[]  = {40, 30, 40, 1, 0}; // unclamp if the error is bigger than this
// not used? -- static int ADJUST_PID_P[]  = {0, 0, 0, 0, 0}; // adjust MIT's proportional gain as percentage of final speed / max speed
static int MAX_ACCEL[]     = {1000, 1000, 1000, 1000, 1000, 1000, 0}; // maximum effort change in one step
static int MAX_DECCEL[]     = {1000, 1000, 1000, 1000, 1000, 1000, 0}; // maximum effort change in one step

// variables for setting the initial position if it's on the dock or not
static int DOCK_FEET_FROM_LIMIT[] = {18, 18, 6, 6, 6, 6, 0}; // home many feet away the dock is past the limit
static int COAST_FEET_FROM_LIMIT[] = {18, 12, 4, 4, 4, 4, 0}; // home many feet away it *typically* coasts past the limit

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

static int MOTOR_PWM_FWD[] = {OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,0}; // would be OUTPUT_MOVER_MOTOR_FWD_POS if PWM on H-bridge
static int MOTOR_PWM_REV[] = {OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,OUTPUT_MOVER_PWM_SPEED_THROTTLE,0}; // would be OUTPUT_MOVER_MOTOR_REV_POS if PWM on H-bridge
#define MOTOR_PWM_F (reverse ? MOTOR_PWM_REV[mover_type] : MOTOR_PWM_FWD[mover_type])
#define MOTOR_PWM_R (reverse ? MOTOR_PWM_FWD[mover_type] : MOTOR_PWM_REV[mover_type])

// RC - max time (allowed by PWM code)
// END - max time (allowed by me to account for max voltage desired by motor controller : 90% of RC)
// RA - low time setting - cannot exceed RC
// RB - low time setting - cannot exceed RC
static int MOTOR_PWM_RC[] = {0x3074,0x3074,0x1180,0x7000,0x7000,0x1180,0};
static int MOTOR_PWM_END[] = {0x3074,0x3074,0x1180,0x7000,0x7000,0x1180,0};
//static int MOTOR_PWM_RA_DEFAULT[] = {0x04D8,0x04D8,0x0000,0x0000,0};
//static int MOTOR_PWM_RB_DEFAULT[] = {0x04D8,0x04D8,0x0000,0x0000,0};
static int MOTOR_PWM_RA_DEFAULT[] = {0x0001,0x0001,0x0001,0x0001,0x001,0x001,0};
static int MOTOR_PWM_RB_DEFAULT[] = {0x0001,0x0001,0x0001,0x0001,0x001,0x001,0};

// TODO - map pwm output pin to block/channel
#define PWM_BLOCK				1				// block 0 : TIOA0-2, TIOB0-2 , block 1 : TIOA3-5, TIOB3-5
static int MOTOR_PWM_CHANNEL[] = {1,1,1,1,1,1,0};		// channel 0 matches TIOA0 to TIOB0, same for 1 and 2
#define ENCODER_PWM_CHANNEL		0				// channel 0 matches TIOA0 to TIOB0, same for 1 and 2

#define MAX_TIME	0x10000
#define MAX_OVER	0x10000
static int RPM_K[] = {1966080, 1966080, 1966080, 1966080, 1966080, 1966080, 0}; // CLOCK * 60 seconds
static int ENC_PER_REV[] = {2, 360, 2, 2, 2, 2, 0}; // 2 = encoder click is half a revolution, or 360 ticks per revolution
static int VELO_K[] = {1344, 1344, 1680, 1680, 1680, 1680, 0}; // rpm/mph*10
static int INCHES_PER_REV[] = {157, 157, 314, 314, 314, 314, 0}; // 5 inch, 10 inch, etc. = inches per wheel revolution
static int TICKS_PER_LEG[] = {1833, 1833, 2292, 2292, 2292, 2292, 0}; // 2:1 ratio 5 inch wheel 6 ft leg, 5:1 ratio 10 inch wheel 6 ft leg, etc.
#define TICKS_DIV 100

// to keep updates to the file system in check somewhat
#define POSITION_DELAY_IN_MSECONDS	500
#define VELOCITY_DELAY_IN_MSECONDS	500
#define DOCK_TIMEOUT_IN_MILLISECONDS 10000
#define SPEED_TIMEOUT_IN_MILLISECONDS 2000
#define PID_TIMEOUT_IN_MILLISECONDS 1000
#define CONTINUOUS_TIMEOUT_IN_MILLISECONDS 1000
#define RAMP_TIMEOUT_IN_MILLISECONDS 10

static int COAST_FACTOR[]  = {10, 10, 20, 20, 20, 20, 1}; // divisor to PID_TIMEOUT_IN_MILLISECONDS, tuned for how well the target coasts (smaller for coasts better? bigger for brakes better?)

// speed charts (TODO -- update with mover on track with load)
// static in MOVER0_PWM_TABLE = ?
// static in MOVER1_PWM_TABLE = ?
// static int MOVER2_PWM_TABLE[] = {0, 1100, 1550, 1975, 2375, 2800, 3325, 3900, 5000, 7000, 10000}; // -- first stab
// static int MOVER2_PWM_TABLE[] = {0, 1350, 1800, 2300, 2700, 3200, 3700, 4500, 5500, 7500, 12000}; // -- second stab

static int MOVER_RAMP_STEPS[] = {7, 7, 0, 0, 1, 1, 0}; // MATs do 2 * speed, MITs do 1 * speed



// external motor controller polarity
static int OUTPUT_MOVER_PWM_SPEED_ACTIVE[] = {ACTIVE_LOW,ACTIVE_LOW,ACTIVE_LOW,ACTIVE_LOW,ACTIVE_LOW,ACTIVE_LOW,0};

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
#define INPUT_MOVER_TRACK_DOCK	INPUT_CHARGING_BAT
#define INPUT_MOVER_DOCK_PULLUP_STATE INPUT_CHARGING_BAT_PULLUP_STATE
#define INPUT_MOVER_DOCK_ACTIVE_STATE	INPUT_CHARGING_BAT_ACTIVE_STATE
#define LIFTER_POSITION_DOWN 0

static bool PWM_H_BRIDGE[] = {false,false,false,false,false,false,false};
static bool DIRECTIONAL_H_BRIDGE[] = {true,true,true,true,true,false,false};
static bool USE_BRAKE[] = {false,false,false,true,true,true,false};
static bool CONTACTOR_H_BRIDGE[] = {false, false, false, false, false, true, false};
static bool MOTOR_CONTROL_H_BRIDGE[] = {false, false, false, false, false, false, false};

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
atomic_t timeout_speed = ATOMIC_INIT(0);
atomic_t dockTryCount = ATOMIC_INIT(0);
atomic_t continuous_speed = ATOMIC_INIT(0);
atomic_t last_reported_speed = ATOMIC_INIT(0);
atomic_t last_direction = ATOMIC_INIT(0);
atomic_t lifter_fault = ATOMIC_INIT(0);
atomic_t found_home = ATOMIC_INIT(0);
atomic_t found_dock = ATOMIC_INIT(0);
atomic_t dock_location = ATOMIC_INIT(0);
atomic_t home_max = ATOMIC_INIT(-5000); // maximum value for home location (used for invalid direction detecting)
atomic_t end_min = ATOMIC_INIT(5000); // minimum value for end location (used for invalid direction detecting)
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
atomic_t pid_vel = ATOMIC_INIT(0);
//atomic_t getting_going = ATOMIC_INIT(0);
//atomic_t going_base = ATOMIC_INIT(0);

// This is used to know which sensor was last set so we can determine
// invalid direction request. If we are home we do not want to 
// allow reverse.
atomic_t last_sensor = ATOMIC_INIT(MOVER_SENSOR_UNKNOWN);


atomic_t lifter_position = ATOMIC_INIT(LIFTER_POSITION_DOWN);

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

//---------------------------------------------------------------------------
// This atomic variable is for our registration with netlink_provider
//---------------------------------------------------------------------------
atomic_t driver_id = ATOMIC_INIT(-1);

// new method
//---------------------------------------------------------------------------
// Forward definition of pid function
//---------------------------------------------------------------------------
static void pid_step(void); 
//static void pid_step2(void); 
static void pid_step3(void); 
static void pid_step4(void); 
#if 0 /* new method not yet used */
static void pid_step1(void); 
#endif

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are awake/asleep
//---------------------------------------------------------------------------
atomic_t sleep_atomic = ATOMIC_INIT(0); // not sleeping

//---------------------------------------------------------------------------
// This atomic variable is use to indicate the goal speed
//---------------------------------------------------------------------------
atomic_t goal_atomic = ATOMIC_INIT(0); // final speed desired
atomic_t ramp_start = ATOMIC_INIT(0); // final speed of ramp
atomic_t ramp_end = ATOMIC_INIT(0); // start speed of ramp
atomic_t ramp_step = ATOMIC_INIT(0); // number of steps determined by (MOVER_RAMP_STEPS * abs(end-start))

//---------------------------------------------------------------------------
// This atomic variable is to store the current movement,
// It is used to synchronize user-space commands with the actual hardware.
//---------------------------------------------------------------------------
atomic_t movement_atomic = ATOMIC_INIT(MOVER_DIRECTION_STOP);

//---------------------------------------------------------------------------
// This atomic variable is to determin if we are going to the dock
// It is used to ignore home and end sensors
//---------------------------------------------------------------------------
atomic_t find_dock_atomic = ATOMIC_INIT(0);

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
static void speed_timeout_fire(unsigned long data);
static void speed_timeout_start(int ms, int speed);
static void speed_timeout_stop(void);
static void dock_timeout_fire(unsigned long data);
static void dock_timeout_start(int ms);
static void dock_timeout_stop(void);
static void pid_timeout_fire(unsigned long data);
static void pid_timeout_start(void);
static void pid_timeout_stop(void);
static void continuous_timeout_fire(unsigned long data);
static void continuous_timeout_start(void);
static void continuous_timeout_stop(void);
static void ramp_timeout_fire(unsigned long data);
static void ramp_timeout_start(int new_speed, int old_speed);
static void ramp_timeout_stop(void);
static void ramp_timeout_continue(void);
static void mover_find_dock(void);
static int isMoverAtDock(void);

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data);

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
static int percent_curve_from_speed(int start, int end, int step); // 1000 * percent, calculated on a curve between start and end
static int pwm_from_effort(int effort);		// absolute pwm, calculated
static int pwm_from_effort_4(int effort);		// absolute pwm, calculated

//---------------------------------------------------------------------------
// Variables for PID speed control
//---------------------------------------------------------------------------
spinlock_t pid_lock = SPIN_LOCK_UNLOCKED;
#define MAX_PID_ERRORS 15
int pid_errors[MAX_PID_ERRORS];  // previous errors (fully calculated for use in summing integral part of PID)
int pid_total_error = 0;   // total effort since we started moving
//int pidEffortCounter = 0;			// prior effort
//int pid_ticks = 0;			// prior effort
int pid_last_effort = 0;			// prior effort
int pid_last_error = 0;				// prior error
int pid_last_last_error = 0;		// prior prior error
int pid_error = 0;					// current error
int pid_counter = 0;		// Used for getting off the dock with the minerak motor controller
//int pid_correct_count = 0;       // number of correct speeds (used to clamp effort)
atomic_t pid_set_point = ATOMIC_INIT(0); // set point for speed to maintain
//int pid_auto_calibration_mult = 0;    // auto-calibration factor to apply to effort
//int pid_auto_calibration_div = 0;    // auto-calibration factor to apply to effort
/*int pid_reset_error = 1;      // we should reset the error when we detect movement
int pid_last_speed = 1;       // what we detected last time we ran pid_step()
int wheel_too_tight = -1;     // we should never reset the error, because our wheel is too tightly connected, thus requiring full force (ie. don't reset after getting going, but still do "normal" halving of total error once moving) -- -1 = haven't checked, -2 = check next time, 1 = too tight, 0 = not too tight
*/

//int last_pid_speed;
int pid_last_speeds[3];          // prior speeds (for detecting wobble)
struct timespec pid_last_time;   // prior peak time (for detecting wobble)
struct timespec time_start;   // time module was loaded

//---------------------------------------------------------------------------
// Kernel timer for the delayed update for position and velocity
//---------------------------------------------------------------------------
static struct timer_list position_timer_list = TIMER_INITIALIZER(position_fire, 0, 0);
static struct timer_list velocity_timer_list = TIMER_INITIALIZER(velocity_fire, 0, 0);

// Timer for finding the dock
static struct timer_list dock_timeout_list = TIMER_INITIALIZER(dock_timeout_fire, 0, 0);

// Timer for trying the speed again
static struct timer_list speed_timeout_list = TIMER_INITIALIZER(speed_timeout_fire, 0, 0);

// Timer for finding the pid
static struct timer_list pid_timeout_list = TIMER_INITIALIZER(pid_timeout_fire, 0, 0);

// Timer for continuous movement
static struct timer_list continuous_timeout_list = TIMER_INITIALIZER(continuous_timeout_fire, 0, 0);

// Timer for ramping movement
static struct timer_list ramp_timeout_list = TIMER_INITIALIZER(ramp_timeout_fire, 0, 0);

// helper function to convert feet to meters
inline int feetToMeters(int feet) {
   // fixed point feet/3.28
   int meters = feet * 100;
   meters /= 328;
   return meters;
}

// helper function to convert meters to feet
inline int metersToFeet(int meters) {
   // fixed point meters*3.28
   int feet = meters * 328;
   feet /= 100;
   return feet;
}


//---------------------------------------------------------------------------
// Reset pid variables
//---------------------------------------------------------------------------
static void reset_pid(void) {
    int i;
    pid_total_error = 0;
    pid_last_effort = 0;
    pid_last_error = 0;
    pid_last_last_error = 0;
    //pid_auto_calibration_mult = 0;
    //pid_auto_calibration_div = 0;
    /*pid_reset_error = 1;
    pid_last_speed = 0;
    if (wheel_too_tight < 0) {
       wheel_too_tight = -1; // if we didn't complete checking for tightness, start over next time around
    }*/
    for (i=0; i<MAX_PID_ERRORS; i++) {
       pid_errors[i] = 0;
    }
}

//---------------------------------------------------------------------------
// Kernel timer for the timeout.
//---------------------------------------------------------------------------
static struct timer_list timeout_timer_list = TIMER_INITIALIZER(timeout_fire, 0, 0);

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
    int to;
//    static int last_mult = -1, this_mult;
    unsigned long new_exp;
    to = TIMEOUT_IN_MSECONDS[mover_type];
    if (isMoverAtDock() != 0) { mult += 1; } // additional multiplier if we have to fight the dock

//    if (last_mult != -1) {
//       this_mult = (mult + last_mult) / 2; // average out the multipliers over time so we don't stop quick after a long multiplier says we should wait a while
//    } else {
//       this_mult = mult;
//    }
//    last_mult = mult;
//    this_mult = max(1, this_mult);

    // find new expire time
    if (mult <= 1) {
        // standard timer
        new_exp = jiffies+((to*HZ)/1000);
    } else {
        // (timer + horn) * mult
        new_exp = jiffies+((mult*(HORN_ON_IN_MSECONDS[mover_type]+to)*HZ)/1000);
    }

    // timer if necessary
    //SENDUSERCONNMSG( "nathan timeout_start: dock %i mult %i", isMoverAtDock(), mult);
    mod_timer(&timeout_timer_list, new_exp);
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
        DELAY_PRINTK("GENERIC MOVER: Registered callback function for move events\n");
    }
    if (faulthandler != NULL && move_fault_callback == NULL) {
        move_fault_callback = faulthandler;
        DELAY_PRINTK("GENERIC MOVER: Registered callback function for move faults\n");
    }
}
EXPORT_SYMBOL(set_move_callback);

static void do_event(int etype) {
    if (move_callback != NULL) {
        move_callback(etype);
    }
}

static void do_fault(int etype) {
    if (move_fault_callback != NULL) {
        move_fault_callback(etype);
    }
}

//---------------------------------------------------------------------------
// a request to turn the motor on
//---------------------------------------------------------------------------
static int hardware_motor_on(int direction)
    {

#ifdef RANDY_GUESSING_AT_PROBLEM    
    if (atomic_read(&movement_atomic) != MOVER_DIRECTION_STOP)
    {
        return;
    }
#endif
// if we are currently docked tell everyone we are not anymore
// Currently we will assume that we will get off of the dock
// if we issue a move
    enable_battery_check(0);
    if (isMoverAtDock() != 0) {
       do_event(EVENT_UNDOCKED);
       battery_check_is_docked(0);
       enable_battery_check(0);
       do_fault(ERR_left_dock_limit);
    }
    // Turn off charging relay (if we have a dock to charge at)
    if (dock_loc == 0 || dock_loc == 1) {
       at91_set_gpio_output(OUTPUT_CHARGING_RELAY, OUTPUT_CHARGING_RELAY_INACTIVE_STATE);
    }
    do_fault(ERR_notcharging_battery);
    //reset_pid(); // reset pid variables
    // turn on directional lines
    if (DIRECTIONAL_H_BRIDGE[mover_type])
        {
        // we always turn both signals off first to ensure that both don't ever get turned
        // on at the same time
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);

        // H-bridge handling
        // de-assert the neg inputs to the h-bridge
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
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
            at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
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
        atomic_set(&last_direction, MOVER_DIRECTION_REVERSE);
       DELAY_PRINTK("%s - %s() - reverse\n",TARGET_NAME[mover_type], __func__);
        }
    else if (direction == MOVER_DIRECTION_FORWARD)
        {
        if (PWM_H_BRIDGE[mover_type]) {
            at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        } else if (DIRECTIONAL_H_BRIDGE[mover_type]) {
#ifndef DEBUG_PID
          //send_nl_message_multi("Forward contacter on!", error_mfh, NL_C_FAILURE);
#endif
            at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, !OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
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
        atomic_set(&last_direction, MOVER_DIRECTION_FORWARD);
       DELAY_PRINTK("%s - %s() - forward\n",TARGET_NAME[mover_type], __func__);
        }
    else
        {
       DELAY_PRINTK("%s - %s() - error\n",TARGET_NAME[mover_type], __func__);
        }

    return 0;
    }

//---------------------------------------------------------------------------
// immediately stop the motor, and try to stop the mover
//---------------------------------------------------------------------------
static int hardware_motor_off(void)
    {
   DELAY_PRINTK("%s - %s()\n",TARGET_NAME[mover_type], __func__);

    // turn off irrelevant timers
    del_timer(&timeout_timer_list);
    del_timer(&horn_on_timer_list);
    del_timer(&horn_off_timer_list);
    mod_timer(&horn_off_timer_list, jiffies+((10*HZ)/1000));

    // de-assert the pwm line
#ifndef DEBUG_PID
//send_nl_message_multi("Brake on!", error_mfh, NL_C_FAILURE);
#endif
//    if(mover_type != MITP){
    at91_set_gpio_output(MOTOR_PWM_F, !OUTPUT_MOVER_PWM_SPEED_ACTIVE[mover_type]);
    at91_set_gpio_output(MOTOR_PWM_R, !OUTPUT_MOVER_PWM_SPEED_ACTIVE[mover_type]);
    __raw_writel(1, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA)); // change to smallest value
    __raw_writel(1, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB)); // change to smallest value
//    }

    // pid stop
    reset_pid(); // reset pid variables
    atomic_set(&goal_atomic, 0); // reset goal speed
    atomic_set(&velocity, 0);

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
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);

        // de-assert the all inputs to the h-bridge
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
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
    enable_battery_check(1);

    return 0;
    }

#if 0
static void setPidSettings(int new_speed)
{
   int Ku = 0, Kp = 0, Tu = 100;
   // not used? -- int Pu = 1000;
    if (IS_MAT) { // MAT
      Ku = new_speed;
//      Kp = Ku * 10 / 6; //Classic
      if (new_speed <= 200) {
         Kp = Ku / 8; // some overshoot
      } else {
         Kp = Ku / 3; // some overshoot
      }
//      kp_m = Ku; kp_d = 1; ki_m = Kp; ki_d = Tu; kd_m = Kp * Tu; kd_d = 8; // classic
      kp_m = Ku; kp_d = 1; ki_m = Kp; ki_d = Tu; kd_m = Kp * Tu; kd_d = 3; // some overshoot
//      ki_m = 0; // No pid_d for MITs
      kd_m = 0; // No pid_d for MITs
    } else if (IS_MIT) { // MIT
         Ku = new_speed;
//         Kp = Ku * 10 / 6; //Classic
         if (new_speed <= 50) {
            Kp = Ku / 2; // some overshoot
         } else {
            Kp = Ku / 3; // some overshoot
         }
//         kp_m = Ku; kp_d = 1; ki_m = Kp; ki_d = Tu; kd_m = Kp * Tu; kd_d = 8; // classic
         kp_m = Kp; kp_d = 1; ki_m = Kp; ki_d = Tu; kd_m = Kp * Tu; kd_d = 3; // some overshoot
         kd_m = 0; // No pid_d for MITs
    }
}
#endif

//---------------------------------------------------------------------------
// sets up a ramping change in speed
//---------------------------------------------------------------------------
static int hardware_speed_set(int new_speed)
    {
    int old_speed;
    DELAY_PRINTK("%s - %s(%i)\n",TARGET_NAME[mover_type], __func__, new_speed);

    // check for full initialization
    if (!atomic_read(&full_init))
        {
               DELAY_PRINTK("%s - %s() error - driver not fully initialized.\n",TARGET_NAME[mover_type], __func__);
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

    old_speed = atomic_read(&goal_atomic);
    if (new_speed != old_speed) {
        atomic_set(&goal_atomic, new_speed); // reset goal speed
        // to ramp, or not to ramp? that is the question
        if ((MOVER_RAMP_STEPS[mover_type] * abs(new_speed-old_speed)) == 0) {
           atomic_set(&pid_set_point, percent_from_speed(new_speed)); // directly set from goal speed
        } else {
           atomic_set(&pid_set_point, percent_curve_from_speed(old_speed, new_speed, 1)); // set as first step of speed change
           ramp_timeout_start(new_speed, old_speed);
        }
        //setPidSettings(percent_from_speed(new_speed));
        // create events
        if (new_speed == 0) {
            do_event(EVENT_COAST); // start of coasting
        } else {
            do_event(EVENT_MOVE); // start of moving
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

    speed = atomic_read(&goal_atomic);
    atomic_set(&speed_atomic, 0);

    // reset PID set point
    atomic_set(&pid_set_point, 0);

    // reset velocity
    atomic_set(&velocity, 0);

// new method
    // reset PID errors and update internal pid variables
    reset_pid(); // reset pid variables

    // turn off the motor
    hardware_motor_off();

    // signal that an operation is done
#ifndef DEBUG_PID
//send_nl_message_multi("\n---------Changing moving_atomic to 0\n", error_mfh, NL_C_FAILURE);
#endif
    atomic_set(&moving_atomic, 0);

    // reset speed ramping
    DELAY_PRINTK("hardware_movement_stop - speed: %i\n", speed);
    hardware_speed_set(speed);

    // notify user-space
    schedule_work(&movement_work);
    dock_timeout_start(60000); // needs to wait at least 2 seconds for velocity to settle to zero

    return 0;
    }

static int mover_speed_stop(int started_stopping) {
   SENDUSERCONNMSG( "randy mover_speed_stop");
    dock_timeout_start(60000); // needs to wait at least 2 seconds for velocity to settle to zero
    speed_timeout_stop();
    ramp_timeout_stop();
    if (started_stopping) {
       do_event(EVENT_STOP); // started stopping
    }
    atomic_set(&goal_atomic, 0); // reset goal speed
    hardware_movement_stop(TRUE); // always true here, so always stops timing out when we stop the mover
    enable_battery_check(1);
    return 1;
}

static int mover_speed_reverse(int speed) {
   timeout_timer_stop();
   SENDUSERCONNMSG( "randy before mover_speed_stop 15");
   mover_speed_stop(1);
   atomic_set(&find_dock_atomic, 0);
   dock_timeout_stop();
   atomic_set(&reverse_speed, speed);
   timeout_timer_start(2); // initial timeout timer twice as long as normal
   return 1;
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

static int mover_speed_set(int speed) {
   int tmpSpeed = 0, selectSpeed = 0, revSpeed = 0;
   int o_speed = current_speed10();

   // check if we're just getting going (once we're under way we'll switch this off)
   //if (o_speed == 0) { atomic_set(&getting_going, 0); /* count up from zero */ }

   SENDUSERCONNMSG( "randy mover_speed_set %i", speed);
   if (speed != 0){
      revSpeed = speed; // we need to save the speed in case we reverse
      if ( mover_type == MITP || mover_type == MITPAMC ){
         selectSpeed = abs(speed);
         if (selectSpeed <= 5) tmpSpeed = 14; // 0.6 mph (to dock slowly)
         else if (selectSpeed <= 10) tmpSpeed = 16; // mph (to meet 1-3 kph)
         else if (selectSpeed <= 20) tmpSpeed = 35; // 3.2 mph (to meet 4-6 kph)
         else if (selectSpeed <= 30) tmpSpeed = 66; // 6.2 mph (to meet 8-10 kph)
         else tmpSpeed = 96; // 9.7 mph (to meet 12-14 kph)
//         if (selectSpeed > MOVER_SPEED_SELECTIONS) selectSpeed = MOVER_SPEED_SELECTIONS;
//         tmpSpeed = MIT_SPEEDS[selectSpeed];
         if (speed < 0) tmpSpeed *= -1;
         speed = tmpSpeed;
      } else if (IS_MIT){
         selectSpeed = abs(speed);
         if (selectSpeed <= 5) tmpSpeed = 6; // 0.6 mph (to dock slowly)
         else if (selectSpeed <= 10) tmpSpeed = 13; // 1.3 mph (to meet 1-3 kph)
         else if (selectSpeed <= 20) tmpSpeed = 31; // 3.2 mph (to meet 4-6 kph)
         else if (selectSpeed <= 30) tmpSpeed = 60; // 6.0 mph (to meet 8-10 kph)
         else tmpSpeed = 85; // 8.5 mph (to meet 12-14 kph)
//         if (selectSpeed > MOVER_SPEED_SELECTIONS) selectSpeed = MOVER_SPEED_SELECTIONS;
//         tmpSpeed = MIT_SPEEDS[selectSpeed];
         if (speed < 0) tmpSpeed *= -1;
         speed = tmpSpeed;
      } else if (IS_MAT){
         selectSpeed = abs(speed);
         if (selectSpeed <= 1) tmpSpeed = 19; // 1.0 mph (to dock slowly, or manual movement)
         else if (selectSpeed <= 5) tmpSpeed = 33; // Button
         else if (selectSpeed <= 10) tmpSpeed = 65; // 6.5 mph (to meet 8-13 kph)
         else if (selectSpeed <= 20) tmpSpeed = 99; // 9.9 mph (to meet 14-18 kph)
         else if (selectSpeed <= 30) tmpSpeed = 134; // 13.4 mph (to meet 19-24 kph)
         else tmpSpeed = 177; // 17.7 mph (to meet 25-32 kph)
//         if (selectSpeed > MOVER_SPEED_SELECTIONS) selectSpeed = MOVER_SPEED_SELECTIONS;
//         tmpSpeed = MIT_SPEEDS[selectSpeed];
         if (speed < 0) tmpSpeed *= -1;
         speed = tmpSpeed;
      }
      if (abs(speed) > 16) {
         atomic_set(&find_dock_atomic, 0); // if moving fast do not ignore sensors
      }
   } else {
      // Stop mover if speed is 0
   SENDUSERCONNMSG( "randy before mover_speed_stop 1");
      mover_speed_stop(1);
      return speed;
   }

   if (o_speed == 0) {
      reset_pid(); // reset pid variables
   }
   
   // moving away from dock when we were trying to find dock?
   if (atomic_read(&find_dock_atomic) == 1) {
      if (speed > 0 && dock_loc == 0) { // going right, dock on left?
         atomic_set(&find_dock_atomic, 0); // moving away from left dock
      } else if (speed < 0 && dock_loc == 1) { // going left, dock on right?
         atomic_set(&find_dock_atomic, 0); // moving away from right dock
      }
   }
   
   // special function for reversing
   SENDUSERCONNMSG( "randy before mover_speed_set o_speed %i, speed %i", o_speed, speed);
   if ((o_speed > 0 && speed < 0) || (o_speed < 0 && speed > 0)) {
      // can't use a scenario, because a scenario may cause a reverse
      timeout_timer_stop();
   SENDUSERCONNMSG( "randy before mover_speed_stop 2");
      atomic_set(&find_dock_atomic, 0); // we never reverse to the dock
      mover_speed_stop(1);
      mover_speed_reverse(revSpeed); // Use the speed passed in for the reverse
      return 1;
   }
   atomic_set(&reverse_speed, 0); // we're not reversing speed, so don't reverse when we stop

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
      if(isMoverAtDock()){
         if (dock_loc == 0){ // Dock on left of track
   SENDUSERCONNMSG( "randy invalid dir dock left");
            do_fault(ERR_invalid_direction_req); return 0;
         }
      }
      // if we are home, do not allow reverse
      if (atomic_read(&last_sensor) == MOVER_SENSOR_HOME) {
         if (atomic_read(&find_dock_atomic) == 0){
   SENDUSERCONNMSG( "randy invalid dir left sensor");
         do_fault(ERR_invalid_direction_req);
         return 0;
         }
      }
      if (0 && IS_MIT && atomic_read(&position) < atomic_read(&home_max)) {
         if (atomic_read(&find_dock_atomic) == 0){
   SENDUSERCONNMSG( "randy invalid dir left position: %i < %i", atomic_read(&position), atomic_read(&home_max));
         do_fault(ERR_invalid_direction_req);
         return 0;
         }
      }
      hardware_movement_set(MOVER_DIRECTION_REVERSE);
   } else if (speed > 0) {
      if(isMoverAtDock()){
         if (dock_loc == 1){ // Dock on right of track
   SENDUSERCONNMSG( "randy invalid dir dock right");
            do_fault(ERR_invalid_direction_req);
            return 0;
         }
      }
      // if we are at the end, do not allow forward
      if (atomic_read(&last_sensor) == MOVER_SENSOR_END) {
         if (atomic_read(&find_dock_atomic) == 0){
   SENDUSERCONNMSG( "randy invalid dir right sensor");
         do_fault(ERR_invalid_direction_req);
         return 0;
         }
      }
      if (0 && IS_MIT && atomic_read(&position) > atomic_read(&end_min)) {
         if (atomic_read(&find_dock_atomic) == 0){
   SENDUSERCONNMSG( "randy invalid dir right position: %i > %i", atomic_read(&position), atomic_read(&end_min));
         do_fault(ERR_invalid_direction_req);
         return 0;
         }
      }
      hardware_movement_set(MOVER_DIRECTION_FORWARD);
   } else { 
      // setting 0 will cause the mover to "coast" if it isn't already stopped
#ifndef DEBUG_PID
//          send_nl_message_multi("Started coasting...", error_mfh, NL_C_FAILURE);
#endif
   }

   if (speed != 0) {
      // if we are moving the last_sensor needs to be unknown
      atomic_set(&last_sensor, MOVER_SENSOR_UNKNOWN);
      pid_counter = 10;
   }

   hardware_speed_set(abs(speed));

   return 1;
}
 
static void dock_timeout_start(int ms)
	{
   if (dock_loc != 0 && dock_loc != 1) {
      return;
   }
   if (ms <= 0) ms = DOCK_TIMEOUT_IN_MILLISECONDS;
	mod_timer(&dock_timeout_list, jiffies+(ms*HZ/1000));
   SENDUSERCONNMSG( "randy dock_timeout_start,%i" ,ms);
	}

//---------------------------------------------------------------------------
// Stops the timeout timer.
//---------------------------------------------------------------------------
static void dock_timeout_stop(void)
	{
	del_timer(&dock_timeout_list);
   SENDUSERCONNMSG( "randy dock_timeout_stop");
	}

//---------------------------------------------------------------------------
// The function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void dock_timeout_fire(unsigned long data)
    {
   int fault, speed;
    dock_timeout_stop();
    if (!atomic_read(&full_init))
        {
        return;
        }
   SENDUSERCONNMSG( "randy dock_timeout_fire");

   // don't try to dock if we're moving
   speed = current_speed10();
   if (abs(speed) > 0) {
      if (atomic_read(&find_dock_atomic) == 1) {
         if ((dock_loc == 0 && speed > 0) || (dock_loc == 1 && speed < 0)) { // if we're moving away from the dock...
            atomic_set(&find_dock_atomic, 0); // ...don't continue looking for the dock
         }
      }
      dock_timeout_start(60000); // needs to wait at least 2 seconds for velocity to settle to zero
      return;
   }

    if (dock_loc == 0) { // Dock at left
       if (atomic_read(&last_sensor) == MOVER_SENSOR_HOME) {
         fault = atomic_read(&lifter_fault);
         if (1 || atomic_read(&lifter_position) == LIFTER_POSITION_DOWN
               || atomic_read(&sleep_atomic) != 0 
               || (fault != 0 && fault != ERR_connected_SIT)
               ){
            mover_find_dock();
         } else {
            dock_timeout_start(60000);
         }
       }
    } else if (dock_loc == 1){ // Dock at right
       if (atomic_read(&last_sensor) == MOVER_SENSOR_END) {
         fault = atomic_read(&lifter_fault);
         if (1 || atomic_read(&lifter_position) == LIFTER_POSITION_DOWN
               || atomic_read(&sleep_atomic) != 0
               || (fault != 0 && fault != ERR_connected_SIT)
               ){
            mover_find_dock();
         } else {
            dock_timeout_start(60000);
         }
       }
    } else {
       mover_find_dock();// No dock, will stop looking for dock
    }
    }

static void speed_timeout_start(int ms, int speed)
	{
      return; // this function has the unintended consequence of causing the target to reverse coarse if the tach sensors are backwards, or if there is a sprurious detection at the wrong time (ie. caused by noise on the line) -- the only consequence of the tach sensors being backwards should be the target reporting it's speed as a negative to its actual speed
#if 0
	   if (speed != 0){
   	      if (ms <= 0) {
               ms = min(TIMEOUT_IN_MSECONDS[mover_type], SPEED_TIMEOUT_IN_MILLISECONDS); // move again before our "no movement detected" timeout fires
            }
	      mod_timer(&speed_timeout_list, jiffies+(ms*HZ/1000));
   	      SENDUSERCONNMSG( "randy speed_timeout_start,%i" ,ms);
	      atomic_set(&timeout_speed, speed);
	   }
#endif
	}

//---------------------------------------------------------------------------
// Stops the timeout timer.
//---------------------------------------------------------------------------
static void speed_timeout_stop(void)
	{
	del_timer(&speed_timeout_list);
   SENDUSERCONNMSG( "randy speed_timeout_stop");
	}

//---------------------------------------------------------------------------
// The function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void speed_timeout_fire(unsigned long data)
    {
        return; // is this function really necessary? with the now-working speed control, the hardware should be fixed to not need this if it's not moving. also see comment in speed_timeout_start
#if 0
    if (!atomic_read(&full_init))
        {
        return;
        }
   SENDUSERCONNMSG( "randy speed_timeout_fire");
   mover_speed_set( atomic_read(&timeout_speed));
#endif

    }

static void pid_timeout_start()
{
   int new_speed, timeout = PID_TIMEOUT_IN_MILLISECONDS;
   new_speed = atomic_read(&pid_set_point); // read desired pid set point
   if (new_speed != 0) {
      //timeout /= 10; // 10 times a second (starting point)
      //timeout /= 3; // 3 times a second (gives the motor controller time to think?)
      //timeout /= 2; // twice a second (gives the motor controller extra time to think?)
      //timeout /= 25; // 25 times a second (cause that's next)
      timeout /= COAST_FACTOR[mover_type]; // adjusted to let it coast nice and smoothly between power pushes
      //timeout /= 250; // 250 times a second (cause that's next)
/*      if (new_speed < 50) {
         timeout /=2;
      }
      if (isMoverAtDock() != 0) { timeout /= 5; } */
   }
   mod_timer(&pid_timeout_list, jiffies+(timeout*HZ/1000));
}

//---------------------------------------------------------------------------
// Stops the timeout timer.
//---------------------------------------------------------------------------
static void pid_timeout_stop(void)
{
	del_timer(&pid_timeout_list);
}

//---------------------------------------------------------------------------
// The function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void pid_timeout_fire(unsigned long data)
    {
      if (!atomic_read(&full_init))
      {
        return;
      }

      pid_timeout_stop();
#if 0
      if (atomic_read(&goal_atomic) != 0){
         // We only care about direction if we are moving
         if (atomic_read(&quad_direction) == 1){
            // Forward detected
            if (atomic_read(&movement_atomic) == MOVER_DIRECTION_REVERSE){
               do_fault(ERR_wrong_direction);
               atomic_set(&continuous_speed, 0);
               mover_speed_stop(1);
            }
         } else {
            // Reverse detected
            if (atomic_read(&movement_atomic) == MOVER_DIRECTION_FORWARD){
               do_fault(ERR_wrong_direction);
               atomic_set(&continuous_speed, 0);
               mover_speed_stop(1);
            }
         }
      }
#endif
      if (mover_type == MITPAMC){
         pid_step4();
      } else if (mover_type == MITP){
         pid_step3();
      } else {
         pid_step();
      }
      pid_timeout_start();
    }
//---------------------------------------------------------------------------
// Continuous timer stuff
//---------------------------------------------------------------------------

static void continuous_timeout_start()
{
   mod_timer(&continuous_timeout_list, jiffies+(CONTINUOUS_TIMEOUT_IN_MILLISECONDS*HZ/1000));
}

static void continuous_timeout_stop(void)
{
	del_timer(&continuous_timeout_list);
}

static void continuous_timeout_fire(unsigned long data)
{
      int vel, sensor, speed;
      if (!atomic_read(&full_init))
      {
        return;
      }

      continuous_timeout_stop();
      vel = atomic_read(&velocity);
      if (vel == 0){
         speed = atomic_read(&continuous_speed);
         if (speed > 0){
            sensor = atomic_read(&last_sensor); // home sensor
            if (sensor == MOVER_SENSOR_HOME) {
               mover_speed_set( speed );
            } else if (sensor == MOVER_SENSOR_END) {
               mover_speed_set( speed * (-1) );
            }
         }
      } else {
         speed = atomic_read(&continuous_speed);
         if (speed > 0){
            continuous_timeout_start();
         }
      }
}

//---------------------------------------------------------------------------
// Ramp timer stuff
//---------------------------------------------------------------------------
static void ramp_timeout_stop(void) {
   atomic_set(&ramp_step, 0);
   atomic_set(&ramp_start, atomic_read(&goal_atomic));
   atomic_set(&ramp_end, atomic_read(&goal_atomic));
	del_timer(&ramp_timeout_list);
}

static void ramp_timeout_start(int new_speed, int old_speed) {
   atomic_set(&ramp_step, (MOVER_RAMP_STEPS[mover_type] * abs(old_speed - new_speed)) - 1); // count down, start on 2nd step
   atomic_set(&ramp_start, old_speed);
   atomic_set(&ramp_end, new_speed);
   mod_timer(&ramp_timeout_list, jiffies+(RAMP_TIMEOUT_IN_MILLISECONDS*HZ/1000));
}

static void ramp_timeout_continue(void) {
   if (atomic_dec_return(&ramp_step) > 0) {
      mod_timer(&ramp_timeout_list, jiffies+(RAMP_TIMEOUT_IN_MILLISECONDS*HZ/1000));
   }
}

static void ramp_timeout_fire(unsigned long data) {
      int step, start, end;
      if (!atomic_read(&full_init))
      {
        return;
      }

      // next step in ramp (linear ramp)
      //atomic_inc(&goal_atomic);
      //atomic_set(&pid_set_point, percent_from_speed(atomic_read(&goal_atomic)));

      // next step in ramp (curved ramp)
      start = atomic_read(&ramp_start);
      end = atomic_read(&ramp_end);
      step = (MOVER_RAMP_STEPS[mover_type] * abs(end - start)) - atomic_read(&ramp_step);
      atomic_set(&pid_set_point, percent_curve_from_speed(start, end, step));
      SENDUSERCONNMSG( "nathan ramp %i %i %i %i %i", atomic_read(&ramp_start), atomic_read(&ramp_end), atomic_read(&ramp_step), step, atomic_read(&pid_set_point));

      ramp_timeout_continue();
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
static void timeout_fire(unsigned long data) {
    int rev_speed = atomic_read(&reverse_speed);
    if (!atomic_read(&full_init))
        {
        return;
        }

   DELAY_PRINTK(KERN_ERR "%s - %s() - the operation has timed out.\n",TARGET_NAME[mover_type], __func__);

    do_event(EVENT_TIMED_OUT); // reached timeout
    do_event(EVENT_STOPPED); // timeout was part of coasting or stopping

   SENDUSERCONNMSG( "nathan timeout_fire: goal %i dock %i", atomic_read(&goal_atomic), atomic_read(&find_dock_atomic));
    if (rev_speed != 0) { // stopped due to reversing
   SENDUSERCONNMSG( "randy before mover_speed_stop 3, rev,%i", rev_speed);
       mover_speed_stop(0); // after event_stopped
       // finish reversing now
       mover_speed_set(rev_speed);
       atomic_set(&reverse_speed, 0);
    } else if (atomic_read(&goal_atomic) == 0) { // stopped due to command
       mover_speed_stop(0); // after event_stopped
       // start docking now
       atomic_set(&dockTryCount, 0);
       dock_timeout_start(60000); // needs to wait at least 2 seconds for velocity to settle to zero
    } else if (atomic_read(&find_dock_atomic) == 1) { // stopped due to docking problems
       //do_event(EVENT_ERROR); // timeout wasn't part of coasting
       //do_fault(ERR_did_not_dock);
       // TODO -- when failure to find dock isn't a mechanical design flaw, undo the comments above
SENDUSERCONNMSG( "randy before mover_speed_stop 4");
       mover_speed_stop(0); // after event_error
       atomic_set(&find_dock_atomic, 0); // we're not finding anymore
    } else { // stopped due to problem moving
       do_event(EVENT_ERROR); // timeout wasn't part of coasting
       do_fault(ERR_no_movement);
SENDUSERCONNMSG( "randy before mover_speed_stop 5");
       mover_speed_stop(0); // after event_error
       atomic_set(&dockTryCount, 0);
       dock_timeout_start(60000); // needs to wait at least 2 seconds for velocity to settle to zero
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
    u32 status, this_t; //, dn1, dn2;
    // not used? -- u32 rb, cv;
    int dn1, dn2, oc;
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }
    if (!tc) return IRQ_HANDLED;

    status = __raw_readl(tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, SR)); // status register
    this_t = __raw_readl(tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, RA));
//    rb = __raw_readl(tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, RB));
//    cv = __raw_readl(tc->regs + ATMEL_TC_REG(ENCODER_PWM_CHANNEL, CV));

//    SENDUSERCONNMSG( "nathan detected quad: OVER:%i A:%i B:%i TIME:%i", status & ATMEL_TC_COVFS, status & ATMEL_TC_LDRAS, status & ATMEL_TC_MTIOB, this_t);

    // we're getting under way
    //atomic_add_unless(&getting_going, 1, UNDER_WAY[mover_type]); // keep counting up until we under way

    // Overlflow caused IRQ?
    if ( status & ATMEL_TC_COVFS )
        {
        atomic_set(&o_count, atomic_read(&o_count) + MAX_TIME);
// new method
        // no longer considered moving, ramp will adjust accordingly if it's still running
        if (atomic_read(&moving_atomic) == 3) {
            atomic_set(&moving_atomic, 2);
        }

        }

    // Pin A going high caused IRQ?
    if ( status & ATMEL_TC_LDRAS ) {
        // reset the timeout timer?
        if (CONTINUE_ON[mover_type] & 2) {
            timeout_timer_start(1);
        }

        // now considered moving, ramp will now only adjust the PID number
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
        oc = atomic_read(&o_count);
        atomic_set(&delta_t, this_t + oc);
        atomic_set(&o_count, 0);
        dn1 = atomic_read(&delta_t);
        dn2 = atomic_read(&quad_direction);
        atomic_set(&velocity, dn1 * dn2);
        atomic_set(&pid_vel, dn1 * dn2);
    } else {
        // Pin A did not go high
        if ( atomic_read(&o_count) >= MAX_OVER ) {
//               mover_speed_stop(1);
            atomic_set(&velocity, 0);
            atomic_set(&pid_vel, 0);
            atomic_set(&quad_direction, 0);
        }
    }

   // do position and velocity
   if (atomic_read(&doing_vel) == FALSE) {
      atomic_set(&doing_vel, TRUE);
      mod_timer(&velocity_timer_list, jiffies+((VELOCITY_DELAY_IN_MSECONDS*HZ)/1000));
   }
   if (atomic_read(&doing_pos) == FALSE) {
      atomic_set(&doing_pos, TRUE);
      mod_timer(&position_timer_list, jiffies+((POSITION_DELAY_IN_MSECONDS*HZ)/1000));
   }
   return IRQ_HANDLED;
}

//---------------------------------------------------------------------------
// track sensor "home"
//---------------------------------------------------------------------------
irqreturn_t track_sensor_home_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    int pos = atomic_read(&position), home = atomic_read(&home_max);
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

   DELAY_PRINTK("%s - %s() : %i\n",TARGET_NAME[mover_type], __func__, atomic_read(&movement_atomic));

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

    // reset home maximum location if necessary
    if (pos > home) {
       atomic_set(&home_max, pos);
    }

    // reset end minimum location (wheel slippage, testing, etc. can cause the old value to be wrong)
    atomic_set(&end_min, 5000);

   SENDUSERCONNMSG( "randy home_int %i", atomic_read(&movement_atomic));
    // check to see if this one needs to be ignored
    if ((atomic_read(&movement_atomic) == MOVER_DIRECTION_FORWARD) ||
            (atomic_read(&find_dock_atomic) == 1)) 
        {
        // ...then ignore switch
        return IRQ_HANDLED;
        }

   SENDUSERCONNMSG( "randy home_int %i   stopping", atomic_read(&movement_atomic));
    atomic_set(&last_sensor, MOVER_SENSOR_HOME); // home sensor
    do_event(EVENT_HOME_LIMIT); // triggered on home limit
    do_fault(ERR_stop_left_limit); // triggered on home limit
   SENDUSERCONNMSG( "randy before mover_speed_stop 6");
    mover_speed_stop(1);
    continuous_timeout_start();
    atomic_set(&dockTryCount, 0);
    dock_timeout_start(60000); // needs to wait at least 2 seconds for velocity to settle to zero

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// track sensor "end"
//---------------------------------------------------------------------------
irqreturn_t track_sensor_end_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    int pos = atomic_read(&position), end = atomic_read(&end_min);
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

   DELAY_PRINTK("%s - %s() : %i\n",TARGET_NAME[mover_type], __func__, atomic_read(&movement_atomic));

    // reset end minimum location if necessary
    if (pos < end) {
       atomic_set(&end_min, pos);
    }
    
    // reset home maximum location (wheel slippage, testing, etc. can cause the old value to be wrong)
    atomic_set(&home_max, -5000);

   SENDUSERCONNMSG( "randy end_int %i", atomic_read(&movement_atomic));
    // check to see if this one needs to be ignored
    if ((atomic_read(&movement_atomic) == MOVER_DIRECTION_REVERSE) ||
            (atomic_read(&find_dock_atomic) == 1)) 
        {
        // ...then ignore switch
        return IRQ_HANDLED;
        }

   SENDUSERCONNMSG( "randy end_int %i     stopping", atomic_read(&movement_atomic));
    atomic_set(&last_sensor, MOVER_SENSOR_END); // end sensor
   // If we have not set our internal track length, set it now if we can
   // so we can turn off the motor after we have traveled more that this distance
   // plus a fudge factor.
   if (atomic_read(&internal_length) == 0){
      atomic_set(&internal_length, atomic_read(&position));
   }

    do_event(EVENT_END_LIMIT); // triggered on end limit
    do_fault(ERR_stop_right_limit); // triggered on home limit
   SENDUSERCONNMSG( "randy before mover_speed_stop 7");
    mover_speed_stop(1);
    continuous_timeout_start();
    atomic_set(&dockTryCount, 0);
    dock_timeout_start(60000); // needs to wait at least 2 seconds for velocity to settle to zero

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// track sensor "dock"
//---------------------------------------------------------------------------
irqreturn_t track_sensor_dock_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

    // if no dock, don't stop on an errant dock interrupt
    // 10/21/12 Randy - lets not ignore dock interrupts for now
    // this is not working correctly at Fort Polk. Maybe the defaults
    // is not working correctly, or maybe the boards were flashed so
    // long ago they have the wrong default in the eeprom. I like the idea
    // but hopefully we really do not need this as it is 
    // up the the EEs to make sure their hardware works.
/*    if (dock_loc == 2 || dock_loc == 3) {
       return IRQ_HANDLED;
    }    */
    
    //DELAY_PRINTK("At Dock Sensor %s - %s() : %i\n",TARGET_NAME[mover_type], __func__, atomic_read(&find_dock_atomic));

    if (isMoverAtDock() != 0) {
       do_event(EVENT_DOCK_LIMIT); // triggered on dock limit
       do_fault(ERR_stop_dock_limit); // triggered on dock limit
   SENDUSERCONNMSG( "randy before mover_speed_stop 9");
         mover_speed_stop(1);
//    if (dock_loc == 0 || dock_loc == 1) {
//       at91_set_gpio_output(OUTPUT_CHARGING_RELAY, OUTPUT_CHARGING_RELAY_ACTIVE_STATE);
//    }
       atomic_set(&find_dock_atomic, 0);
       enable_battery_check(1);
       battery_check_is_docked(1);
       if (atomic_read(&found_home) == 0) { // if we haven't found our home position, we at least know *about* where the dock is
          if (dock_loc == 0) { // Dock at left
             atomic_set(&position, ((-1 * INCHES_PER_REV[mover_type] * DOCK_FEET_FROM_LIMIT[mover_type]) / TICKS_DIV)); // we're docked behind home, calculate ticks
          } else { // Dock at right
             atomic_set(&position, ((INCHES_PER_REV[mover_type] * (metersToFeet(track_len) + DOCK_FEET_FROM_LIMIT[mover_type])) / TICKS_DIV)); // we're docked past end, calculate ticks
          }
       } else {
          // we've found both home and the dock...
          if (atomic_read(&found_dock) == 0) { // if this is our first time...
             // ...remember where the dock is
             atomic_set(&dock_location, atomic_read(&position));
             atomic_set(&found_dock, 1);
          } else { // if this is not our first time...
             // ...reset our location to compensate for wheel spin, which causes our location to drift over time
             atomic_set(&position, atomic_read(&dock_location));
          }
       }
    } else {
       // Not on dock (if we have a dock)
       if (dock_loc == 0 || dock_loc == 1) {
          at91_set_gpio_output(OUTPUT_CHARGING_RELAY, OUTPUT_CHARGING_RELAY_INACTIVE_STATE);
       }
       do_event(EVENT_UNDOCKED);
       battery_check_is_docked(0);
       enable_battery_check(0);
       do_fault(ERR_left_dock_limit);
    }
//    atomic_set(&continuous_speed, 0);

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
   DELAY_PRINTK("timer_alloc(): %08x\n", (unsigned int) tc);

    if (!tc)
        {
        return -EINVAL;
        }

    mck_freq = clk_get_rate(tc->clk[MOTOR_PWM_CHANNEL[mover_type]]);
   DELAY_PRINTK("mck_freq: %i\n", (unsigned int) mck_freq);

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
           DELAY_PRINTK(KERN_ERR "ENCODER clk_enable() failed\n");
            return -EINVAL;
            }

    if (clk_enable(tc->clk[MOTOR_PWM_CHANNEL[mover_type]]) != 0)
            {
           DELAY_PRINTK(KERN_ERR "MOTOR clk_enable() failed\n");
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
           DELAY_PRINTK(KERN_ERR "request_irq(): Bad irq number or handler\n");
            }
        else if (status == -EBUSY)
            {
           DELAY_PRINTK(KERN_ERR "request_irq(): IRQ is busy, change your config\n");
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
           DELAY_PRINTK(KERN_ERR "request_irq() failed - invalid irq number (0x%08X) or handler\n", pin_number);
            }
        else if (status == -EBUSY)
            {
           DELAY_PRINTK(KERN_ERR "request_irq(): irq number (0x%08X) is busy, change your config\n", pin_number);
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
   DELAY_PRINTK("%s reverse: %i\n",__func__,  reverse);

    // turn on brake?
    if (USE_BRAKE[mover_type])
        {
        at91_set_gpio_output(OUTPUT_MOVER_APPLY_BRAKE, OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE);
        }

    // always put the H-bridge circuitry in the right state from the beginning
    if (DIRECTIONAL_H_BRIDGE[mover_type]) {
        // configure motor gpio for output and set initial output
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);

        // de-assert the neg inputs to the h-bridge
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);

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
    // Turn on charging relay if we don't have dock
    if (dock_loc == 2 || dock_loc == 3) {
      at91_set_gpio_output(OUTPUT_CHARGING_RELAY, OUTPUT_CHARGING_RELAY_ACTIVE_STATE);
      battery_check_is_docked(1);
      enable_battery_check(1);
      // we know *about* where the home is, assume that location
      if (dock_loc == 2) { // home at left
         atomic_set(&position, ((-1 * INCHES_PER_REV[mover_type] * DOCK_FEET_FROM_LIMIT[mover_type]) / TICKS_DIV)); // we're "docked" behind home, calculate ticks
      } else { // assume home at right
         atomic_set(&position, ((INCHES_PER_REV[mover_type] * (metersToFeet(track_len) + DOCK_FEET_FROM_LIMIT[mover_type])) / TICKS_DIV)); // we're "docked" past end, calculate ticks
      }
    } else if (isMoverAtDock() != 0) { // or if we're on it
      do_event(EVENT_DOCK_LIMIT);
      at91_set_gpio_output(OUTPUT_CHARGING_RELAY, OUTPUT_CHARGING_RELAY_ACTIVE_STATE);
      battery_check_is_docked(1);
      enable_battery_check(1);
      // we know *about* where the dock is, assume that location
      if (dock_loc == 0) { // Dock at left
         atomic_set(&position, ((-1 * INCHES_PER_REV[mover_type] * DOCK_FEET_FROM_LIMIT[mover_type]) / TICKS_DIV)); // we're docked behind home, calculate ticks
      } else { // assume Dock at right
         atomic_set(&position, ((INCHES_PER_REV[mover_type] * (metersToFeet(track_len) + DOCK_FEET_FROM_LIMIT[mover_type])) / TICKS_DIV)); // we're docked past end, calculate ticks
      }
    } else {
      do_event(EVENT_UNDOCKED);
      battery_check_is_docked(0);
      // assume we're on the opposite side from the dock
      if (dock_loc == 0) { // Dock at left
         atomic_set(&position, ((INCHES_PER_REV[mover_type] * (metersToFeet(track_len) + COAST_FEET_FROM_LIMIT[mover_type])) / TICKS_DIV)); // we coasted past end, calculate ticks
      } else { // assume Dock at right
         atomic_set(&position, ((-1 * INCHES_PER_REV[mover_type] * COAST_FEET_FROM_LIMIT[mover_type]) / TICKS_DIV)); // we coasted behind home, calculate ticks
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
        (hardware_set_gpio_input_irq(INPUT_MOVER_TRACK_END, INPUT_MOVER_END_OF_TRACK_PULLUP_STATE, track_sensor_end_int, "track_sensor_end_int") == FALSE) ||
        (hardware_set_gpio_input_irq(INPUT_MOVER_TRACK_DOCK, INPUT_MOVER_DOCK_PULLUP_STATE, track_sensor_dock_int, "track_sensor_dock_int") == FALSE)
            )
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
        // configure motor gpio for output and set initial output
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_POS, !OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE);

        // de-assert the neg inputs to the h-bridge
        at91_set_gpio_output(OUTPUT_MOVER_FORWARD_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);
        at91_set_gpio_output(OUTPUT_MOVER_REVERSE_NEG, OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE);

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
    del_timer(&position_timer_list);
    del_timer(&velocity_timer_list);
    del_timer(&dock_timeout_list);
    del_timer(&speed_timeout_list);
	 del_timer(&continuous_timeout_list);
	 del_timer(&ramp_timeout_list);

#ifndef TESTING_ON_EVAL
    free_irq(INPUT_MOVER_TRACK_HOME, NULL);
    free_irq(INPUT_MOVER_TRACK_END, NULL);
    free_irq(INPUT_MOVER_TRACK_DOCK, NULL);
#endif
    free_irq(INPUT_MOVER_TRACK_SENSOR_1, NULL);

    hardware_pwm_exit();

    return 0;
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
   DELAY_PRINTK("%s - %s()\n",TARGET_NAME[mover_type], __func__);

    // signal that an operation is in progress
#ifndef DEBUG_PID
//send_nl_message_multi("\n---------Changing moving_atomic to 2\n", error_mfh, NL_C_FAILURE);
#endif
    atomic_set(&moving_atomic, 2); // now considered moving

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
   DELAY_PRINTK("%s - %s()\n",TARGET_NAME[mover_type], __func__);

    // turn off horn?
    if (HORN_ON_IN_MSECONDS[mover_type] > 0)
        {
        at91_set_gpio_output(OUTPUT_MOVER_HORN, !OUTPUT_MOVER_HORN_ACTIVE_STATE);
        }
    }


//---------------------------------------------------------------------------
// Helper function to map 1000*percent values from speed values
//---------------------------------------------------------------------------
static int percent_curve_from_speed(int start, int end, int step) {
   int result;
   // if we can't ramp, don't
   if ((MOVER_RAMP_STEPS[mover_type] * abs(end - start)) == 0) {
      return percent_from_speed(end);
   }

   // start as a percent of the distance
   result = (step*1000)/(MOVER_RAMP_STEPS[mover_type]*abs(end - start));

   // fit result to curve
   result = (result * result) / 1000;

   // if we're going down, flip the curve
   if (end < start) {
      result = (-1 * (result)) + 1000;
   }

   // result is now a 0-1000 percent, scale it to fit between start and end, but still on a 0-1000 scale
   result = (result / (1000/(percent_from_speed(abs(end-start))))) + percent_from_speed(min(end, start));

   // return the final 0-1000 version
   return result;

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
    if (NUMBER_OF_SPEEDS[mover_type] <= 0) {
       return 0; // bad data, 0 effort
    }
    return (1000*speed/NUMBER_OF_SPEEDS[mover_type]); // 1000 * percent = 0 to 1000
}

//---------------------------------------------------------------------------
// Helper function to map pwm values from effort values
//---------------------------------------------------------------------------
static int pwm_from_effort(int effort) {
    // use straight percentage
    return MOTOR_PWM_RB_DEFAULT[mover_type] + (
      (effort * (MOTOR_PWM_END[mover_type] - MOTOR_PWM_RB_DEFAULT[mover_type]))
      / 1000);
}

static int pwm_from_effort_4(int effort) {
    int returnPWM = 0;
    // use straight percentage
    if ((atomic_read(&movement_atomic) == MOVER_DIRECTION_FORWARD)){
       return MOTOR_PWM_RB_DEFAULT[mover_type] + (
         (effort * (MOTOR_PWM_END[mover_type] - MOTOR_PWM_RB_DEFAULT[mover_type]))
         / 1000);
    } else if ((atomic_read(&movement_atomic) == MOVER_DIRECTION_REVERSE)){
       returnPWM = (1000 * MOTOR_PWM_END[mover_type]) - (MOTOR_PWM_RB_DEFAULT[mover_type] + (
         (effort * (MOTOR_PWM_END[mover_type] - MOTOR_PWM_RB_DEFAULT[mover_type]))
         / 1000));
    }
    return returnPWM;
}

//---------------------------------------------------------------------------
// Helper function to map pwm values to speed values
//---------------------------------------------------------------------------
#if 0
static int pwm_from_speed(int speed) {
    DELAY_PRINTK("pwm_from_speed(%i)\n", speed);
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
    DELAY_PRINTK("speed_from_pwm(%i)\n", ra);
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
#if 0
        char *msg = kmalloc(256, GFP_KERNEL);
        //snprintf(msg, 128, "setpoint:%i; detect:%i", new_speed, meas_vel);
        snprintf(msg, 256, "cs10: (100*(%i/%i)/%i)/%i", RPM_K[mover_type],ENC_PER_REV[mover_type],vel,VELO_K[mover_type]);
        send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
        kfree(msg);
#endif
//    if (vel != 0) {
//        vel = (100*(RPM_K[mover_type]/ENC_PER_REV[mover_type])/vel)/VELO_K[mover_type];
//    }
//    SENDUSERCONNMSG( "nathan giving velocity from: (100 * (%i / %i) / %i) / %i", RPM_K[mover_type], ENC_PER_REV[mover_type], vel, VELO_K[mover_type]);
    if (VELO_K[mover_type] <= 0) {
       return 0; // bad data, 0 effort
    }
    if (vel > 0) {
        vel = (100*(RPM_K[mover_type]/ENC_PER_REV[mover_type])/vel)/VELO_K[mover_type];
    } else if (vel < 0) {
        vel = -1 * ((100*(RPM_K[mover_type]/ENC_PER_REV[mover_type])/abs(vel))/VELO_K[mover_type]);
    }
    return vel;
}

static int current_pid_speed10(void) {
    int vel = atomic_read(&pid_vel);
#if 0
        char *msg = kmalloc(256, GFP_KERNEL);
        //snprintf(msg, 128, "setpoint:%i; detect:%i", new_speed, meas_vel);
        snprintf(msg, 256, "cs10: (100*(%i/%i)/%i)/%i", RPM_K[mover_type],ENC_PER_REV[mover_type],vel,VELO_K[mover_type]);
        send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
        kfree(msg);
#endif
//    if (vel != 0) {
//        vel = (100*(RPM_K[mover_type]/ENC_PER_REV[mover_type])/vel)/VELO_K[mover_type];
//    }
//    SENDUSERCONNMSG( "nathan giving velocity from: (100 * (%i / %i) / %i) / %i", RPM_K[mover_type], ENC_PER_REV[mover_type], vel, VELO_K[mover_type]);
    if (VELO_K[mover_type] <= 0) {
       return 0; // bad data, 0 effort
    }
    if (vel > 0) {
        vel = (100*(RPM_K[mover_type]/ENC_PER_REV[mover_type])/vel)/VELO_K[mover_type];
    } else if (vel < 0) {
        vel = -1 * ((100*(RPM_K[mover_type]/ENC_PER_REV[mover_type])/abs(vel))/VELO_K[mover_type]);
    }
    return vel;
}

static int calcPidError( int new, int input){
#if 0
    int pidError = 0, diff = 0;
    pidError = new - input; // set point - input
    if (pidError < 0) { // Going too fast
// If the error is too big set error as 1/2 of the desired speed
      if (pidError < ((((-1) * new) * 3) / 2)){
         pidError = (((-1) * new) / 2);
      } else {
// Allow error to be less than actual error
// if within a percentage. This will allow
// the effort to float down. This is especially
// important at low speeds.
      if (new <= 0) {
         return 0;
      }
      diff = abs(pidError) * 100 / new;
      if (diff < 10) {
         pidError = 0;
      } else if (diff < 15) {
         pidError *= -1;
         pidError /= 2;
      } else if (diff < 20) {
         pidError *= -1;
         pidError /= 3;
      } else {
         pidError /= 2;
      }
      }
    } else {
    }
    return pidError;
#endif
    return new - input; // just give the raw error
}

#if 0 /* not used? */
static int calcTargetRatio( int input, int new){
   int rtnVal = 0;
   int ratio = input * 100;
   int diff = 0;
   if (new <= 0) {
      return 0;
   }
   if (IS_MAT){ //MAT
      if (new <= 50) diff = 10;
      else if (new <= 100) diff = 10;
      else if (new <= 150) diff = 7;
      else if (new <= 200) diff = 10;
      else if (new <= 250) diff = 15;
      else diff = 20;
      if (ratio > (100 + diff)){
         return 1;
      } else if (ratio < 100){
         return -1;
      }
   } else if (IS_MIT){ // MIT
      if (new <= 35) diff = 20;
      else if (new <= 50) diff = 4;
      else if (new <= 100) diff = 4;
      else if (new <= 150) diff = 7;
      else diff = 10;
      if (ratio > (100 + diff)){
         rtnVal = 1;
      } else if (ratio < 100){
         rtnVal = -1;
      }
   }

   return rtnVal;
}
#endif

//---------------------------------------------------------------------------
// Get/set functions for external control from within kernel
//---------------------------------------------------------------------------
int mover_speed_get(void) {
   int rtnSpeed;
   DELAY_PRINTK("mover_speed_get\n");
   rtnSpeed = current_speed10();  // changed from current_speed
#if 0
   if (rtnSpeed < 1000) {
      rtnSpeed = atomic_read(&last_reported_speed);
   } else if (rtnSpeed > 1000) {
      rtnSpeed = atomic_read(&last_reported_speed);
   }
   atomic_set(&last_reported_speed, rtnSpeed);
#endif
   return rtnSpeed;  // changed from current_speed
}
EXPORT_SYMBOL(mover_speed_get);

int mover_continuous_speed_get(void) {
   int speed;
   DELAY_PRINTK("mover_speed_get\n");
   speed = atomic_read(&continuous_speed);
   return speed;
}
EXPORT_SYMBOL(mover_continuous_speed_get);

int mover_set_continuous_move(int c) {
   int sensor, speed;
   DELAY_PRINTK("mover_set_continuous_move\n");
   // check to see if we're sleeping or not
   if (atomic_read(&sleep_atomic) == 1) {
         return 0;
   }
   atomic_set(&find_dock_atomic, 0);
   dock_timeout_stop();
   if (c != 0) {
      atomic_set(&continuous_speed, abs(c));
      speed = abs(c);
      if (isMoverAtDock()){
         if (dock_loc == 1) { // Dock at end away from home
            speed *= -1;
         }
      } else {
         sensor = atomic_read(&last_sensor); // home sensor
         if (sensor == MOVER_SENSOR_END) {
            speed *= -1;
         } else {
            if (home_loc == 1) { // Dock at end away from home
               speed *= -1;
            }
         }
      }
      mover_speed_set( speed );
      speed_timeout_start(0, speed);
   } else {
      atomic_set(&continuous_speed, 0);
      dock_timeout_stop();
   SENDUSERCONNMSG( "randy before mover_speed_stop 10");
      mover_speed_stop(1);
   }
   return 0;
}
EXPORT_SYMBOL(mover_set_continuous_move);

int mover_set_moveaway_move(int c) {
   int sensor, speed;
   DELAY_PRINTK("mover_set_moveaway_move\n");
   SENDUSERCONNMSG( "randy mover_set_moveaway_move %i", c);
   // check to see if we're sleeping or not
   if (atomic_read(&sleep_atomic) == 1) {
   SENDUSERCONNMSG( "randy mover_set_moveaway_move sleeping");
         return 0;
   }
   atomic_set(&find_dock_atomic, 0);
   atomic_set(&continuous_speed, 0);
   speed_timeout_stop();
   if (c != 0) {
      speed = abs(c);
      if (isMoverAtDock()){
   SENDUSERCONNMSG( "randy mover_set_moveaway_move at dock");
         if (dock_loc == 1) { // Dock on right
            speed *= -1;
         }
      } else {
         sensor = atomic_read(&last_sensor);
         if (sensor == MOVER_SENSOR_END) { // On right
   SENDUSERCONNMSG( "randy mover_set_moveaway_move at right sensor");
            speed *= -1;
         } else {
            if (home_loc == 1) { // Home on right
   SENDUSERCONNMSG( "randy mover_set_moveaway_move home on right");
               speed *= -1;
            }
         }
      }
      mover_speed_set( speed );
      speed_timeout_start(0, speed);
   } else {
   SENDUSERCONNMSG( "randy before mover_speed_stop 11");
      mover_speed_stop(1);
   }
   return 0;
}
EXPORT_SYMBOL(mover_set_moveaway_move);

int mover_set_move_speed(int speed) {
   atomic_set(&continuous_speed, 0);
   atomic_set(&find_dock_atomic, 0);
   // check to see if we're sleeping or not
   if (atomic_read(&sleep_atomic) == 1) {
         return 0;
   }
   SENDUSERCONNMSG( "randy mover_set_move_speed %i", speed);
   if (speed != 0) {
      dock_timeout_stop();
      speed_timeout_start(0, speed);
      return mover_speed_set( speed );
   } else {
   SENDUSERCONNMSG( "randy before mover_speed_stop 12");
      mover_speed_stop(1);
      return 1;
   }
}
EXPORT_SYMBOL(mover_set_move_speed);

void set_lifter_position(int val)
{
    atomic_set(&lifter_position, val);
}
EXPORT_SYMBOL(set_lifter_position);

void set_lifter_fault(int val)
{
    atomic_set(&lifter_fault, val);
}
EXPORT_SYMBOL(set_lifter_fault);

int mover_set_speed_stop() {
    atomic_set(&continuous_speed, 0);
   SENDUSERCONNMSG( "randy before mover_speed_stop 13");
    mover_speed_stop(1);
    atomic_set(&find_dock_atomic, 0);
    dock_timeout_stop(); // stop docking as well
    return 1;
}
EXPORT_SYMBOL(mover_set_speed_stop);

extern int mover_position_get() {
    int pos = atomic_read(&position);
    if (TICKS_DIV <= 0) {
       return 0; // bad data, return 0
    }
    return ((INCHES_PER_REV[mover_type]*pos)/(TICKS_DIV))/12; // inches to feet
}
EXPORT_SYMBOL(mover_position_get);

int isMoverAtDock(void){
   int retVal = 0;
    if (at91_get_gpio_value(INPUT_MOVER_TRACK_DOCK) == INPUT_MOVER_DOCK_ACTIVE_STATE){
      retVal = 1;
    }
    return retVal;
}

void mover_find_dock(void) {

   SENDUSERCONNMSG( "randy mover_find_dock,%i" ,atomic_read(&dockTryCount));
   if (isMoverAtDock() != 0) {
   SENDUSERCONNMSG( "randy mover_find_dock, at dock");
      return;
   } // At the dock already
   if (atomic_read(&dockTryCount) > DOCK_RETRY_COUNT[mover_type]) {
      // atomic_set(&find_dock_atomic, 0); -- don't reset! will cause timeout_fire to not report ERR_did_not_dock!
   SENDUSERCONNMSG( "randy mover_find_dock, over retry count");
      return;
   } // Tried too many times
   if (IS_MAT || IS_MIT){
      atomic_set(&find_dock_atomic, 1);
      atomic_inc(&dockTryCount);
      if (dock_loc == 1) { // Dock at end away from home
         mover_speed_set(1); // Go Slow
         dock_timeout_start(60000);
      } else if (dock_loc == 0) { // Dock at home end
         mover_speed_set(-1); // Go Slow
         dock_timeout_start(60000);
      } else {
         atomic_set(&find_dock_atomic, 0); // No dock, stop looking
      }
   }
}

void mover_go_home(void) {
   atomic_set(&continuous_speed, 0);
   atomic_set(&find_dock_atomic, 0);
   dock_timeout_stop();
   if (IS_MIT){
      if (home_loc == 1) { // Dock at end away from home
         if (atomic_read(&last_sensor) != MOVER_SENSOR_END){
               mover_speed_set(28); // Get there kind of fast
         } else {
            if (dock_loc == 0 && isMoverAtDock()) { // Dock at end away from home
               mover_speed_set(28); // Get there kind of fast
            }
         }
      } else if (home_loc == 0) { // Dock at home end
         if (atomic_read(&last_sensor) != MOVER_SENSOR_HOME){
               mover_speed_set(-28); // Get there kind of fast
         } else {
            if (dock_loc == 1 && isMoverAtDock()) {
               mover_speed_set(-28); // Get there kind of fast
            }
         }
      }
   }
}
EXPORT_SYMBOL(mover_go_home);

//---------------------------------------------------------------------------
// Sleep the device
//---------------------------------------------------------------------------
int mover_sleep_set(int value) {
DELAY_PRINTK("mover_sleep_set(%i)\n", value);
   atomic_set(&continuous_speed, 0);
   if (value == 1) {
      // start sleeping by stopping
//      mover_speed_stop(1);
//      Sleeping movers can move to the dock to charge
//    Just move to the end of the track where the dock is.
//    The rest happens automatically
      if (dock_loc == 1) { // Dock at end away from home
         mover_speed_set(1); // Go Slow
      } else if (dock_loc == 0) { // Dock at home end
         mover_speed_set(-1); // Go Slow
      }
   } else if (value == 3) {
//      mover_find_dock();
//    Just move to the end of the track where the dock is.
//    The rest happens automatically
      if (dock_loc == 1) { // Dock at end away from home
         mover_speed_set(1); // Go Slow
      } else if (dock_loc == 0) { // Dock at home end
         mover_speed_set(-1); // Go Slow
      }
      return 1; // Just return, we don't want to change the sleep state
   }
   atomic_set(&sleep_atomic, value);
   return 1;
}
EXPORT_SYMBOL(mover_sleep_set);

//---------------------------------------------------------------------------
// Get device sleep state
//---------------------------------------------------------------------------
int mover_sleep_get(void) {
DELAY_PRINTK("mover_sleep_get()\n");
   return atomic_read(&sleep_atomic);
}
EXPORT_SYMBOL(mover_sleep_get);

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
        if (vel <= 0 || ENC_PER_REV[mover_type]) {
           return 0; // bad data, return 0
        }
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
    DELAY_PRINTK("%s - %s() : user command stop\n",TARGET_NAME[mover_type], __func__);

   SENDUSERCONNMSG( "randy before mover_speed_stop 14");
        mover_speed_stop(1);
        }

    // check if an operation is in progress, if so ignore any command other than 'stop' above
    else if (atomic_read(&moving_atomic) > 0)
        {
    DELAY_PRINTK("%s - %s() : operation in progress, ignoring command.\n",TARGET_NAME[mover_type], __func__);
        }

    else if (sysfs_streq(buf, "forward"))
        {
    DELAY_PRINTK("%s - %s() : user command forward\n",TARGET_NAME[mover_type], __func__);

        hardware_movement_set(MOVER_DIRECTION_FORWARD);
        }

    else if (sysfs_streq(buf, "reverse"))
        {
    DELAY_PRINTK("%s - %s() : user command reverse\n",TARGET_NAME[mover_type], __func__);
        status = size;

        hardware_movement_set(MOVER_DIRECTION_REVERSE);
        }
    else
        {
    DELAY_PRINTK("%s - %s() : unknown user command %s\n",TARGET_NAME[mover_type], __func__, buf);
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
        atomic_set(&moving_atomic, 1);
        atomic_set(&last_sensor, MOVER_SENSOR_UNKNOWN);
        hardware_movement_set(MOVER_DIRECTION_FORWARD);
        hardware_motor_on(MOVER_DIRECTION_FORWARD);
        pid_timeout_stop();
        __raw_writel(value, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
        __raw_writel(value, tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB));
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
    int rc = atomic_read(&tc_clock), r;
    switch (rc) {
       case 1: rc = 66500000; break;
       case 2: rc = 16625000; break;
       case 3: rc = 4156250; break;
       case 4: rc = 1039062; break;
       default: rc = 1; break;
    }
    r = __raw_readl(tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RC)); // convert to hz
    if (r <= 0) {
       return 0;
    }
    rc /= r;
    return sprintf(buf, "%d\n", rc);
    }

//---------------------------------------------------------------------------
// Handles writes to the ra attribute through sysfs (for debugging only)
//---------------------------------------------------------------------------
static ssize_t hz_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
    long value;
    ssize_t status;
    int ra, rc=1, nc, p; // ra, rc and new clock

DELAY_PRINTK("Attempting to change hz\n");

    status = strict_strtol(buf, 0, &value);
    if (status == 0) {
        if (rc > 2000) { return 0;}
        if (rc < 200) { return 0;}

        nc = atomic_read(&tc_clock);
DELAY_PRINTK("changing hz to %i\n", value);

     // find correct clock multiplier
     while (nc>0 && nc<5) {
       switch (nc) {
           case 1: rc = 66500000; break;
           case 2: rc = 16625000; break;
           case 3: rc = 4156250; break;
           case 4: rc = 1039062; break;
       }

       if (value <= 0) {
          return 0;
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
DELAY_PRINTK("changing clock to %i\n", nc);

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

DELAY_PRINTK("done\n");
        default: return -EINVAL; break;
        }

    }
DELAY_PRINTK("failed with %i\n", status);
    return status;
}

//---------------------------------------------------------------------------
// Handles writes to the speed attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t speed_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    long value;
    ssize_t status;
    DELAY_PRINTK("speed_store");

    status = strict_strtol(buf, 0, &value);
    if ((status == 0) &&
           (value >= 0) &&
           (value <= NUMBER_OF_SPEEDS[mover_type]))
        {
        DELAY_PRINTK("speed_store - speed: %i\n", value);
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
         int dir;
         int len;
         int pos;
#ifdef SEND_POS
         struct timespec time_now = current_kernel_time();
         struct timespec time_d = timespec_sub(time_now, time_start);
#endif
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }

        // only notify sysfs if we've move at least half a leg
        if (abs(atomic_read(&position_old) - atomic_read(&position)) > (TICKS_PER_LEG[mover_type]/TICKS_DIV/2)) {
            atomic_set(&position_old, atomic_read(&position)); 
#ifndef WOBBLE_DETECT
#ifndef ACCEL_TEST
            do_event(EVENT_POSITION); // notify mover driver
#endif
#endif
            target_sysfs_notify(&target_device_mover_generic, "position");
#ifdef SEND_POS
        SENDUSERCONNMSG( "pid6,t,%i,e,0,u,0,r,0,y,0,o,0,P,0,I,0,D,0,l,%i", (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000), atomic_read(&position)); // use the same output format as the SEND_PID message, so it's easier to parse
#endif
        }

            // See if our position is out of bounds
            // The numbers 100 and -100 are randomly picked 
            dir = atomic_read(&movement_atomic);
            len = atomic_read(&internal_length);
            pos = atomic_read(&position);
            if (len != 0 &&
                  (((dir == MOVER_DIRECTION_FORWARD) && (pos > (len + 200))) ||
                  ((dir == MOVER_DIRECTION_REVERSE) && (pos < -200)))) {
               do_fault(ERR_stop_by_distance);
//               mover_speed_stop(1);
            }
        }

static void do_velocity(struct work_struct * work) {
    int vel, old_vel = 0;
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    // use the calculated speed to see if we've changed a significant digit or not
    vel = current_speed10();
    
    // only notify sysfs if we've changed velocity
    old_vel = atomic_read(&velocity_old);
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

#ifdef SEND_DEBUG
void sendUserConnMsg( char *fmt, ...){
    va_list ap;
    char *msg;
    va_start(ap, fmt);
     msg = kmalloc(256, GFP_KERNEL);
     if (msg){
         vsnprintf(msg, 256, fmt, ap);
         DELAY_PRINTK(msg);
         send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
         kfree(msg);
     }
   va_end(ap);
}
#endif

// new method
static void pid_step() {
    int new_speed=1, input_speed=0, i, error_sum=0, input_speed_old=0, pid_effort=0, pid_p=0, pid_i=0, pid_d=0;
   int direction, delta;
   // not used? -- using_ticks
   // not used? -- targetRatio;
   // not used? -- int a, b, c;
   struct timespec time_now = current_kernel_time();
   struct timespec time_d = timespec_sub(time_now, time_start);
   static int last_delta = -1; // invalid initial value for this static int
   //static int pid_count = 0;

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    //using_ticks = pid_ticks;
    //pid_ticks = 0;
    
    new_speed = atomic_read(&pid_set_point); // read desired pid set point
    input_speed_old = percent_from_speed(abs(current_speed10())); // ex on MIT : 0-20mph => 0-1000, so 5mph = 250, 2.5 mph = 125, etc.
    direction = atomic_read(&quad_direction);
    
    // delta is time since last pid_step, which used to be just delta_t, now it is time_d...units don't matter as we will adjust the D gain abitrarily anyway
    //delta = atomic_read(&delta_t); // delta was in clock ticks...
    delta = (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000l); // ...but is now in milliseconds
    if (last_delta != -1) { // check that we're valid
       int temp = delta;
       delta = delta - last_delta; // ... and is now the time since the last time we ran pid_step
       last_delta = temp; // remember
    } else {
       last_delta = delta; // remember
       // first time around we won't mess with delta
    }
    // old delta was speed dependent, new delta should always be about the same

    // read input speed (adjust speed10 value to 1000*percent)
    input_speed = percent_from_speed(abs(current_pid_speed10())); // ex on MIT : 0-20mph => 0-1000, so 5mph = 250, 2.5 mph = 125, etc.
    atomic_set(&pid_vel, 0);
    if (!spin_trylock(&pid_lock)) {
        return;
    }

//    pid_error = new_speed - input_speed; // set point - input
    pid_error = calcPidError(new_speed, input_speed); // set point - input

    // put calculated current error to end of list
    if (input_speed != 0 || IS_MAT) {
       pid_errors[MAX_PID_ERRORS-1] = pid_error;
    }
    //pid_total_error += pid_error;

    // calculate sum of past errors
    for (i=0; i<MAX_PID_ERRORS; i++) {
       error_sum += pid_errors[i];
    }
    //error_sum = pid_total_error;
    //if (error_sum < (pid_error * 5)) error_sum = pid_error * 5;


       // do the PID calculations

       // individual pid steps to make full equation more clean
       if (kp_d > 0){
         pid_p = (((1000 * kp_m * pid_error) / kp_d) / 1000);
       } else {
         pid_p = ((1000 * kp_m * pid_error) / 1000);
       }
       if (ki_d > 0){
         //pid_i = (((1000 * ki_m * pid_total_error) / ki_d) / 1000); // re-calculate entire error in one step
         pid_i = (((1000 * ki_m * error_sum) / ki_d) / 1000); // re-calculate recent error in one step
       }

/*       a = 1000 * kd_m;
       b = pid_error - pid_last_error;
       c = (kd_d * delta) / 1000;
       pid_d = ((a * b) / c); */
       if (kd_d > 0 && delta > 0){
          //pid_d = ((((1000 * kd_m) * (pid_last_last_error - pid_error)) / (kd_d * delta)) / 1000); // last last error
          //pid_d = ((((1000 * kd_m) * (pid_last_error - pid_error)) / (kd_d * delta)) / 1000); // last error
          //pid_d = ((((1000 * kd_m) * (pid_last_last_error - pid_error)) / (kd_d * 1)) / 1000); // last last error (pre-calculated, constant delta) -- POSITIVE
          //pid_d = ((((1000 * kd_m) * (pid_error - pid_last_last_error)) / (kd_d * 1)) / 1000); // last last error (pre-calculated, constant delta) -- NEGATIVE
          //pid_d = ((((1000 * kd_m) * (pid_last_error - pid_error)) / (kd_d * 1)) / 1000); // last error (pre-calculated, constant delta) -- POSITIVE (normal) VERSION negates changes in pid_p
          pid_d = ((((1000 * kd_m) * (pid_last_last_error - pid_last_error)) / (kd_d * 1)) / 1000); // last error (pre-calculated, constant delta) -- POSITIVE (past error) VERSION negates changes in pid_p
          //pid_d = ((((1000 * kd_m) * (pid_error - pid_last_error)) / (kd_d * 40)) / 1000); // last error (pre-calculated, constant delta) -- NEGATIVE VERSION negates changes in pid_i rather than pid_p
          //pid_d = ((((1000 * kd_m) * ((pid_p+pid_i) - pid_last_effort)) / (kd_d * 40)) / 1000); // last error (pre-calculated, constant delta) -- NON-STANDARD VERSION negates changes in effort
          //pid_d /= 2; // average the last and last last errors

       }

#if 0
   // find auto-calibration?
   if (pid_auto_calibration_div == 0 || pid_auto_calibration_mult == 0) {
      if (pid_last_error < pid_error && input_speed > 0) {
         // first peak, calibrate to it
         if (input_speed > new_speed) {
            // too fast
            pid_auto_calibration_mult = new_speed;
            pid_auto_calibration_div = (-10 * pid_error);
         } else if (input_speed == new_speed) {
            // just right
            pid_auto_calibration_mult = 1;
            pid_auto_calibration_div = 1;
         } else {
            // too slow
            pid_auto_calibration_mult = (10 * pid_error);
            pid_auto_calibration_div = new_speed;
         }
         pid_p *= pid_auto_calibration_mult;
         pid_p /= pid_auto_calibration_div;
         pid_i *= pid_auto_calibration_mult;
         pid_i /= pid_auto_calibration_div;
      }
   } else {
      // have already found calibration values, use them
      pid_p *= pid_auto_calibration_mult;
      pid_p /= pid_auto_calibration_div;
      pid_i *= pid_auto_calibration_mult;
      pid_i /= pid_auto_calibration_div;
   }
#endif

       // descrete PID algorithm gleamed from Scott's brain
       //pid_effort = pid_last_effort + pid_p + pid_i + pid_d;
       pid_effort = pid_p + pid_i + pid_d;
       //pid_effort = ((new_speed * pid_effort) / 1000); // scale effort based on which speed is chosen

       // average last effort and current effort to minimize accelerations
       //pid_effort = (pid_last_effort + pid_effort) / 2;

   //delay_printk( "pid0,r,%d,p,%i,i,%i,d,%i,e,%i,t,%i", pid_error, pid_p, pid_i, pid_d, pid_effort,direction);
//   SENDUSERCONNMSG( "pid0,r,%d,p,%i,i,%i,d,%i,e,%i,t,%i", pid_error, pid_p, pid_i, pid_d, pid_effort,direction);
    // max effort to 100%
   if (new_speed != 0) {
      /*if (isMoverAtDock() != 0) {
        pid_effort += 1000;
      } */
      // this stuff completely screws with the PID
      /*
      int gg = atomic_read(&getting_going);
      if (gg < UNDER_WAY[mover_type]) {
         pid_count = 0;
         pid_effort = GETTING_GOING_EFFORT[mover_type];
         //atomic_set(&going_base, 0);
      } else if (++pid_count < UNDER_WAY_MERGE[mover_type]) {
         // mix the getting going value and the pid_effort together, weighted based on how many pid_steps we've taken
         int pe = pid_effort, gg = GETTING_GOING_EFFORT[mover_type];
         pe *= ((pid_count * 100) / UNDER_WAY_MERGE[mover_type]);
         gg *= (100 - ((pid_count * 100) / UNDER_WAY_MERGE[mover_type]));
         pid_effort = (pe/100) + (gg/100);
      }
      */

   }
   /*
    // check wheel tightness by observing the first dip in speed after the error reset
    if (new_speed > 10 && wheel_too_tight == -1 && !pid_reset_error && input_speed <  pid_last_speed) {
       wheel_too_tight = -2; // check our values when the input speed is greater than the last speed...
    } else if (new_speed > 10 && wheel_too_tight == -2 && input_speed > pid_last_speed) {
       if (pid_last_speed <= (new_speed / 2)) {
          // dropped down to less than half of the desired speed, we're too tight
          wheel_too_tight = 1;
       } else {
          // we're fine, adjust the total error as normal
          wheel_too_tight = 0;
       }
    }*/

    // move errors back
//    if (pid_last_error != pid_last_last_error || pid_last_error == 0) {
//       pid_last_last_error = pid_last_error; // used to get closer to actual derivitive, as pid_last_error stays stagnant for too long
//    }
    if (input_speed != 0) {
       pid_last_last_error = pid_last_error; // used to get closer to actual derivitive
       pid_last_error = pid_error;
    }
    /*
    if (pid_last_speed != input_speed) {
       pid_last_speed = input_speed;
    }*/
    // move past errors back in error buffer
    if (input_speed != 0) {
       for (i=0; i<MAX_PID_ERRORS-1; i++) {
          pid_errors[i] = pid_errors[i+1];
       }
    }
    /*
    if (!pid_reset_error) {
       if (wheel_too_tight != 1) { // don't reset total error if our wheel is attached too tightly
          pid_error /= 2; // once we've got through our initial stuff, add in only half the error so "I" reacts slower
       } else {
          // when we're too tight, lose 2/3rds rather than 1/2
          pid_error *= 2; // once we've got through our initial stuff, add in only half the error so "I" reacts slower
          pid_error /= 3; // once we've got through our initial stuff, add in only half the error so "I" reacts slower
       }
    }*/
//    if (input_speed != 0) {
//       pid_total_error += pid_error;
//    }
    /*if (pid_reset_error && input_speed > new_speed) {
       if (wheel_too_tight != 1) { // don't reset total error if our wheel is attached too tightly
          pid_total_error /= 2; // most of our time spent accelerating was waiting for the motor controller
       } else {
          // when we're too tight, lose 2/3rds rather than 1/2
          pid_total_error *= 2; // most of our time spent accelerating was waiting for the motor controller
          pid_total_error /= 3; // most of our time spent accelerating was waiting for the motor controller
       }
       pid_reset_error = 0; // ... but only do it once per move
    }*/
    if (new_speed == 0) {
       pid_effort = 0; // when stopping is 0
    } else {
#if 0
      if (IS_MIT) {
         if (new_speed < 50) {
            if (pid_effort < 0) {
               pid_effort /= 4;
            } else if (pid_effort < 200) {
               pid_effort = 150;
               pid_effort = 175;
               pid_effort += 250;
            }
//            if (atomic_read(&movement_atomic) == MOVER_DIRECTION_FORWARD) {
//               pid_effort += 100;
//            }
         }
      }
#endif
       // clamp effort to valid values
       if (IS_MIT) {
          pid_effort = max(min(pid_effort, 1000), atomic_read(&movement_atomic) == MOVER_DIRECTION_FORWARD ? 100 : 80); // non-zero min to always be touching the throttle line, so the motor controller doesn't reset its timer, and be different forward/reverse to compensate for the gear box
       } else if (IS_MAT) {
          pid_effort = max(min(pid_effort, 1000), 1); // just barely touch line to not engage breaks
       } else {
          pid_effort = max(min(pid_effort, 1000), 25); // min of 25, max of 1000
       }
    }
    
         
#ifdef SEND_PID
    //if ((pid_count++ % 10) == 0) {
       //SENDUSERCONNMSG( "pid4,t,%i,e,%i,u,%i,r,%i,y,%i,d,%i,P,%i,I,%i,D,%i,*,%i,/,%i", (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000), pid_error, max(min(pid_effort, 1000), 0), new_speed, input_speed,delta, pid_p, pid_i, pid_d, pid_auto_calibration_mult, pid_auto_calibration_div);
       SENDUSERCONNMSG( "pid5,t,%i,e,%i,u,%i,r,%i,y,%i,o,%i,P,%i,I,%i,D,%i,l,%i", (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000), pid_error, pid_effort, new_speed, input_speed, input_speed_old, pid_p, pid_i, pid_d, atomic_read(&position));
    //}
#endif
/*
    if (pid_effort > 800){
      pidEffortCounter ++;
      if (pidEffortCounter > 20){
   SENDUSERCONNMSG( "maximum effort exceeded");
         mover_speed_stop(1);
         pid_effort = 0;
      }
    } else {
      pidEffortCounter = 0;
    }
*/
    // convert effort to pwm
    new_speed = pwm_from_effort(pid_effort);

    // These change the pwm duty cycle
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB));

    // move effort values
    pid_last_effort = pid_effort;

    // unlock for next time
    spin_unlock(&pid_lock);
}

#if 0 /* new method not currently used */
// new method
static void pid_step1() {
    int req_vel=1, meas_vel=0, effort=0;
    // not used? -- int error_sum=0, i, pid_p=0, pid_i=0, pid_d=0;
   int pwm_sig = 0, err_vel=0, base = 0;
   int using_ticks, direction;
   // not used? -- int targetRatio, delta;
   // not used? -- int a, b, c;
#ifdef SEND_DEBUG
   struct timespec time_now = current_kernel_time();
   struct timespec time_d = timespec_sub(time_now, time_start);
#endif

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    using_ticks = pid_ticks;
    pid_ticks = 0;
    
    req_vel = atomic_read(&pid_set_point); // read desired pid set point
    meas_vel = atomic_read(&velocity); // read speed from quad_encoder
    direction = atomic_read(&quad_direction);

    // read input speed (adjust speed10 value to 1000*percent)
    meas_vel = (1000 * abs(current_speed10())) / NUMBER_OF_SPEEDS[mover_type];
    if (!spin_trylock(&pid_lock)) {
        return;
    }

    err_vel = req_vel - meas_vel; // set point - input
//    error = calcPidError(req_vel, meas_vel); // set point - input

       // descrete PID algorithm gleamed from Scott's brain
       if (req_vel > 0){
          base = 100;
          if (req_vel < 50){
            base = 175;
          }
       } else {
         base = 0;
       }
       effort = 20 * err_vel + base;
/*
       if (effort > 800) {
         effort = 800;
       } */


    if (req_vel == 0) {
       effort = max(effort, 0); // minimum when stopping is 0
    }
    // convert effort to pwm
    pwm_sig = pwm_from_effort(effort);

   SENDUSERCONNMSG( "pid1,t,%i,e,%i,u,%i,r,%i,y,%i,p,%i", (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000), err_vel, effort, req_vel, meas_vel,pwm_sig);
   if (atomic_read(&doing_vel) == FALSE)
       {
       atomic_set(&doing_vel, TRUE);
       mod_timer(&velocity_timer_list, jiffies+((VELOCITY_DELAY_IN_MSECONDS*HZ)/1000));
       }

    // These change the pwm duty cycle
    __raw_writel(max(pwm_sig,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
    __raw_writel(max(pwm_sig,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB));

    // unlock for next time
    spin_unlock(&pid_lock);
}
#endif

// new method
// negative effort less than 50%
// 0 effort uses 50% pid
// positive effort more than 50%
// 
#if 0
static void pid_step2() {
    int new_speed=1, input_speed=0, i, error_sum=0, input_speed_old=0, pid_effort=0, pid_p=0, pid_i=0, pid_d=0;
    int new_pid_effort = 500; // 50% effort for new controller is 0 effort
   int direction, delta;
   // not used? -- using_ticks
   // not used? -- targetRatio;
   // not used? -- int a, b, c;
   struct timespec time_now = current_kernel_time();
   struct timespec time_d = timespec_sub(time_now, time_start);
   static int last_delta = -1; // invalid initial value for this static int
   //static int pid_count = 0;

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    //using_ticks = pid_ticks;
    //pid_ticks = 0;
    
    new_speed = atomic_read(&pid_set_point); // read desired pid set point
    input_speed_old = percent_from_speed(abs(current_speed10())); // ex on MIT : 0-20mph => 0-1000, so 5mph = 250, 2.5 mph = 125, etc.
    direction = atomic_read(&quad_direction);
    
    // delta is time since last pid_step, which used to be just delta_t, now it is time_d...units don't matter as we will adjust the D gain abitrarily anyway
    //delta = atomic_read(&delta_t); // delta was in clock ticks...
    delta = (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000l); // ...but is now in milliseconds
    if (last_delta != -1) { // check that we're valid
       int temp = delta;
       delta = delta - last_delta; // ... and is now the time since the last time we ran pid_step
       last_delta = temp; // remember
    } else {
       last_delta = delta; // remember
       // first time around we won't mess with delta
    }
    // old delta was speed dependent, new delta should always be about the same

    // read input speed (adjust speed10 value to 1000*percent)
    input_speed = percent_from_speed(abs(current_pid_speed10())); // ex on MIT : 0-20mph => 0-1000, so 5mph = 250, 2.5 mph = 125, etc.
    atomic_set(&pid_vel, 0);
    if (!spin_trylock(&pid_lock)) {
        return;
    }
    if (new_speed != 0) {
      if ((atomic_read(&movement_atomic) == MOVER_DIRECTION_REVERSE)) {
         pid_effort = -1;
      } else {
         pid_effort = 1;
      }
      new_pid_effort = 500 + (pid_effort * new_speed / 2);
      max(min(new_pid_effort, 1000), 0);
    }
         
#ifdef SEND_PID
       SENDUSERCONNMSG( "step2,t,%i,e,%i,u,%i,r,%i,y,%i,o,%i,P,%i,I,%i,D,%i,l,%i", (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000), pid_error, new_pid_effort, new_speed, input_speed, input_speed_old, pid_p, pid_i, pid_d, atomic_read(&position));
#endif
    // convert effort to pwm
    new_speed = pwm_from_effort(new_pid_effort);

    // These change the pwm duty cycle
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB));

    // move effort values
    pid_last_effort = new_pid_effort;

    // unlock for next time
    spin_unlock(&pid_lock);
}
#endif

// new method
static void pid_step3() {
   int new_speed=1, input_speed=0, input_speed_old=0, pid_effort=0, pid_p=0, pid_i=0, pid_d=0;
   int direction, delta;
   // not used? -- using_ticks
   // not used? -- targetRatio;
   // not used? -- int a, b, c;
   struct timespec time_now = current_kernel_time();
   struct timespec time_d = timespec_sub(time_now, time_start);
   static int last_delta = -1; // invalid initial value for this static int
   //static int pid_count = 0;

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    //using_ticks = pid_ticks;
    //pid_ticks = 0;
    
    new_speed = atomic_read(&pid_set_point); // read desired pid set point
    input_speed_old = percent_from_speed(abs(current_speed10())); // ex on MIT : 0-20mph => 0-1000, so 5mph = 250, 2.5 mph = 125, etc.
    direction = atomic_read(&quad_direction);
    
    // delta is time since last pid_step, which used to be just delta_t, now it is time_d...units don't matter as we will adjust the D gain abitrarily anyway
    //delta = atomic_read(&delta_t); // delta was in clock ticks...
    delta = (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000l); // ...but is now in milliseconds
    if (last_delta != -1) { // check that we're valid
       int temp = delta;
       delta = delta - last_delta; // ... and is now the time since the last time we ran pid_step
       last_delta = temp; // remember
    } else {
       last_delta = delta; // remember
       // first time around we won't mess with delta
    }
    // old delta was speed dependent, new delta should always be about the same

    // read input speed (adjust speed10 value to 1000*percent)
    input_speed = percent_from_speed(abs(current_pid_speed10())); // ex on MIT : 0-20mph => 0-1000, so 5mph = 250, 2.5 mph = 125, etc.
    atomic_set(&pid_vel, 0);
    if (!spin_trylock(&pid_lock)) {
        return;
    }
    if (new_speed != 0) {
      //pid_effort = (kp_m * new_speed / 2;
      pid_effort = (((1000 * kp_m * new_speed) / kp_d) / 1000);
      if(isMoverAtDock() && pid_counter > 0){
          pid_effort *= 100;
          pid_counter --;
      }
      max(min(pid_effort, 1000), 0);
    }
         
#ifdef SEND_PID
       SENDUSERCONNMSG( "step3,t,%i,e,%i,u,%i,r,%i,y,%i,o,%i,P,%i,I,%i,D,%i,l,%i", (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000), 0, pid_effort, new_speed, input_speed, input_speed_old, pid_p, pid_i, pid_d, atomic_read(&position));
#endif
    // convert effort to pwm
    new_speed = pwm_from_effort(pid_effort);

    // These change the pwm duty cycle
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB));

    // move effort values
    pid_last_effort = pid_effort;

    // unlock for next time
    spin_unlock(&pid_lock);
}

// new method
static void pid_step4() {
   int new_speed=1, input_speed=0, input_speed_old=0, pid_effort=0, pid_p=0, pid_i=0, pid_d=0;
   int direction, delta;
   // not used? -- using_ticks
   // not used? -- targetRatio;
   // not used? -- int a, b, c;
   struct timespec time_now = current_kernel_time();
   struct timespec time_d = timespec_sub(time_now, time_start);
   static int last_delta = -1; // invalid initial value for this static int
   //static int pid_count = 0;

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    //using_ticks = pid_ticks;
    //pid_ticks = 0;
    
    new_speed = atomic_read(&pid_set_point); // read desired pid set point
    input_speed_old = percent_from_speed(abs(current_speed10())); // ex on MIT : 0-20mph => 0-1000, so 5mph = 250, 2.5 mph = 125, etc.
    direction = atomic_read(&quad_direction);
    
    // delta is time since last pid_step, which used to be just delta_t, now it is time_d...units don't matter as we will adjust the D gain abitrarily anyway
    //delta = atomic_read(&delta_t); // delta was in clock ticks...
    delta = (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000l); // ...but is now in milliseconds
    if (last_delta != -1) { // check that we're valid
       int temp = delta;
       delta = delta - last_delta; // ... and is now the time since the last time we ran pid_step
       last_delta = temp; // remember
    } else {
       last_delta = delta; // remember
       // first time around we won't mess with delta
    }
    // old delta was speed dependent, new delta should always be about the same

    // read input speed (adjust speed10 value to 1000*percent)
    input_speed = percent_from_speed(abs(current_pid_speed10())); // ex on MIT : 0-20mph => 0-1000, so 5mph = 250, 2.5 mph = 125, etc.
    atomic_set(&pid_vel, 0);
    if (!spin_trylock(&pid_lock)) {
        return;
    }
    if (new_speed != 0) {
      //pid_effort = (kp_m * new_speed / 2;
      pid_effort = (((1000 * kp_m * new_speed) / kp_d) / 1000);
      max(min(pid_effort, 1000), 0);
    }
         
#ifdef SEND_PID
       SENDUSERCONNMSG( "step3,t,%i,e,%i,u,%i,r,%i,y,%i,o,%i,P,%i,I,%i,D,%i,l,%i", (int)(time_d.tv_sec * 1000) + (int)(time_d.tv_nsec / 1000000), 0, pid_effort, new_speed, input_speed, input_speed_old, pid_p, pid_i, pid_d, atomic_read(&position));
#endif
    // convert effort to pwm
    new_speed = pwm_from_effort_4(pid_effort);

    // These change the pwm duty cycle
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RA));
    __raw_writel(max(new_speed,1), tc->regs + ATMEL_TC_REG(MOTOR_PWM_CHANNEL[mover_type], RB));

    // move effort values
    pid_last_effort = pid_effort;

    // unlock for next time
    spin_unlock(&pid_lock);
}


//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_mover_generic_init(void)
    {
    // not used? -- struct heartbeat_object hb_obj;
    // not used? -- int d_id;
    // not used? -- struct nl_driver driver = {&hb_obj, NULL, 0, NULL}; // only heartbeat object
    int retval;

    // for debug
    time_start = current_kernel_time();
    switch (mover_type) {
        case 0: case 1: atomic_set(&tc_clock, 2); break;
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

    // setup heartbeat for polling
//    hb_obj_init_nt(&hb_obj, pid_step, PID_HZ[mover_type]); // heartbeat object calling pid_step()
//    d_id = install_nl_driver(&driver);
//    atomic_set(&driver_id, d_id);

// new method
    // initialize pid stuff
    reset_pid(); // reset pid variables

    // initialize sysfs structure
    target_device_mover_generic.name = TARGET_NAME[mover_type]; // set name in structure here as we can't initialize on a non-constant
    retval = target_sysfs_add(&target_device_mover_generic);

    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
    pid_timeout_start();
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
    uninstall_nl_driver(atomic_read(&driver_id));
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

