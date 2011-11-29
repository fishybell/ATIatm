#ifndef __TARGET_GENERIC_OUTPUT_H__
#define __TARGET_GENERIC_OUTPUT_H__

#include "target_hardware.h"

// supported generic output types defined in netlink_shared.h (ACC_*)

// tells wether generic output of the given type and number exists
extern int generic_output_exists(int type); // returns the number that exist

// get/set routiens for disabling/enabling 
typedef enum {
    ENABLED,
    DISABLED,
} GO_enable_t;
extern GO_enable_t generic_output_get_enable(int type, int num);
extern void generic_output_set_enable(int type, int num, GO_enable_t value);

// get/set routines for manual on/off (overrides mode)
typedef enum {
    ON_IMMEDIATE,	// on now, only ignores initial delay value
    OFF_IMMEDIATE,	// off immediate (stops current burst, repeat, etc.)
    ON_SOON,		// manual on (uses initial delay)
    OFF_SOON,		// manual off (waits for current burst, repeat, etc., cancels infinite repeat)
} GO_state_t;
extern GO_state_t generic_output_get_state(int type, int num);
extern void generic_output_set_state(int type, int num, GO_state_t value);

// get/set routines for active-on mode
#define MANUAL_ACTIVE		(1<<0)	// activate/deactivate only on manual action
#define ACTIVE_RAISE			(1<<1)	// activate on start of raise (for lifting targets)
#define UNACTIVE_RAISE		(1<<2)	// deactivate on start of raise (for lifting targets)
#define ACTIVE_UP				(1<<3)	// activate on end of raise (for lifting targets)
#define UNACTIVE_UP			(1<<4)	// deactivate on end of raise (for lifting targets)
#define ACTIVE_LOWER			(1<<5)	// activate on start of lower (for lifting targets)
#define UNACTIVE_LOWER		(1<<6)	// deactivate on start of lower (for lifting targets)
#define ACTIVE_DOWN			(1<<7)	// activate on end of lower (for lifting targets)
#define UNACTIVE_DOWN		(1<<8)	// deactivate on end of lower (for lifting targets)
#define ACTIVE_MOVE			(1<<9)	// activate on start of moving (for moving targets)
#define UNACTIVE_MOVE		(1<<10)	// deactivate on start of moving (for moving targets)
#define ACTIVE_MOVING		(1<<11)	// activate on moving at speed (for moving targets)
#define UNACTIVE_MOVING		(1<<12)	// deactivate on moving at speed (for moving targets)
#define ACTIVE_COAST			(1<<13)	// activate on start of coasting (for moving targets)
#define UNACTIVE_COAST		(1<<14)	// deactivate on start of coasting (for moving targets)
#define ACTIVE_STOP			(1<<15)	// activate on start of stopping (for moving targets)
#define UNACTIVE_STOP		(1<<16)	// deactivate on start of stopping (for moving targets)
#define ACTIVE_STOPPED		(1<<17)	// activate on full stop (for moving targets)
#define UNACTIVE_STOPPED	(1<<18)	// deactivate on full stop (for moving targets)
#define ACTIVE_HIT			(1<<19)	// activate on hit (arbirtrary hit event)
#define UNACTIVE_HIT	   	(1<<20)	// deactivate on hit (arbirtrary hit event)
#define ACTIVE_KILL			(1<<21)	// activate on kill (arbitrary kill event)
#define UNACTIVE_KILL		(1<<22)	// deactivate on kill (arbitrary kill event)
#define ACTIVE_ERROR			(1<<23)	// activate on error (deactivates on everything else)
extern int generic_output_get_active_on(int type, int num); // value ORed from above
extern void generic_output_set_active_on(int type, int num, int value); // value ORed from above

// get/set routines for firing mode
typedef enum {
    CONSTANT_ON,	// goes on, stays on (can not repeat)
    BURST_FIRE,		// intermittent on/off multiple times (can repeat)
    TEMP_ON,		// intermittent on (can repeat)
} GO_mode_t;
extern GO_mode_t generic_output_get_mode(int type, int num);
extern void generic_output_set_mode(int type, int num, GO_mode_t value);

// get/set routines for inital delay (used in all modes)
extern int generic_output_get_initial_delay(int type, int num); // time before initial firing
extern void generic_output_set_initial_delay(int type, int num, int msecs);
extern int generic_output_get_initial_delay_random(int type, int num); // random time before initial firing
extern void generic_output_set_initial_delay_random(int type, int num, int msecs);

// get/set routines for repeat firings (only used when the mode uses repeats)
extern int generic_output_get_repeat_delay(int type, int num); // time between repeats
extern void generic_output_set_repeat_delay(int type, int num, int msecs);
extern int generic_output_get_repeat_delay_random(int type, int num); // random time between repeats
extern void generic_output_set_repeat_delay_random(int type, int num, int msecs);

extern int generic_output_get_repeat_count(int type, int num); // repeat X times (-1 for infinity, 0 for never)
extern void generic_output_set_repeat_count(int type, int num, int count);

// get/set routines for intermittent firing (only used when the mode uses intermittents)
extern int generic_output_get_on_time(int type, int num); // active time
extern void generic_output_set_on_time(int type, int num, int msecs);

extern int generic_output_get_off_time(int type, int num); // inactive time
extern void generic_output_set_off_time(int type, int num, int msecs);

extern int generic_output_get_onoff_repeat(int type, int num); // repeat on/off cycle X times (-1 for infinity, 0 for never)
extern void generic_output_set_onoff_repeat(int type, int num, int count);

// an event has occurred that the various accessories may need to activate on
typedef enum {
    EVENT_RAISE,	// start of raise
    EVENT_UP,		// finished raising
    EVENT_LOWER,	// start of lower
    EVENT_DOWN,		// finished lowering
    EVENT_MOVE,		// start of move
    EVENT_MOVING,	// reached target speed
    EVENT_POSITION,	// changed position
    EVENT_COAST,	// started coast
    EVENT_STOP,		// started stopping
    EVENT_STOPPED,	// finished stopping
    EVENT_HIT,		// hit
    EVENT_KILL,		// kill
    EVENT_SHUTDOWN,	// shutdown
    EVENT_SLEEP,	// sleep
    EVENT_WAKE,		// wake
    EVENT_HOME_LIMIT,		// triggered on home limit switch
    EVENT_END_LIMIT,		// triggered on end limit switch
    EVENT_TIMED_OUT,		// triggered on end limit switch
    EVENT_ERROR,	// error with one of the above (always causes immediate deactivate)
} GO_event_t;
extern void generic_output_event(GO_event_t type); // can be called from interrupt

#endif // __TARGET_GENERIC_OUTPUT_H__
