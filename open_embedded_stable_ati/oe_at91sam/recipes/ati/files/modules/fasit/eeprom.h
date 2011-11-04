#ifndef _EEPROM_H_
#define _EEPROM_H_

using namespace std;

// generic base class for handling reading and writing to the eeprom board
class Eeprom {
public:
	// reads a string from the eeprom, if it is blank then returns the default_string
    static void ReadEeprom(int address, int size, char *default_string, char *dest_buf);
    // reads an int from the eeprom, if it is blank then returns the default_int
    static int ReadEeprom(int address, int size, int default_int);

private:
	// runs the actual command to get the console output
    static int runCMD(char *cmd, int read, int default_int);
    static void runCMD(char *cmd, int read, char *default_string, char *dest_buf);

};
#endif
