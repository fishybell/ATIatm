#ifndef __DEFAULTS_H__
#define __DEFAULTS_H

// Hit calibration defaults
#define HIT_MSECS_BETWEEN 100
#define HIT_MSECS_BETWEEN_LOC 0x100
#define HIT_MSECS_BETWEEN_SIZE 0x08

#define HIT_DESENSITIVITY 4
#define HIT_DESENSITIVITY_LOC 0x108
#define HIT_DESENSITIVITY_SIZE 0x08

#define HIT_START_BLANKING 7
#define HIT_START_BLANKING_LOC 0x110
#define HIT_START_BLANKING_SIZE 0x08

#define HIT_ENABLE_ON 4
#define HIT_ENABLE_ON_LOC 0x118
#define HIT_ENABLE_ON_SIZE 0x08

// Hit sensor defaults
#define HIT_SENSOR_TYPE 0
#define HIT_SENSOR_TYPE_LOC 0x120
#define HIT_SENSOR_TYPE_SIZE 0x08

#define HIT_SENSOR_INVERT 0
#define HIT_SENSOR_INVERT_LOC 0x128
#define HIT_SENSOR_INVERT_SIZE 0x08

// Fall Reaction defaults
#define FALL_KILL_AT_X_HITS 1
#define FALL_KILL_AT_X_HITS_LOC 0x2C0
#define FALL_KILL_AT_X_HITS_SIZE 0x10

#define FALL_AT_FALL 3
#define FALL_AT_FALL_LOC 0x2D0
#define FALL_AT_FALL_SIZE 0x10

// Bob type defaults
#define BOB_TYPE 1
#define BOB_TYPE_LOC 0X2E0
#define BOB_TYPE_SIZE 0X10

// Serial Number defaults
#define SERIAL_NUMBER "00000-0-0"
#define SERIAL_NUMBER_LOC 0x280
#define SERIAL_NUMBER_SIZE 0x20

#define SERIAL_SIZE 20

// Address Defaults
#define ADDRESS_LOC 0x2A0
#define ADDRESS_SIZE 0x20

#define ADD_SIZE 20

// Battery defaults
#define SIT_BATTERY 12
#define SIT_BATTERY_LOC 0x400
#define SIT_BATTERY_SIZE 0x08

#define SAT_BATTERY 12
#define SAT_BATTERY_LOC 0x408
#define SAT_BATTERY_SIZE 0x08

#define SES_BATTERY 12
#define SES_BATTERY_LOC 0x410
#define SES_BATTERY_SIZE 0x08

#define MIT_BATTERY 24
#define MIT_BATTERY_LOC 0x418
#define MIT_BATTERY_SIZE 0x08

#define MAT_BATTERY 48
#define MAT_BATTERY_LOC 0x420
#define MAT_BATTERY_SIZE 0x08

#define MIT_MOVER_TYPE 0
#define MIT_MOVER_TYPE_LOC 0x428
#define MIT_MOVER_TYPE_SIZE 0x04

#define MIT_REVERSE 0
#define MIT_REVERSE_LOC 0x42C
#define MIT_REVERSE_SIZE 0x04

#define MAT_MOVER_TYPE 1
#define MAT_MOVER_TYPE_LOC 0x430
#define MAT_MOVER_TYPE_SIZE 0x04

#define MAT_REVERSE 0
#define MAT_REVERSE_LOC 0x434
#define MAT_REVERSE_SIZE 0x04

#define BATTERY_SIZE 8
#define MOVER_SIZE 4

// SES defaults
#define SES_LOOP 1
#define SES_LOOP_LOC 0x440
#define SES_LOOP_SIZE 0x10

#define SES_MODE 0
#define SES_MODE_LOC 0x450
#define SES_MODE_SIZE 0x10

#define SES_SIZE 10

// MFS defaults (Muzzle Flash Simulator)
#define MFS_EXISTS 1
#define MFS_EXISTS_LOC 0x180
#define MFS_EXISTS_SIZE 0x04

#define MFS_ACTIVATE 0
#define MFS_ACTIVATE_LOC 0x184
#define MFS_ACTIVATE_SIZE 0x04

#define MFS_ACTIVATE_EXPOSE 0
#define MFS_ACTIVATE_EXPOSE_LOC 0x188
#define MFS_ACTIVATE_EXPOSE_SIZE 0x04

#define MFS_ACTIVATE_ON_HIT 0
#define MFS_ACTIVATE_ON_HIT_LOC 0x18C
#define MFS_ACTIVATE_ON_HIT_SIZE 0x04

#define MFS_ACTIVATE_ON_KILL 0
#define MFS_ACTIVATE_ON_KILL_LOC 0x190
#define MFS_ACTIVATE_ON_KILL_SIZE 0x04

#define MFS_MS_ON_TIME 0
#define MFS_MS_ON_TIME_LOC 0x194
#define MFS_MS_ON_TIME_SIZE 0x04

#define MFS_MS_OFF_TIME 0
#define MFS_MS_OFF_TIME_LOC 0x198
#define MFS_MS_OFF_TIME_SIZE 0x04

#define MFS_START_DELAY 0
#define MFS_START_DELAY_LOC 0x19C
#define MFS_START_DELAY_SIZE 0x04

#define MFS_REPEAT_DELAY 0
#define MFS_REPEAT_DELAY_LOC 0x1A0
#define MFS_REPEAT_DELAY_SIZE 0x04

#define MFS_REPEAT_COUNT 0
#define MFS_REPEAT_COUNT_LOC 0x1A4
#define MFS_REPEAT_COUNT_SIZE 0x04

#define MFS_EX1 0
#define MFS_EX1_LOC 0x1A8
#define MFS_EX1_SIZE 0x04	

#define MFS_EX2 0
#define MFS_EX2_LOC 0x1AC
#define MFS_EX2_SIZE 0x04

#define MFS_EX3 0
#define MFS_EX3_LOC 0x1B0
#define MFS_EX3_SIZE 0x04

#define MFS_MODE 0
#define MFS_MODE_LOC 0x1B4
#define MFS_MODE_SIZE 0x04

#define MFS_SIZE 4

// MGL defaults (Moon Glow Simulator)
#define MGL_EXISTS 1
#define MGL_EXISTS_LOC 0x340
#define MGL_EXISTS_SIZE 0x04

#define MGL_ACTIVATE 0
#define MGL_ACTIVATE_LOC 0x344
#define MGL_ACTIVATE_SIZE 0x04

#define MGL_ACTIVATE_EXPOSE 0
#define MGL_ACTIVATE_EXPOSE_LOC 0x348
#define MGL_ACTIVATE_EXPOSE_SIZE 0x04

#define MGL_ACTIVATE_ON_HIT 0
#define MGL_ACTIVATE_ON_HIT_LOC 0x34C
#define MGL_ACTIVATE_ON_HIT_SIZE 0x04

#define MGL_ACTIVATE_ON_KILL 0
#define MGL_ACTIVATE_ON_KILL_LOC 0x350
#define MGL_ACTIVATE_ON_KILL_SIZE 0x04

#define MGL_MS_ON_TIME 0
#define MGL_MS_ON_TIME_LOC 0x354
#define MGL_MS_ON_TIME_SIZE 0x04

#define MGL_MS_OFF_TIME 0
#define MGL_MS_OFF_TIME_LOC 0x358
#define MGL_MS_OFF_TIME_SIZE 0x04

#define MGL_START_DELAY 0
#define MGL_START_DELAY_LOC 0x35C
#define MGL_START_DELAY_SIZE 0x04

#define MGL_REPEAT_DELAY 0
#define MGL_REPEAT_DELAY_LOC 0x360
#define MGL_REPEAT_DELAY_SIZE 0x04

#define MGL_REPEAT_COUNT 0
#define MGL_REPEAT_COUNT_LOC 0x364
#define MGL_REPEAT_COUNT_SIZE 0x04

#define MGL_EX1 0
#define MGL_EX1_LOC 0x368
#define MGL_EX1_SIZE 0x04	

#define MGL_EX2 0
#define MGL_EX2_LOC 0x36C
#define MGL_EX2_SIZE 0x04

#define MGL_EX3 0
#define MGL_EX3_LOC 0x370
#define MGL_EX3_SIZE 0x04

#define MGL_SIZE 4

// PHI defaults (Positive Hit Indicator)
#define PHI_EXISTS 1
#define PHI_EXISTS_LOC 0x380
#define PHI_EXISTS_SIZE 0x04

#define PHI_ACTIVATE 0
#define PHI_ACTIVATE_LOC 0x384
#define PHI_ACTIVATE_SIZE 0x04

#define PHI_ACTIVATE_EXPOSE 0
#define PHI_ACTIVATE_EXPOSE_LOC 0x388
#define PHI_ACTIVATE_EXPOSE_SIZE 0x04

#define PHI_ACTIVATE_ON_HIT 0
#define PHI_ACTIVATE_ON_HIT_LOC 0x38C
#define PHI_ACTIVATE_ON_HIT_SIZE 0x04

#define PHI_ACTIVATE_ON_KILL 0
#define PHI_ACTIVATE_ON_KILL_LOC 0x390
#define PHI_ACTIVATE_ON_KILL_SIZE 0x04

#define PHI_MS_ON_TIME 0
#define PHI_MS_ON_TIME_LOC 0x394
#define PHI_MS_ON_TIME_SIZE 0x04

#define PHI_MS_OFF_TIME 0
#define PHI_MS_OFF_TIME_LOC 0x398
#define PHI_MS_OFF_TIME_SIZE 0x04

#define PHI_START_DELAY 0
#define PHI_START_DELAY_LOC 0x39C
#define PHI_START_DELAY_SIZE 0x04

#define PHI_REPEAT_DELAY 0
#define PHI_REPEAT_DELAY_LOC 0x3A0
#define PHI_REPEAT_DELAY_SIZE 0x04

#define PHI_REPEAT_COUNT 0
#define PHI_REPEAT_COUNT_LOC 0x3A4
#define PHI_REPEAT_COUNT_SIZE 0x04

#define PHI_EX1 0
#define PHI_EX1_LOC 0x3A8
#define PHI_EX1_SIZE 0x04	

#define PHI_EX2 0
#define PHI_EX2_LOC 0x3AC
#define PHI_EX2_SIZE 0x04

#define PHI_EX3 0
#define PHI_EX3_LOC 0x3B0
#define PHI_EX3_SIZE 0x04

#define PHI_SIZE 4

// SMK defaults (Smoke)
#define SMK_EXISTS 0
#define SMK_EXISTS_LOC 0x300
#define SMK_EXISTS_SIZE 0x04

#define SMK_ACTIVATE 0
#define SMK_ACTIVATE_LOC 0x304
#define SMK_ACTIVATE_SIZE 0x04

#define SMK_ACTIVATE_EXPOSE 0
#define SMK_ACTIVATE_EXPOSE_LOC 0x308
#define SMK_ACTIVATE_EXPOSE_SIZE 0x04

#define SMK_ACTIVATE_ON_HIT 0
#define SMK_ACTIVATE_ON_HIT_LOC 0x30C
#define SMK_ACTIVATE_ON_HIT_SIZE 0x04

#define SMK_ACTIVATE_ON_KILL 0
#define SMK_ACTIVATE_ON_KILL_LOC 0x310
#define SMK_ACTIVATE_ON_KILL_SIZE 0x04

#define SMK_MS_ON_TIME 0
#define SMK_MS_ON_TIME_LOC 0x314
#define SMK_MS_ON_TIME_SIZE 0x04

#define SMK_MS_OFF_TIME 0
#define SMK_MS_OFF_TIME_LOC 0x318
#define SMK_MS_OFF_TIME_SIZE 0x04

#define SMK_START_DELAY 0
#define SMK_START_DELAY_LOC 0x31C
#define SMK_START_DELAY_SIZE 0x04

#define SMK_REPEAT_DELAY 0
#define SMK_REPEAT_DELAY_LOC 0x320
#define SMK_REPEAT_DELAY_SIZE 0x04

#define SMK_REPEAT_COUNT 0
#define SMK_REPEAT_COUNT_LOC 0x324
#define SMK_REPEAT_COUNT_SIZE 0x04

#define SMK_EX1 0
#define SMK_EX1_LOC 0x328
#define SMK_EX1_SIZE 0x04	

#define SMK_EX2 0
#define SMK_EX2_LOC 0x32C
#define SMK_EX2_SIZE 0x04

#define SMK_EX3 0
#define SMK_EX3_LOC 0x330
#define SMK_EX3_SIZE 0x04

#define SMK_SIZE 4

// THM defaults (Thermals)
#define THM_EXISTS 0
#define THM_EXISTS_LOC 0x3C0
#define THM_EXISTS_SIZE 0x04

#define THM_ACTIVATE 0
#define THM_ACTIVATE_LOC 0x3C4
#define THM_ACTIVATE_SIZE 0x04

#define THM_ACTIVATE_EXPOSE 0
#define THM_ACTIVATE_EXPOSE_LOC 0x3C8
#define THM_ACTIVATE_EXPOSE_SIZE 0x04

#define THM_ACTIVATE_ON_HIT 0
#define THM_ACTIVATE_ON_HIT_LOC 0x3CC
#define THM_ACTIVATE_ON_HIT_SIZE 0x04

#define THM_ACTIVATE_ON_KILL 0
#define THM_ACTIVATE_ON_KILL_LOC 0x3D0
#define THM_ACTIVATE_ON_KILL_SIZE 0x04

#define THM_MS_ON_TIME 0
#define THM_MS_ON_TIME_LOC 0x3D4
#define THM_MS_ON_TIME_SIZE 0x04

#define THM_MS_OFF_TIME 0
#define THM_MS_OFF_TIME_LOC 0x3D8
#define THM_MS_OFF_TIME_SIZE 0x04

#define THM_START_DELAY 0
#define THM_START_DELAY_LOC 0x3DC
#define THM_START_DELAY_SIZE 0x04

#define THM_REPEAT_DELAY 0
#define THM_REPEAT_DELAY_LOC 0x3E0
#define THM_REPEAT_DELAY_SIZE 0x04

#define THM_REPEAT_COUNT 0
#define THM_REPEAT_COUNT_LOC 0x3E4
#define THM_REPEAT_COUNT_SIZE 0x04

#define THM_EX1 0
#define THM_EX1_LOC 0x3E8
#define THM_EX1_SIZE 0x04	

#define THM_EX2 0
#define THM_EX2_LOC 0x3EC
#define THM_EX2_SIZE 0x04

#define THM_EX3 0
#define THM_EX3_LOC 0x3F0
#define THM_EX3_SIZE 0x04

#define THM_SIZE 4


// ,.. defaults


#endif
