#include "eeprom.h"
#include "radio_edit.h"

/*************************************************************
 * Reads data from the specified address at the specified size.
 * If the memory location is blank then return the default_string.
 * Sets dest_buf to the resulting string
 *************************************************************/
void ReadEepromStr(int address, int size, char* default_string, char *dest_buf) {
   char wbuf[1024];
   snprintf(wbuf, 1024, "/usr/bin/eeprom_rw read -addr 0x%04X -size 0x%02X\n", address, size);
   runCMDstr(wbuf, default_string, dest_buf);
}

/***************************************************************
 * Reads data from the specified address at the specified size.
 * If the memory location is blank then return the default_int.
 * Returns an int.
 *************************************************************/
int ReadEepromInt(int address, int size, int default_int) {
   char wbuf[1024];
   snprintf(wbuf, 1024, "/usr/bin/eeprom_rw read -addr 0x%04X -size 0x%02X\n", address, size);
   return runCMDint(wbuf, default_int);
}

/***************************************************************
 * Writes data from the specified address at the specified size.
 *************************************************************/
void WriteEepromInt(int address, int size, int default_int) {
   char wbuf[1024];
   snprintf(wbuf, 1024, "/usr/bin/eeprom_rw write -addr 0x%04X -size 0x%02X -blank 0x%02X\n", address, size, size);
   runCMDintW(wbuf, default_int, size);
}

/*************************************************************
 * Opens up the memory location for reading.
 * Returns either the data at the location or the default value
 * if the location is blank.
 * Sets dest_buf to the resulting string
 *************************************************************/
void runCMDstr(char *cmd, char *default_string, char *dest_buf) {
   char data[1024];
   FILE *fp;
   int buffer_max = 256;
   char buffer[buffer_max];
   int index = 0, r = 1;
   int status;

   // open pipe
   fp = popen(cmd, "r");
   if (fp == NULL) {
      DieWithError("Error: fp is NULL");
      return;
   }

   // read
   while (index < 1024 && r > 0) {
      r = fread(data + index, buffer_max, sizeof(char), fp);
      index += r;
   }

   // close pipe
   status = pclose(fp);
   if (status == -1) {
      DieWithError("Error: could not close");
      return;
   }
   // if the memory location is blank use the default value
   if (strnlen(data, 1024) <= 0) {
      memcpy(dest_buf, default_string, strnlen(default_string,1024));
   } else {
      memcpy(dest_buf, data, strnlen(data,1024));
   }
}

/*************************************************************
 * Opens up the memory location for reading.
 * Returns either the data at the location or the default value
 * if the location is blank.
 * Returns an int.
 *************************************************************/
int runCMDint(char *cmd, int default_int) {
   char data[1024];
   int output;
   FILE *fp;
   int buffer_max = 256;
   int index = 0, r = 1;
   int status;

   // open pipe
   fp = popen(cmd, "r");
   if (fp == NULL) {
      DieWithError("Error: fp is NULL");
      return default_int;
   }

   // read
   while (index < 1024 && r > 0) {
      r = fread(data + index, buffer_max, sizeof(char), fp);
      index += r;
   }

   // close pipe
   status = pclose(fp);
   if (status == -1) {
      DieWithError("Error: could not close");
      return default_int;
   }
	// convert the data to an int
   if (sscanf(data, "%i", &output) != 1) {
     // if the memory location is blank or invalid use the default value
	  output = default_int;
   }
   return output;	
}

/*************************************************************
 * Opens up the memory location for writing.
 *************************************************************/
void runCMDintW(char *cmd, int default_int, int size) {
   char data[1024];
   int output;
   FILE *fp;
   int buffer_max = 256;
   int index = 0, r = 1;
   int status;

   // open pipe
   fp = popen(cmd, "w");
   if (fp == NULL) {
      DieWithError("Error: fp is NULL");
   }

   // format data
   snprintf(data, min(size,1024), "%i", default_int);

   // write
   while (index < 1024 && r > 0 && index < size) {
      r = fwrite(data + index, min(size,buffer_max), sizeof(char), fp);
      index += r;
   }

   // close pipe
   status = pclose(fp);
   if (status == -1) {
      DieWithError("Error: could not close");
   }
}



