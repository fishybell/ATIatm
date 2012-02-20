#include <string.h>
#include <stdio.h>

#include "eeprom.h"

/*************************************************************
/* Reads data from the specified address at the specified size.
/* If the memory location is blank then return the default_int.
/* Returns an int.
/*************************************************************/
int ReadEeprom_int(int address, int size, int default_int) {
   char wbuf[1024];
   snprintf(wbuf, 1024, "/usr/bin/eeprom_rw read -addr 0x%02X -size 0x%02X\n", address, size);
   return runCMD_int(wbuf, 1, default_int); // 1 == read
}

/*************************************************************
/* Reads data from the specified address at the specified size.
/* If the memory location is blank then return the default_string.
/* Sets dest_buf to the resulting string
/*************************************************************/
void ReadEeprom_str(int address, int size, char* default_string, char *dest_buf) {
   char wbuf[1024];
   snprintf(wbuf, 1024, "/usr/bin/eeprom_rw read -addr 0x%02X -size 0x%02X\n", address, size);
   runCMD_str(wbuf, 1, default_string, dest_buf); // 1 == read
}

/*************************************************************
/* Opens up the memory location for reading or writing.
/* Returns either the data at the location or the default value
/* if the location is blank.
/* Returns an int.
/*************************************************************/
int runCMD_int(char *cmd, int read, int default_int) {
   char data[1024];
   int output;
   FILE *fp;
   int buffer_max = 256;
   int index = 0, r = 1;
   int status;

   // open pipe
   fp = popen(cmd, read?"r":"w");
   if (fp == NULL) {
      //return "Error: fp is NULL";
   }

   // read/write
   while (index < 1024 && r > 0) {
      r = fread(data + index, buffer_max, sizeof(char), fp);
      index += r;
   }

   // close pipe
   status = pclose(fp);
   if (status == -1) {
      //return "Error: could not close";
   }
	// convert the data to an int
   if (sscanf(data, "%i", &output) != 1) {
     // if the memory location is blank or invalid use the default value
	  output = default_int;
   }
   return output;	
}

/*************************************************************
/* Opens up the memory location for reading or writing.
/* Returns either the data at the location or the default value
/* if the location is blank.
/* Sets dest_buf to the resulting string
/*************************************************************/
void runCMD_str(char *cmd, int read, char *default_string, char *dest_buf) {
   char data[1024];
   FILE *fp;
   int buffer_max = 256;
   char buffer[buffer_max];
   int index = 0, r = 1;
   int status;

   // open pipe
   fp = popen(cmd, read?"r":"w");
   if (fp == NULL) {
      return;
   }

   // read/write
   while (index < 1024 && r > 0) {
      r = fread(data + index, buffer_max, sizeof(char), fp);
      index += r;
   }

   // close pipe
   status = pclose(fp);
   if (status == -1) {
      return;
   }
   // if the memory location is blank use the default value
   if (strnlen(data, 1024) <= 0) {
      memcpy(dest_buf, default_string, strnlen(default_string,1024));
   } else {
      memcpy(dest_buf, data, strnlen(data,1024));
   }
}




