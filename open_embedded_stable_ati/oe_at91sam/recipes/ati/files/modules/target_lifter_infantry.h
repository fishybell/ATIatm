#ifndef __TARGET_LIFTER_INFANTRY_H__
#define __TARGET_LIFTER_INFANTRY_H__

#include "target_hardware.h"

#define LIFTER_POSITION_DOWN   			0
#define LIFTER_POSITION_UP    			1
#define LIFTER_POSITION_MOVING  		2
#define LIFTER_POSITION_ERROR_NEITHER	3	// Neither limit switch is active, but the lifter is not moving

// get current position
extern int lifter_position_get(void);

// set current position
extern int lifter_position_set(int position);

// get the device sleep state
extern int lifter_sleep_get(void);

// set the device to asleep or awake
extern int lifter_sleep_set(int);

// register a callback for the lift event
typedef void (*lift_event_callback)(int); // called on finished, starting, and error (passing an EVENT_### value)
extern void set_lift_callback(lift_event_callback handler);

#endif // __TARGET_LIFTER_INFANTRY_H__
