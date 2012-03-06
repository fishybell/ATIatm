#include "radio_edit.h"

int verbose, RFfd;

// tcp port we'll listen to for new connections
#define defaultPORT "/dev/ttyS1"

// size of client buffer
#define CLIENT_BUFFER 1024

void DieWithError(char *errorMessage){
   char buf[200];
   strerror_r(errno,buf,200);
   DCMSG(RED,"dtxm_edit %s %s \n", errorMessage,buf);
   if (RFfd != -1) {
      CloseRadio(RFfd);
   }
   exit(1);
}

void print_help(int exval) {
   printf("dtxm_edit [-h] [-v verbosity] [-t serial_device] \n\n");
   printf("  -h              print this help and exit\n");
   printf("  -t /dev/ttyS1   set serial port device\n");
   print_verbosity();    
   exit(exval);
}

// structure that defines eeprom locations
typedef struct radio_eeprom {
   uint16 addr;
   const char *name;
   uint8 size;
} radio_eeprom_t;

// map of useful eeprom addresses in the DTXM
const radio_eeprom_t radio_map[] = {
   {0x0000, "EEFLAG1", 1},
   {0x0001, "EEFLAG2", 1},
   {0x0002, "EEFLAG3", 1},
   {0x0006, "TXDELAY", 1},
   {0x0007, "RXDELAY", 1},
   {0x0009, "TXDELAY2", 1},
   {0x000A, "FTXDELAY", 1},
   {0x000B, "FRXDELAY", 1},
   {0x000C, "FTXDELAY2", 1},
   {0x000D, "FTXCHGP", 1},
   {0x000E, "FRXCHGP", 1},
   {0x000F, "STXCHGP", 1},
   {0x0010, "SRXCHGP", 1},
   {0x0012, "TXTIMOUT", 1},
   {0x0025, "MODEL", 2},
   {0x0027, "MODEL", 5},
   {0x002C, "DATMAN", 2},
   {0x002E, "PINIT", 3},
   {0x0031, "DATPROG", 2},
   {0x0033, "CUSTID", 35},
   {0x0056, "TX_FREQ_ARY Channel 1", 3},
   {0x0059, "TX_FREQ_ARY Channel 2", 3},
   {0x005C, "TX_FREQ_ARY Channel 3", 3},
   {0x005F, "TX_FREQ_ARY Channel 4", 3},
   {0x0062, "TX_FREQ_ARY Channel 5", 3},
   {0x0065, "TX_FREQ_ARY Channel 6", 3},
   {0x0068, "TX_FREQ_ARY Channel 7", 3},
   {0x006B, "TX_FREQ_ARY Channel 8", 3},
   {0x006E, "ETRF_R_ARY Channel 1 TX Frequency PLL R", 3},
   {0x0071, "ETRF_R_ARY Channel 2 TX Frequency PLL R", 3},
   {0x0074, "ETRF_R_ARY Channel 3 TX Frequency PLL R", 3},
   {0x0077, "ETRF_R_ARY Channel 4 TX Frequency PLL R", 3},
   {0x007A, "ETRF_R_ARY Channel 5 TX Frequency PLL R", 3},
   {0x007D, "ETRF_R_ARY Channel 6 TX Frequency PLL R", 3},
   {0x0080, "ETRF_R_ARY Channel 7 TX Frequency PLL R", 3},
   {0x0083, "ETRF_R_ARY Channel 8 TX Frequency PLL R", 3},
   {0x0086, "ETRF_N_ARY Channel 1 TX Frequency PLL N ", 3},
   {0x0089, "ETRF_N_ARY Channel 2 TX Frequency PLL N ", 3},
   {0x008C, "ETRF_N_ARY Channel 3 TX Frequency PLL N ", 3},
   {0x008F, "ETRF_N_ARY Channel 4 TX Frequency PLL N ", 3},
   {0x0092, "ETRF_N_ARY Channel 5 TX Frequency PLL N ", 3},
   {0x0095, "ETRF_N_ARY Channel 6 TX Frequency PLL N ", 3},
   {0x0098, "ETRF_N_ARY Channel 7 TX Frequency PLL N ", 3},
   {0x009B, "ETRF_N_ARY Channel 8 TX Frequency PLL N ", 3},
   {0x009E, "RX_FREQ_ARY Channel 1 RX Frequency", 3},
   {0x00A1, "RX_FREQ_ARY Channel 2 RX Frequency", 3},
   {0x00A4, "RX_FREQ_ARY Channel 3 RX Frequency", 3},
   {0x00A7, "RX_FREQ_ARY Channel 4 RX Frequency", 3},
   {0x00AA, "RX_FREQ_ARY Channel 5 RX Frequency", 3},
   {0x00AD, "RX_FREQ_ARY Channel 6 RX Frequency", 3},
   {0x00B0, "RX_FREQ_ARY Channel 7 RX Frequency", 3},
   {0x00B3, "RX_FREQ_ARY Channel 8 RX Frequency", 3},
   {0x00B6, "ERRF_R_ARY Channel 1 RX Frequency PLL R", 3},
   {0x00B9, "ERRF_R_ARY Channel 2 RX Frequency PLL R", 3},
   {0x00BC, "ERRF_R_ARY Channel 3 RX Frequency PLL R", 3},
   {0x00BF, "ERRF_R_ARY Channel 4 RX Frequency PLL R", 3},
   {0x00C2, "ERRF_R_ARY Channel 5 RX Frequency PLL R", 3},
   {0x00C5, "ERRF_R_ARY Channel 6 RX Frequency PLL R", 3},
   {0x00C8, "ERRF_R_ARY Channel 7 RX Frequency PLL R", 3},
   {0x00CB, "ERRF_R_ARY Channel 8 RX Frequency PLL R", 3},
   {0x00CE, "ERRF_N_ARY Channel 1 RX Frequency PLL N", 3},
   {0x00D1, "ERRF_N_ARY Channel 2 RX Frequency PLL N", 3},
   {0x00D4, "ERRF_N_ARY Channel 3 RX Frequency PLL N", 3},
   {0x00D7, "ERRF_N_ARY Channel 4 RX Frequency PLL N", 3},
   {0x00DA, "ERRF_N_ARY Channel 5 RX Frequency PLL N", 3},
   {0x00DD, "ERRF_N_ARY Channel 6 RX Frequency PLL N", 3},
   {0x00E0, "ERRF_N_ARY Channel 7 RX Frequency PLL N", 3},
   {0x00E3, "ERRF_N_ARY Channel 8 RX Frequency PLL N", 3},
   {0x00E3, "ERRF_N_ARY Channel 8 RX Frequency PLL N", 3},
   {0x00E6, "EETXFR_ARY Channel 1 TX Frequency Trim", 1},
   {0x00E7, "EETXFR_ARY Channel 2 TX Frequency Trim", 1},
   {0x00E8, "EETXFR_ARY Channel 3 TX Frequency Trim", 1},
   {0x00E9, "EETXFR_ARY Channel 4 TX Frequency Trim", 1},
   {0x00EA, "EETXFR_ARY Channel 5 TX Frequency Trim", 1},
   {0x00EB, "EETXFR_ARY Channel 6 TX Frequency Trim", 1},
   {0x00EC, "EETXFR_ARY Channel 7 TX Frequency Trim", 1},
   {0x00ED, "EETXFR_ARY Channel 8 TX Frequency Trim", 1},
   {0x00EE, "EERXFR_ARY Channel 1 RX Frequency Trim", 1},
   {0x00EF, "EERXFR_ARY Channel 2 RX Frequency Trim", 1},
   {0x00F0, "EERXFR_ARY Channel 3 RX Frequency Trim", 1},
   {0x00F1, "EERXFR_ARY Channel 4 RX Frequency Trim", 1},
   {0x00F2, "EERXFR_ARY Channel 5 RX Frequency Trim", 1},
   {0x00F3, "EERXFR_ARY Channel 6 RX Frequency Trim", 1},
   {0x00F4, "EERXFR_ARY Channel 7 RX Frequency Trim", 1},
   {0x00F5, "EERXFR_ARY Channel 8 RX Frequency Trim", 1},
   {0x00F6, "EEBAL_ARY Channel 1 Balance", 1},
   {0x00F7, "EEBAL_ARY Channel 2 Balance", 1},
   {0x00F8, "EEBAL_ARY Channel 3 Balance", 1},
   {0x00F9, "EEBAL_ARY Channel 4 Balance", 1},
   {0x00FA, "EEBAL_ARY Channel 5 Balance", 1},
   {0x00FB, "EEBAL_ARY Channel 6 Balance", 1},
   {0x00FC, "EEBAL_ARY Channel 7 Balance", 1},
   {0x00FD, "EEBAL_ARY Channel 8 Balance", 1},
   {0x00FE, "EEDEV_ARY Channel 1 Deviation", 1},
   {0x00FF, "EEDEV_ARY Channel 2 Deviation", 1},
   {0x0100, "EEDEV_ARY Channel 3 Deviation", 1},
   {0x0101, "EEDEV_ARY Channel 4 Deviation", 1},
   {0x0102, "EEDEV_ARY Channel 5 Deviation", 1},
   {0x0103, "EEDEV_ARY Channel 6 Deviation", 1},
   {0x0104, "EEDEV_ARY Channel 7 Deviation", 1},
   {0x0105, "EEDEV_ARY Channel 8 Deviation", 1},
   {0x010E, "EEATXGN_ARY Channel 1 AUX IN Gain", 1},
   {0x010F, "EEATXGN_ARY Channel 2 AUX IN Gain", 1},
   {0x0110, "EEATXGN_ARY Channel 3 AUX IN Gain", 1},
   {0x0111, "EEATXGN_ARY Channel 4 AUX IN Gain", 1},
   {0x0112, "EEATXGN_ARY Channel 5 AUX IN Gain", 1},
   {0x0113, "EEATXGN_ARY Channel 6 AUX IN Gain", 1},
   {0x0114, "EEATXGN_ARY Channel 7 AUX IN Gain", 1},
   {0x0115, "EEATXGN_ARY Channel 8 AUX IN Gain", 1},
   {0x0116, "EEARXGN_ARY Channel 1 AUX OUT Gain", 1},
   {0x0117, "EEARXGN_ARY Channel 2 AUX OUT Gain", 1},
   {0x0118, "EEARXGN_ARY Channel 3 AUX OUT Gain", 1},
   {0x0119, "EEARXGN_ARY Channel 4 AUX OUT Gain", 1},
   {0x011A, "EEARXGN_ARY Channel 5 AUX OUT Gain", 1},
   {0x011B, "EEARXGN_ARY Channel 6 AUX OUT Gain", 1},
   {0x011C, "EEARXGN_ARY Channel 7 AUX OUT Gain", 1},
   {0x011D, "EEARXGN_ARY Channel 8 AUX OUT Gain", 1},
   {0x011E, "EERXGN_ARY Channel 1 AUDIO PA Gain", 1},
   {0x011F, "EERXGN_ARY Channel 2 AUDIO PA Gain", 1},
   {0x0120, "EERXGN_ARY Channel 3 AUDIO PA Gain", 1},
   {0x0121, "EERXGN_ARY Channel 4 AUDIO PA Gain", 1},
   {0x0122, "EERXGN_ARY Channel 5 AUDIO PA Gain", 1},
   {0x0123, "EERXGN_ARY Channel 6 AUDIO PA Gain", 1},
   {0x0124, "EERXGN_ARY Channel 7 AUDIO PA Gain", 1},
   {0x0125, "EERXGN_ARY Channel 8 AUDIO PA Gain", 1},
   {0x0126, "EETXPWR_ARY Channel 1 TX Power Set", 1},
   {0x0127, "EETXPWR_ARY Channel 2 TX Power Set", 1},
   {0x0128, "EETXPWR_ARY Channel 3 TX Power Set", 1},
   {0x0129, "EETXPWR_ARY Channel 4 TX Power Set", 1},
   {0x012A, "EETXPWR_ARY Channel 5 TX Power Set", 1},
   {0x012B, "EETXPWR_ARY Channel 6 TX Power Set", 1},
   {0x012C, "EETXPWR_ARY Channel 7 TX Power Set", 1},
   {0x012D, "EETXPWR_ARY Channel 8 TX Power Set", 1},
   {0x012E, "EETXLOPWR_ARY Channel 1 TX Low Power Set", 1},
   {0x012F, "EETXLOPWR_ARY Channel 2 TX Low Power Set", 1},
   {0x0130, "EETXLOPWR_ARY Channel 3 TX Low Power Set", 1},
   {0x0131, "EETXLOPWR_ARY Channel 4 TX Low Power Set", 1},
   {0x0132, "EETXLOPWR_ARY Channel 5 TX Low Power Set", 1},
   {0x0133, "EETXLOPWR_ARY Channel 6 TX Low Power Set", 1},
   {0x0134, "EETXLOPWR_ARY Channel 7 TX Low Power Set", 1},
   {0x0135, "EETXLOPWR_ARY Channel 8 TX Low Power Set", 1},
   {0x0136, "EESQLOLIM_ARY Channel 1 Lower Squelch Set", 1},
   {0x0137, "EESQLOLIM_ARY Channel 2 Lower Squelch Set", 1},
   {0x0138, "EESQLOLIM_ARY Channel 3 Lower Squelch Set", 1},
   {0x0139, "EESQLOLIM_ARY Channel 4 Lower Squelch Set", 1},
   {0x013A, "EESQLOLIM_ARY Channel 5 Lower Squelch Set", 1},
   {0x013B, "EESQLOLIM_ARY Channel 6 Lower Squelch Set", 1},
   {0x013C, "EESQLOLIM_ARY Channel 7 Lower Squelch Set", 1},
   {0x013D, "EESQLOLIM_ARY Channel 8 Lower Squelch Set", 1},
   {0x013E, "EESQUPLIM_ARY Channel 1 Upper Squelch Set", 1},
   {0x013F, "EESQUPLIM_ARY Channel 2 Upper Squelch Set", 1},
   {0x0140, "EESQUPLIM_ARY Channel 3 Upper Squelch Set", 1},
   {0x0141, "EESQUPLIM_ARY Channel 4 Upper Squelch Set", 1},
   {0x0142, "EESQUPLIM_ARY Channel 5 Upper Squelch Set", 1},
   {0x0143, "EESQUPLIM_ARY Channel 6 Upper Squelch Set", 1},
   {0x0144, "EESQUPLIM_ARY Channel 7 Upper Squelch Set", 1},
   {0x0145, "EESQUPLIM_ARY Channel 8 Upper Squelch Set", 1},
   {0x0151, "COMNLOPWR", 1},
   {0x0152, "COMNHIPWR", 1},
   {0x0153, "EERX_GAIN", 1},
   {0x0154, "EERX_OFFSET", 1},
   {0x0155, "EETX_PRELEN", 1},
   {0x0156, "EEMODE1", 1},
   {0x0157, "NAMEPTR", 2},
   {0x0159, "EEMODE2", 1},
   {0x015A, "EEMODE3", 1},
   {0x015B, "EEMODE4", 1},
   {0x015C, "EEMODE5", 1},
   {0x015D, "EETIMOUT1", 1},
   {0x015E, "RP_SYSTEM_ID", 1},
   {0x015F, "RP_GROUP_ID", 1},
   {0x0160, "RP_UNIT_ID", 1},
   {0x0161, "RP_SUBUNIT_ID", 1},
   {0x0162, "RP_PAD", 2},
   {0x0164, "RP_DD_SYSTEM_ID", 2},
   {0x0165, "RP_DD_GROUP_ID", 1},
   {0x0166, "RP_DD_UNIT_ID", 1},
   {0x0167, "RP_DD_SUBUNIT_ID", 1},
   {0x0168, "RP_PAD2", 3},
   {0x016B, "RP_ControlWd1", 1},
   {0x016C, "RP_ControlWd2", 1},
   {0x016D, "RP_ControlWd3", 1},
   {0x016E, "RP_MessLenMSB", 1},
   {0x016F, "RP_MessLenLSB", 1},
   {0x0170, "DCARRIER", 1},
   {0x0171, "FRAME_STA_LVL", 1},
   {0x0172, "FRAME_STA_RXGAIN", 1},
   {0x0173, "RSSI_THRESHOLD", 1},
   {0x0174, "AFTER_DET_DEL", 1},
   {0x0175, "RP_RETRY_LIMIT", 1},
   {0x0176, "SYNC_METHOD", 1},
   {0x0177, "RP_MODE", 1},
   {0x0178, "RP_TIMOUT", 1},
   {0x0179, "RP_SYSTEM_MASK", 1},
   {0x017A, "RP_GROUP_MASK", 1},
   {0x017B, "RP_UNIT_MASK", 1},
   {0x017C, "RP_SUBUNIT_MASK", 1},
   {0, NULL, 0},
};

/*****************************************************
 * Radio Functions (dtxm specific)                   *
 *****************************************************/
#define R_ECHO 1
#define R_NOECHO 0
void writeRadio(int fd, char *buf, int s, int echo) {
   int r;
   // write it out
   DDCMSG(D_RADIO_VERY, BLUE, "Writing to radio: %s", buf);
   DDCMSG_HEXB(D_RADIO_VERY, BLUE, "In hex: ", buf, s);
   if ((r = write(fd, buf, s)) != s) {
      if (r <= 0) {
         DieWithError("Could not write data");
      } else if (r < s) {
         DieWithError("Could not write all data");
      }
   }
//   usleep(90000); // slight delay
   //fsync(fd); 
   // read back the echo
   //s++; // we write a \r, but read a \r\n
   if (echo) {
      char *nbuf = malloc(s+1);
      DDCMSG(D_RADIO_MEGA, BLUE, "Going to try to read %i (%s)", s, buf);
      while ((r = read(fd, nbuf, s)) > 0) {
//         usleep(90000); // slight delay
         nbuf[r+1] = '\0';
         DDCMSG(D_RADIO_MEGA, BLUE, "Read back %i (%s) of %i", r, nbuf, s);
         if (s == r ) {
            DDCMSG(D_RADIO_MEGA, BLUE, "done reading...");
            break;
         }
         s -= r;
         DDCMSG(D_RADIO_MEGA, BLUE, "going to read again %i", s);
      }
      if (r < 0) {
         DCMSG(RED, "Read back %i (%s) instead of %i (%s)", r, nbuf, s, buf);
         free(nbuf);
         DieWithError("Radio did not echo back");
      }
      free(nbuf);
   }
   DDCMSG(D_RADIO_MEGA, BLUE, "...returning");
}

int readRadio(int fd, char *buf, int s) {
   int err, r = read(fd, buf, s);
//   usleep(90000); // slight delay
   err = errno;
   if (r <= 0) {
      DieWithError("Could not read data");
   }
   DDCMSG(D_RADIO_VERY, CYAN, "Read from radio: %s", buf);
   DDCMSG_HEXB(D_RADIO_VERY, CYAN, "In hex: ", buf, r);
   return s;
}

// opens a tty to the radio and gets it into programming mode (returns -1 on error)
void OpenRadio(char *tty, int *fd) {
   int times=0;
   uint8 test_int;
   // open port
   *fd = open_port(tty, 1); // 1 for blocking

   // set into programming mode
   while (++times <= 3) {
      DDCMSG(D_RADIO, BLACK, "Entering programming mode (%i)", times);
      writeRadio(*fd, "+", 1, R_NOECHO);
      sleep(1);
   }

   // verify we're in programming mode by reading a memory address
   DDCMSG(D_RADIO, BLACK, "Verifying programming mode");
   test_int = ReadRadioEepromInt8(*fd, 0x0196, 1);
   DDCMSG(D_RADIO, BLACK, "Test value read %i, expected %i", test_int, 0x03);
}

// returns the radio to operating mode and closes a the radio file descriptor
void CloseRadio(int fd) {
   DDCMSG(D_RADIO, BLACK, "Closing programming mode");
   writeRadio(fd, "*00\r", 4, R_ECHO);
   close(fd);
}

// reads a string from the radio eeprom
void ReadRadioEepromStr(int fd, int addr, int size, char *dest_buf) {
   int s, i, r=((size*2)+2);
   char *buf = malloc(r+1); // enough to de-hexify it and have extra on end for <cr><nl>NULL
   char msgbuf[18];

   // check contraints
   if (size >= 8 || size < 0) {
      free(buf);
      DCMSG(RED, "Wrong size value: %i", size);
      DieWithError("Could not read radio EEPROM");
   }
   if (addr >= 0xffff || addr < 0) {
      free(buf);
      DCMSG(RED, "Wrong addr value: %8X", addr);
      DieWithError("Could not read radio EEPROM");
   }

   // tell radio what EEPROM we're reading ...
   DDCMSG(D_RADIO_VERY, CYAN, "Reading string (%i) from radio EEPROM (%04X)", size, addr);
   snprintf(msgbuf, 18, "*0202620%i%02X%02X40\r\n", size, (addr & 0xff00) >> 8, addr & 0xff);
   writeRadio(fd, msgbuf, 17, R_ECHO);

   // ... and read it ...
   s=readRadio(fd, buf, r);
   if (s > 0) {
      i = s;
      DDCMSG(D_RADIO_VERY, CYAN, "Read %i bytes", s);
      if (s < r) {
         while ((s = read(fd, buf+i, r)) > 0) {
//            usleep(90000); // slight delay
            DDCMSG(D_RADIO_MEGA, RED, "Null terminating: %i %i", i, s);
            buf[i+s+1] = '\0';
            DDCMSG(D_RADIO_MEGA, BLUE, "Read back %i (%s) of %i", s, buf+i, r);
            if (r == s) {
               DDCMSG(D_RADIO_MEGA, BLUE, "done reading...");
               break;
            }
            r -= s; // amount left to read goes down
            i += s; // total amount read goes up
            DDCMSG(D_RADIO_MEGA, BLUE, "going to read again %i", s);
         }
      }
      DDCMSG(D_RADIO_MEGA, BLUE, "Total read: %i", i);
   } else {
      DDCMSG(D_RADIO_VERY, RED, "Read %i bytes", s);
      DieWithError("Did not read correctly");
   }
   
   // ... and convert it
   for (i = 0; i < size; i++) {
      int x;
      if (sscanf(buf+(i*2), "%2X", &x) != 1) {
         free(buf);
         DCMSG(RED, "failed to convert %s(%i)", buf+(i*2), i);
         DieWithError("Could not convert hex data");
      } else {
         dest_buf[i] = x & 0xff;
      }
   }
   
   free(buf);
}

// reads an int from the radio eeprom
uint32 ReadRadioEepromInt32(int fd, int addr, int size) {
   uint32 ret;
   DDCMSG(D_RADIO_VERY, CYAN, "Reading uint32 from radio EEPROM (%04X) by using 4 byte string...", addr);
   ReadRadioEepromStr(fd, addr, sizeof(uint32), (char*)&ret);
   return ret;
}

// reads an int from the radio eeprom
uint16 ReadRadioEepromInt16(int fd, int addr, int size) {
   uint16 ret;
   DDCMSG(D_RADIO_VERY, CYAN, "Reading uint16 from radio EEPROM (%04X) by using 2 byte string...", addr);
   ReadRadioEepromStr(fd, addr, sizeof(uint16), (char*)&ret);
   return ret;
}

// reads an int from the radio eeprom
uint8 ReadRadioEepromInt8(int fd, int addr, int size) {
   uint8 ret;
   DDCMSG(D_RADIO_VERY, CYAN, "Reading uint8 from radio EEPROM (%04X) by using 1 byte string...", addr);
   ReadRadioEepromStr(fd, addr, sizeof(uint8), (char*)&ret);
   return ret;
}




/*************
 *************  DTXM EEPROM Editing Program
 *************   
 *************  if there is no tty argument, use defaultPORT
 *************  otherwise use the first are as the port number to listen on.
 *************
 *************  This program will read the local EEPROM settings, check against
 *************    what is on the radio currently, and write any differences
 *************
 *************  DO NOT RUN THIS PROGRAM WHILE OTHER RADIO PROGRAMS ARE RUNNING!
 *************
 *************/


int main(int argc, char **argv) {
   int opt;
   int c;
   uint8 test_int8;
   uint16 test_int16;
   uint32 test_int32;
   char test_buf[128];
   char ttyport[32];    /* default to ttyS1  */
   char frequency[64];  /* default frequency (as a string) */
   uint8 powerLow;      /* low power setting */
   uint8 powerHigh;     /* high power setting */
   char radioWritten;   /* radio parameters have been written to radio EEPROM */

   RFfd=-1;             /* File descriptor for RFmodem serial port */
   verbose=0;           /* Verbosity bitfield */

   strcpy(ttyport,"/dev/ttyS1");
   
   while((opt = getopt(argc, argv, "hv:t:")) != -1) {
      switch(opt) {
         case 'h':
         print_help(0);
         break;

         case 'v':
         verbose = strtoul(optarg,NULL,16);
         break;

         case 't':
         strcpy(ttyport,optarg);
         break;
         
         case ':':
         fprintf(stderr, "Error - Option `%c' needs a value\n\n", optopt);
         print_help(1);
         break;

         case '?':
         fprintf(stderr, "Error - No such option: `%c'\n\n", optopt);
         print_help(1);

         break;
      }
   }

   DDCMSG(D_PORT, BLACK, "Watching comm port = <%s>\n", ttyport);

   // set up the RF modem link
   OpenRadio(ttyport, &RFfd); 

   // for now, just read what's in the radios EEPROM and write it out 
   c = 0;
   while (radio_map[c].name != NULL) {
      switch (radio_map[c].size) {
         case sizeof(uint8):
            test_int8 = ReadRadioEepromInt8(RFfd, radio_map[c].addr, sizeof(uint8));
            DDCMSG(D_RADIO, BLACK, "Read %i (%02X) from 0x%04X (%s)", test_int8, test_int8, radio_map[c].addr, radio_map[c].name);
            break;
         case sizeof(uint16):
            test_int16 = ReadRadioEepromInt16(RFfd, radio_map[c].addr, sizeof(uint16));
            DDCMSG(D_RADIO, BLACK, "Read %i (%04X) from 0x%04X (%s)", test_int16, test_int16, radio_map[c].addr, radio_map[c].name);
            break;
         case sizeof(uint32):
            test_int32 = ReadRadioEepromInt32(RFfd, radio_map[c].addr, sizeof(uint32));
            DDCMSG(D_RADIO, BLACK, "Read %i (%08X) from 0x%04X (%s)", test_int32, test_int32, radio_map[c].addr, radio_map[c].name);
            break;
         default:
            ReadRadioEepromStr(RFfd, radio_map[c].addr, radio_map[c].size, test_buf);
            test_buf[radio_map[c].size+1] = '\0'; // null terminate
            DDCMSG(D_RADIO, BLACK, "Read <<<%s>>> from 0x%04X (%s)", test_buf, radio_map[c].addr, radio_map[c].name);
            DDCMSG(D_RADIO, BLACK, "In hex: ", test_buf, radio_map[c].size);
            break;
      }
      c++; // loop through entire map
   }

   // close radio and soft-reset it
   CloseRadio(RFfd);
}






