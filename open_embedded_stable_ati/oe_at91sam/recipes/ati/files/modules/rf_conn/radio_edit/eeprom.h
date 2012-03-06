#ifndef _EEPROM_H_
#define _EEPROM_H_

// reads a string from the eeprom, if it is blank then returns the default_string
static void ReadEepromStr(int address, int size, char *default_string, char *dest_buf);
// reads an int from the eeprom, if it is blank then returns the default_int
static int ReadEepromInt(int address, int size, int default_int);

// runs the actual command to get the console output
static void runCMDstr(char *cmd, char *default_string, char *dest_buf);
static int runCMDint(char *cmd, int default_int);

#endif
