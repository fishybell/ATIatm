#ifndef __TARGET_MOVER_GENERIC_H__
#define __TARGET_MOVER_GENERIC_H__

#include "target_hardware.h"

// get current speed (positive/negative for direction)
extern int mover_speed_get(void); // in whole mph

// set current speed (positive/negative for direction)
extern int mover_speed_set(int speed); // in whole mph

// set current speed (positive/negative for direction)
extern int mover_set_move_speed(int speed); // in whole mph

// set current speed (positive/negative for direction)
extern int mover_set_continuous_move(int speed); // in whole mph

// immediate stop (set to 0 will coast to a stop)
extern int mover_speed_stop(void);

// go to where the home position has been set
extern void mover_go_home(void);

// get current position (from home in feet)
extern int mover_position_get(void);

// get the device sleep state
extern int mover_sleep_get(void);

// set the device to asleep or awake
extern int mover_sleep_set(int);

// set the lifter position
extern void set_lifter_position(int);

// set the lifter fault
extern void set_lifter_fault(int);

// register a callback for the lift event
typedef void (*move_event_callback)(int); // called on finished, starting, and error (passing an EVENT_### value)
extern void set_move_callback(move_event_callback handler, move_event_callback faulthandler);

#endif // __TARGET_MOVER_GENERIC_H__
