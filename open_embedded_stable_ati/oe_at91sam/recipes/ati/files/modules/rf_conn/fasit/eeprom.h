#ifndef _EEPROM_H_
#define _EEPROM_H_

#include "../../defaults.h"

// reads an int from the eeprom, if it is blank then returns the default_int
int ReadEeprom_int(int address, int size, int default_int);
// reads a string from the eeprom, if it is blank then returns the default_string
void ReadEeprom_str(int address, int size, char *default_string, char *dest_buf);

// runs the actual command to get the console output
int runCMD_int(char *cmd, int read, int default_int);
void runCMD_str(char *cmd, int read, char *default_string, char *dest_buf);

#endif
