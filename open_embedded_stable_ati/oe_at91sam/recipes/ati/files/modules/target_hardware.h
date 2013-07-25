// target_hardware.h

#ifndef __TARGET_HARDWARE_H__
#define __TARGET_HARDWARE_H__

#include "delay_printk.h"

//---------------------------------------------------------------------------
// LOGICAL ACTIVE STATES
//---------------------------------------------------------------------------
#define ACTIVE_LOW						0
#define ACTIVE_HIGH						1

//---------------------------------------------------------------------------
// PULL-UP RESISTOR SETTING
//---------------------------------------------------------------------------
#define PULLUP_OFF						0
#define PULLUP_ON						1

//---------------------------------------------------------------------------
// INPUT DEGLITCH SETTING
//---------------------------------------------------------------------------
#define DEGLITCH_OFF					0
#define DEGLITCH_ON						1

//---------------------------------------------------------------------------
// LIFTER
//---------------------------------------------------------------------------
#define INPUT_LIFTER_POS_ACTIVE_STATE		ACTIVE_HIGH
#define INPUT_LIFTER_POS_PULLUP_STATE		PULLUP_ON
#define INPUT_LIFTER_POS_DEGLITCH_STATE		DEGLITCH_ON
#define	INPUT_LIFTER_POS_UP_LIMIT 			AT91_PIN_PC5
#define	INPUT_LIFTER_POS_DOWN_LIMIT 		AT91_PIN_PC4

#define OUTPUT_LIFTER_MOTOR_POS_ACTIVE_STATE	ACTIVE_LOW
#define	OUTPUT_LIFTER_MOTOR_FWD_POS 			AT91_PIN_PB3
#define	OUTPUT_LIFTER_MOTOR_REV_POS 			AT91_PIN_PB19

#define OUTPUT_LIFTER_MOTOR_NEG_ACTIVE_STATE	ACTIVE_HIGH
#define	OUTPUT_LIFTER_MOTOR_REV_NEG 			AT91_PIN_PB13
#define	OUTPUT_LIFTER_MOTOR_FWD_NEG 			AT91_PIN_PB12

//---------------------------------------------------------------------------
// BATTERY
//---------------------------------------------------------------------------
#define	INPUT_ADC_LOW_BAT 					AT91_PIN_PC1

#define OUTPUT_LED_LOW_BAT_ACTIVE_STATE		ACTIVE_LOW
#define	OUTPUT_LED_LOW_BAT 					AT91_PIN_PC15

#define INPUT_CHARGING_BAT_ACTIVE_STATE			ACTIVE_LOW
#define INPUT_CHARGING_BAT_PULLUP_STATE			PULLUP_ON
#define INPUT_CHARGING_BAT_DEGLITCH_STATE		DEGLITCH_ON
#define	INPUT_CHARGING_BAT						AT91_PIN_PB8  // DOCKED

#define	OUTPUT_CHARGING_RELAY 			AT91_PIN_PA4	// Mover board
#define	OUTPUT_CHARGING_RELAY_ACTIVE_STATE 		ACTIVE_HIGH
#define	OUTPUT_CHARGING_RELAY_INACTIVE_STATE 	ACTIVE_LOW

#define USB_ENABLE_ACTIVE_STATE				ACTIVE_LOW
#define USB_ENABLE_PULLUP_STATE				PULLUP_OFF
#define USB_ENABLE					AT91_PIN_PA22

//---------------------------------------------------------------------------
// SES USER INTERFACE
//---------------------------------------------------------------------------
#define	INPUT_SES_MODE_BUTTON_ACTIVE_STATE			ACTIVE_LOW
#define INPUT_SES_MODE_BUTTON_PULLUP_STATE			PULLUP_ON
#define INPUT_SES_MODE_BUTTON_DEGLITCH_STATE			DEGLITCH_ON
#define	INPUT_SES_MODE_BUTTON 					AT91_PIN_PB0

#define	OUTPUT_SES_MODE_INDICATOR_ACTIVE_STATE			ACTIVE_LOW
#define	OUTPUT_SES_MODE_MAINT_INDICATOR 			AT91_PIN_PA5
#define	OUTPUT_SES_MODE_TESTING_INDICATOR 			AT91_PIN_PA29
#define	OUTPUT_SES_MODE_RECORD_INDICATOR 			AT91_PIN_PA25
#define	OUTPUT_SES_MODE_LIVEFIRE_INDICATOR 			AT91_PIN_PA4
#define	OUTPUT_SES_MODE_LIVEFIRE_INDICATOR_BIG		AT91_PIN_PB22
#define	OUTPUT_SES_AMPLIFIER_ON			 			AT91_PIN_PB30
#define	OUTPUT_SES_AMPLIFIER_ACTIVE_STATE		 	ACTIVE_LOW

#define	INPUT_SELECTOR_KNOB_ACTIVE_STATE			ACTIVE_LOW
#define INPUT_SELECTOR_KNOB_PULLUP_STATE			PULLUP_ON
#define INPUT_SELECTOR_KNOB_DEGLITCH_STATE			DEGLITCH_ON
#define	INPUT_SELECTOR_KNOB_PIN_1				AT91_PIN_PB10
#define	INPUT_SELECTOR_KNOB_PIN_2				AT91_PIN_PB11
#define	INPUT_SELECTOR_KNOB_PIN_4				AT91_PIN_PA30
#define	INPUT_SELECTOR_KNOB_PIN_8				AT91_PIN_PA31

//---------------------------------------------------------------------------
// USER INTERFACE
//---------------------------------------------------------------------------
#define	INPUT_TEST_BUTTON_ACTIVE_STATE		ACTIVE_LOW
#define INPUT_TEST_BUTTON_PULLUP_STATE		PULLUP_ON
#define INPUT_TEST_BUTTON_DEGLITCH_STATE	DEGLITCH_ON
#define	INPUT_TEST_BUTTON 					AT91_PIN_PC12

#define	INPUT_MOVER_TEST_BUTTON_ACTIVE_STATE		ACTIVE_LOW
#define INPUT_MOVER_TEST_BUTTON_PULLUP_STATE		PULLUP_ON
#define INPUT_MOVER_TEST_BUTTON_DEGLITCH_STATE		DEGLITCH_ON
#define	INPUT_MOVER_TEST_BUTTON_FWD 			AT91_PIN_PC3
#define	INPUT_MOVER_TEST_BUTTON_REV 			AT91_PIN_PC6

#define	OUTPUT_TEST_INDICATOR_ACTIVE_STATE	ACTIVE_LOW
#define	OUTPUT_TEST_INDICATOR 				AT91_PIN_PC8

//---------------------------------------------------------------------------
// HIT SENSOR - MECHANICAL
//---------------------------------------------------------------------------
#define INPUT_HIT_SENSOR_ACTIVE_STATE		ACTIVE_HIGH
#define INPUT_HIT_SENSOR_PULLUP_STATE		PULLUP_ON
#define INPUT_HIT_SENSOR_DEGLITCH_STATE		DEGLITCH_ON
#define	INPUT_HIT_SENSOR 					AT91_PIN_PB24
#define	INPUT_FRONT_TIRE_HIT 				AT91_PIN_PB21 
#define	INPUT_BACK_TIRE_HIT 				AT91_PIN_PB20
#define	INPUT_ENGINE_HIT 					AT91_PIN_PB25

//---------------------------------------------------------------------------
// HIT SENSOR - MILES RECEIVER
//---------------------------------------------------------------------------
#define INPUT_MILES_ACTIVE_STATE			ACTIVE_HIGH
#define INPUT_MILES_PULLUP_STATE			PULLUP_ON
#define INPUT_MILES_DEGLITCH_STATE			DEGLITCH_ON
#define	INPUT_MILES_HIT 					AT91_PIN_PB23

#define OUTPUT_MILES_RESET_ACTIVE_STATE		ACTIVE_LOW
#define	OUTPUT_MILES_RESET 					AT91_PIN_PB22

//---------------------------------------------------------------------------
// MILE TRANSMITTER
//---------------------------------------------------------------------------
#define OUTPUT_MILES_SHOOTBACK_ACTIVE_STATE		ACTIVE_LOW
#define	OUTPUT_MILES_SHOOTBACK 					AT91_PIN_PC2 
// was PC10

//---------------------------------------------------------------------------
// MOVER
//---------------------------------------------------------------------------
#define OUTPUT_MOVER_HORN_ACTIVE_STATE			ACTIVE_LOW
#define	OUTPUT_MOVER_HORN 				AT91_PIN_PB30

#define INPUT_MOVER_SPEED_SENSOR_ACTIVE_STATE		ACTIVE_LOW
#define INPUT_MOVER_SPEED_SENSOR_PULLUP_STATE		PULLUP_ON
#define	INPUT_MOVER_SPEED_SENSOR_1 					AT91_PIN_PB0
#define	INPUT_MOVER_SPEED_SENSOR_2 					AT91_PIN_PB1

#define INPUT_MOVER_TRACK_SENSOR_ACTIVE_STATE		ACTIVE_LOW
#define INPUT_MOVER_TRACK_SENSOR_PULLUP_STATE		PULLUP_ON
#define INPUT_MOVER_TRACK_SENSOR_DEGLITCH_STATE			DEGLITCH_ON
#define	INPUT_MOVER_TRACK_SENSOR_1 					AT91_PIN_PB10
#define	INPUT_MOVER_TRACK_SENSOR_2 					AT91_PIN_PB11

// External Motor Control
#define OUTPUT_MOVER_APPLY_BRAKE_ACTIVE_STATE		ACTIVE_LOW
#define	OUTPUT_MOVER_APPLY_BRAKE 					AT91_PIN_PB9

// External Motor Control
#define INPUT_MOVER_END_OF_TRACK_ACTIVE_STATE		ACTIVE_HIGH
#define INPUT_MOVER_END_OF_TRACK_PULLUP_STATE		PULLUP_ON
#define	INPUT_MOVER_END_OF_TRACK_1 					AT91_PIN_PA30
#define	INPUT_MOVER_END_OF_TRACK_2 					AT91_PIN_PA31

// External Motor Control
#define OUTPUT_MOVER_DIRECTION_ACTIVE_STATE			ACTIVE_LOW
#define	OUTPUT_MOVER_DIRECTION_FORWARD 				AT91_PIN_PA29
#define	OUTPUT_MOVER_DIRECTION_REVERSE 				AT91_PIN_PA25

// External Motor Control
#define	OUTPUT_MOVER_PWM_SPEED_ACTIVE_STATE			ACTIVE_LOW
#define	OUTPUT_MOVER_PWM_SPEED_THROTTLE_OLD			AT91_PIN_PA28
#define	OUTPUT_MOVER_PWM_SPEED_THROTTLE 			AT91_PIN_PB2

// H-Bridge
#define OUTPUT_MOVER_MOTOR_POS_ACTIVE_STATE			ACTIVE_LOW
#define	OUTPUT_MOVER_MOTOR_FWD_POS 					AT91_PIN_PB3
#define	OUTPUT_MOVER_MOTOR_REV_POS 					AT91_PIN_PB19

// H-Bridge
#define OUTPUT_MOVER_MOTOR_NEG_ACTIVE_STATE			ACTIVE_HIGH
#define	OUTPUT_MOVER_MOTOR_REV_NEG 					AT91_PIN_PB13
#define	OUTPUT_MOVER_MOTOR_FWD_NEG 					AT91_PIN_PB12


//---------------------------------------------------------------------------
// POWER
//---------------------------------------------------------------------------
#define	OUTPUT_POWER_OFF 				AT91_PIN_PA11

//---------------------------------------------------------------------------
// MUZZLE FLASH
// If BES is active then Muzzle Flash (MFS) and Night Light (Moon Glow)
// and PHI (Positive Hit Indicator) will be disabled.
//---------------------------------------------------------------------------
#define	OUTPUT_MUZZLE_FLASH_ACTIVE_STATE		ACTIVE_LOW
#define	OUTPUT_MUZZLE_FLASH 				AT91_PIN_PA10
#define	OUTPUT_NIGHT_LIGHT 				AT91_PIN_PA6
#define	OUTPUT_HIT_INDICATOR 				AT91_PIN_PA9

//---------------------------------------------------------------------------
// BES - Battlefield Effects Simulator
// If BES is active then Night Light (Moon Glow) and PHI (Positive Hit Indicator)
// will be disabled. As of 07/25/2013 the enable line is no longer used. We
// must provide power continuously to the BES.
//---------------------------------------------------------------------------
#define	OUTPUT_BES_ENABLE_ACTIVE_STATE		ACTIVE_LOW
#define	OUTPUT_BES_TRIGGER_1_ACTIVE_STATE	ACTIVE_HIGH
#define	OUTPUT_BES_TRIGGER_2_ACTIVE_STATE	ACTIVE_HIGH
#define	OUTPUT_BES_TRIGGER_3_ACTIVE_STATE	ACTIVE_HIGH
#define	OUTPUT_BES_TRIGGER_4_ACTIVE_STATE	ACTIVE_LOW
#define	OUTPUT_BES_ENABLE 			AT91_PIN_PA4 // J9-2 Lifter board
#define	OUTPUT_BES_TRIGGER_1 			AT91_PIN_PA8 // J9-4 Lifter board
#define	OUTPUT_BES_TRIGGER_2 			AT91_PIN_PA6 // J9-6 Lifter board
#define	OUTPUT_BES_TRIGGER_3 			AT91_PIN_PA9 // J9-7 Lifter board
#define	OUTPUT_BES_TRIGGER_4 			AT91_PIN_PA4 // J9-2 Lifter board

//---------------------------------------------------------------------------
// MISC.
//---------------------------------------------------------------------------
#define	OUTPUT_AUX 						AT91_PIN_PA8
#define	INPUT_SPARE_IN 					AT91_PIN_PC0

#define OUTPUT_THERMAL_ACTIVE_STATE		ACTIVE_LOW
#define	OUTPUT_THERMAL 					AT91_PIN_PA5
// Thermal pulse is on SATs and uses the same pin as MFS on SITs
#define OUTPUT_THERMAL_PULSE_ACTIVE_STATE		ACTIVE_HIGH
#define	OUTPUT_THERMAL_PULSE 				AT91_PIN_PA10 // J9-8 Lifter board

#define	OUTPUT_SMOKE 					AT91_PIN_PA4
#define	OUTPUT_M21_RELAY 				AT91_PIN_PB30

#define	OUTPUT_RADIO_CH_A_B 			AT91_PIN_PA7

#define	NCHS_ROD_A_LOWER 				AT91_PIN_PA26
#define	NCHS_ROD_A_UPPER 				AT91_PIN_PC9
#define	NCHS_ROD_B_LOWER 				AT91_PIN_PA27
#define	NCHS_ROD_B_UPPER 				AT91_PIN_PC7
#define  NCHS_PULLUP_STATE          PULLUP_ON

#endif //  __TARGET_HARDWARE_H__
