#ifndef _EEPROM_H_
#define _EEPROM_H_

// reads a string from the eeprom, if it is blank then returns the default_string
void ReadEepromStr(int address, int size, char *default_string, char *dest_buf);
// reads an int from the eeprom, if it is blank then returns the default_int
int ReadEepromInt(int address, int size, int default_int);
// writes an int from the eeprom
void WriteEepromInt(int address, int size, int default_int);

// runs the actual command to get the console output
void runCMDstr(char *cmd, char *default_string, char *dest_buf);
int runCMDint(char *cmd, int default_int);
void runCMDintW(char *cmd, int default_int, int size); // write data instead of reading it

#endif
