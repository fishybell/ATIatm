#ifndef __DTXM_EDIT_H__
#define __DTXM_EDIT_H__

#include "mcp.h"
#include "rf.h"

// reuse the existing verbosity bits, but repurpose their meanings
#define D_EEPROM 1 /* was D_PACKET */
#define D_RADIO  2 /* was D_RF */
#define D_MATH   4 /* was D_CRC */
#define D_PORT   8 /* was D_POLL */
#define D_EEPROM_VERY 0x11 /* D_EEPROM with D_VERY */
#define D_RADIO_VERY 0x12 /* D_RADIO with D_VERY */
#define D_EEPROM_MEGA 0x81 /* D_EEPROM with D_MEGA */
#define D_RADIO_MEGA 0x82 /* D_RADIO with D_MEGA */

void DieWithError(char *errorMessage);

// opens a tty to the radio and gets it into programming mode (returns -1 on error)
void OpenRadio(char *tty, int *fd);
// returns the radio to operating mode and closes a the radio file descriptor
void CloseRadio(int fd);

// reads a string from the radio eeprom
void ReadRadioEepromStr(int fd, int addr, int size, char *dest_buf);
// reads an int from the radio eeprom
uint32 ReadRadioEepromInt32(int fd, int addr, int size);
uint16 ReadRadioEepromInt16(int fd, int addr, int size);
uint8 ReadRadioEepromInt8(int fd, int addr, int size);

// writes a string to the radio eeprom
void WriteRadioEepromStr(int fd, int addr, int size, char *src_buf);
// writes an int to the radio eeprom
void WriteRadioEepromInt32(int fd, int addr, int size, uint32 src_int);
void WriteRadioEepromInt16(int fd, int addr, int size, uint16 src_int);
void WriteRadioEepromInt8(int fd, int addr, int size, uint8 src_int);


#endif

