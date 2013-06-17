#ifndef __TARGET_MOVER_HIT_POLL_H__
#define __TARGET_MOVER_HIT_POLL_H__

#include "target_hardware.h"

// get/set hit calibration info
extern void set_mover_hit_calibration(int line, int seperation, int sensitivity);
extern void get_mover_hit_calibration(int line, int *seperation, int *sensitivity);

// turn on/off hit sensor blanking
extern void hit_mover_blanking_on(int line);
extern void hit_mover_blanking_off(int line);

// turn on/off hit sensor line inverting
extern void set_mover_hit_invert(int line, int invert);
extern int get_mover_hit_invert(int line);

// register a callback for the hit event
typedef void (*hit_mover_event_callback)(int, int); // called at each hit received by the sensor (passing which sensor line it was received on)

extern void set_mover_hit_callback(int line, hit_mover_event_callback handler, hit_mover_event_callback discon);


#endif // __TARGET_MOVER_HIT_POLL_H__
