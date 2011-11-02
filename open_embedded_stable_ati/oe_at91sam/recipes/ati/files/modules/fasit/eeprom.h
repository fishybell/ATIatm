#ifndef _EEPROM_H_
#define _EEPROM_H_

#include <map>
#include <list>
#include <string.h>

using namespace std;

// generic base class for handling reading and writing to the eeprom board
class Eeprom {
public:
	// reads a string from the eeprom, if it is blank then returns the default_string
    static string ReadEeprom(int address, int size, string default_string);
    // reads an int from the eeprom, if it is blank then returns the default_int
    static int ReadEeprom(int address, int size, int default_int);

private:
	// runs the actual command to get the console output
    static int runCMD(string cmd, int read, int default_int);
    static string runCMD(string cmd, int read, string default_string);

};
#endif
