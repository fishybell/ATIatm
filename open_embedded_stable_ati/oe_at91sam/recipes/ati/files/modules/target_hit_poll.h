#ifndef __TARGET_HIT_POLL_H__
#define __TARGET_HIT_POLL_H__

#include "target_hardware.h"

// get/set hit calibration info
extern void set_hit_calibration(int lower, int upper); // set lower and upper hit calibration values
extern void get_hit_calibration(int *lower, int *upper); // get lower and upper hit calibration values

// register a callback for the hit event
typedef void (*hit_event_callback)(void); // called at each hit received by the sensor
extern void set_hit_callback(hit_event_callback handler);


#endif // __TARGET_HIT_POLL_H__
