#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sstream>
#include <stdio.h>

using namespace std;

#include "eeprom.h"
#include "common.h"
#include "tcp_factory.h"

/*************************************************************
/* Reads data from the specified address at the specified size.
/* If the memory location is blank then return the default_int.
/* Returns an int.
/*************************************************************/
int Eeprom::ReadEeprom(int address, int size, int default_int) {
   ostringstream s1;
   s1 << "/usr/bin/eeprom_rw read -addr 0x" << hex << address << " -size 0x" << hex << size << endl;
   //string s2 = s1.str();
   return Eeprom::runCMD(s1.str().c_str(), 1, default_int); // 1 == read
}

/*************************************************************
/* Reads data from the specified address at the specified size.
/* If the memory location is blank then return the default_string.
/* Returns a string.
/*************************************************************/
string Eeprom::ReadEeprom(int address, int size, string default_string) {
   ostringstream s1;
   s1 << "/usr/bin/eeprom_rw read -addr 0x" << hex << address << " -size 0x" << hex << size << endl;
   //string s2 = s1.str();
   return Eeprom::runCMD(s1.str().c_str(), 1, default_string); // 1 == read
}

/*************************************************************
/* Opens up the memory location for reading or writing.
/* Returns either the data at the location or the default value
/* if the location is blank.
/* Returns an int.
/*************************************************************/
int Eeprom::runCMD(string cmd, int read, int default_int) {
FUNCTION_START("::runCMD");
   string data;
   int output;
   FILE *fp;
   int buffer_max = 256;
   char buffer[buffer_max];
   int status;

   // open pipe
   fp = popen(cmd.c_str(), read?"r":"w");
   if (fp == NULL) {
      //return "Error: fp is NULL";
   }

   // read/write
   while (fgets(buffer, buffer_max, fp) != NULL) {
      data.append(buffer);
   }

   // close pipe
   status = pclose(fp);
   if (status == -1) {
      //return "Error: could not close";
   }
   // if the memory location is blank use the default value
   if (data.length() <= 0) {
      output = default_int;
   } else {
	  // convert the data to an int
	  output = atoi(data.c_str());
   }
   return output;	
FUNCTION_END("::runCMD()")
}

/*************************************************************
/* Opens up the memory location for reading or writing.
/* Returns either the data at the location or the default value
/* if the location is blank.
/* Returns a string.
/*************************************************************/
string Eeprom::runCMD(string cmd, int read, string default_string) {
FUNCTION_START("::runCMD");
   string data;
   FILE *fp;
   int buffer_max = 256;
   char buffer[buffer_max];
   int status;

   // open pipe
   fp = popen(cmd.c_str(), read?"r":"w");
   if (fp == NULL) {
      return "Error: fp is NULL";
   }

   // read/write
   while (fgets(buffer, buffer_max, fp) != NULL) {
      data.append(buffer);
   }

   // close pipe
   status = pclose(fp);
   if (status == -1) {
      return "Error: could not close";
   }
   // if the memory location is blank use the default value
   if (data.length() <= 0) {
      data = default_string;
   }
   return data;	
FUNCTION_END("::runCMD()")
}



