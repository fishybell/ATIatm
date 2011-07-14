#ifndef __TARGET_BATTERY_H__
#define __TARGET_BATTERY_H__

#include "target_hardware.h"

// enables/disables battery sensing (so we don't sense while running a moter)
extern void enable_battery_check(int enable); // 1 = on, 0 = off

#endif // __TARGET_BATTERY_H__
