// target_hardware.h

#ifndef __TARGET_HARDWARE_H__
#define __TARGET_HARDWARE_H__

//---------------------------------------------------------------------------
// LOGICAL ACTIVE STATES
//---------------------------------------------------------------------------
#define ACTIVE_LOW						0
#define ACTIVE_HIGH						1

//---------------------------------------------------------------------------
// INOUT PULL-UP RESISTOR SETTING
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
#define INPUT_LIFTER_POS_ACTIVE_STATE		ACTIVE_LOW
#define INPUT_LIFTER_POS_PULLUP_STATE		PULLUP_ON
#define INPUT_LIFTER_POS_DEGLITCH_STATE		DEGLITCH_ON
#define	INPUT_LIFTER_POS_UP_LIMIT 			AT91_PIN_PC5
#define	INPUT_LIFTER_POS_DOWN_LIMIT 		AT91_PIN_PC4

#define OUTPUT_LIFTER_MOTOR_ACTIVE_STATE	ACTIVE_LOW
#define	OUTPUT_LIFTER_MOTOR_FWD_POS 		AT91_PIN_PB3
#define	OUTPUT_LIFTER_MOTOR_REV_POS 		AT91_PIN_PB19
#define	OUTPUT_LIFTER_MOTOR_REV_NEG 		AT91_PIN_PB13
#define	OUTPUT_LIFTER_MOTOR_FWD_NEG 		AT91_PIN_PB12

//---------------------------------------------------------------------------
// BATTERY
//---------------------------------------------------------------------------
#define	INPUT_ADC_LOW_BAT 				AT91_PIN_PC1
#define	OUTPUT_LED_LOW_BAT 				AT91_PIN_PC15

//---------------------------------------------------------------------------
// USER INTERFACE
//---------------------------------------------------------------------------
#define	INPUT_TEST_BUTTON 				AT91_PIN_PC12
#define	OUTPUT_TEST_INDICATOR 			AT91_PIN_PC8
#define	INPUT_WAKEUP_BUTTON 			AT91_PIN_PB25

//---------------------------------------------------------------------------
// HIT SENSOR - MECHANICAL
//---------------------------------------------------------------------------
#define	INPUT_HIT_SENSOR 				AT91_PIN_PB24
#define	INPUT_FRONT_TIRE_HIT 			AT91_PIN_PB21
#define	INPUT_BACK_TIRE_HIT 			AT91_PIN_PB20
#define	INPUT_ENGINE_HIT 				AT91_PIN_PA3

//---------------------------------------------------------------------------
// HIT SENSOR - MILES RECEIVER
//---------------------------------------------------------------------------
#define	INPUT_MILES_HIT 				AT91_PIN_PB23
#define	OUTPUT_MILES_RESET 				AT91_PIN_PB22

//---------------------------------------------------------------------------
// MILE TRANSMITTER
//---------------------------------------------------------------------------
#define	OUTPUT_MILES_SHOOTBACK 			AT91_PIN_PC11


#define	INPUT_SPEED_SENSOR1 			AT91_PIN_PB0
#define	INPUT_SPEED_SENSOR2 			AT91_PIN_PB1
#define	INPUT_TRACK_SENSOR1 			AT91_PIN_PB10
#define	INPUT_TRACK_SENSOR2 			AT91_PIN_PB11

#define	OUTPUT_APPLY_BRAKE 				AT91_PIN_PB9

#define	INPUT_DOCKED 					AT91_PIN_PB8

#define	INPUT_END_OF_TRACK1 			AT91_PIN_PA30
#define	INPUT_END_OF_TRACK2 			AT91_PIN_PA31

#define	OUTPUT_PWM_SPEED_THROTTLE 		AT91_PIN_PB2
#define	OUTPUT_MOVER_FORWARD 			AT91_PIN_PA29
#define	OUTPUT_MOVER_REVERSE 			AT91_PIN_PA25

#define	OUTPUT_POWER_OFF 				AT91_PIN_PA11

#define	OUTPUT_MUZZLE_FLASH 			AT91_PIN_PA10
#define	OUTPUT_HIT_INDICATOR 			AT91_PIN_PA9
#define	OUTPUT_AUX 						AT91_PIN_PA8
#define	INPUT_SPARE_IN 					AT91_PIN_PC0
#define	OUTPUT_NIGHT_LIGHT 				AT91_PIN_PA6
#define	OUTPUT_THERMAL 					AT91_PIN_PA5
#define	OUTPUT_SMOKE 					AT91_PIN_PA4
#define	OUTPUT_M21_RELAY 				AT91_PIN_PC2

#define	OUTPUT_RADIO_CH_A_B 			AT91_PIN_PA7

#define	ROD1A_LOMAH_NCHS 				AT91_PIN_PA26
#define	ROD1B_LOMAH_NCHS 				AT91_PIN_PA27
#define	ROD2A_LOMAH_NCHS 				AT91_PIN_PC7
#define	ROD2B_LOMAH_NCHS 				AT91_PIN_PC9

#endif //  __TARGET_HARDWARE_H__
