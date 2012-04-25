#ifndef __TARGET_BATTERY_H__
#define __TARGET_BATTERY_H__

#include "target_hardware.h"

// enables/disables battery sensing (so we don't sense while running a moter)
extern void enable_battery_check(int enable); // 1 = on, 0 = off
extern void battery_check_is_docked(int dockval); // 1 = docked, 0 = not docked

#endif // __TARGET_BATTERY_H__
