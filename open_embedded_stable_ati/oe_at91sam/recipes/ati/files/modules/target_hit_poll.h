#ifndef __TARGET_HIT_POLL_H__
#define __TARGET_HIT_POLL_H__

#include "target_hardware.h"

// get/set hit calibration info
extern void set_hit_calibration(int seperation, int sensitivity);
extern void get_hit_calibration(int *seperation, int *sensitivity);

// turn on/off hit sensor blanking
extern void hit_blanking_on(void);
extern void hit_blanking_off(void);

// turn on/off hit sensor line inverting
extern void set_hit_invert(int invert);
extern int get_hit_invert(void);

// register a callback for the hit event
typedef void (*hit_event_callback)(int); // called at each hit received by the sensor (passing which sensor line it was received on)
extern void set_hit_callback(hit_event_callback handler, hit_event_callback discon, hit_event_callback magni);


#endif // __TARGET_HIT_POLL_H__
