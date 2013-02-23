#include "radio_edit.h"
#include "rf.h"
#include "eeprom.h"
#include "../../defaults.h"

int verbose, RFfd;
const char *__PROGRAM__ = "dtxm_edit ";

// a small delay that we inject into the reading/writing portions of the code
//#define SMALL_DELAY usleep(100); DCMSG(RED, ".%i", __LINE__);
#define SMALL_DELAY usleep(50);

// tcp port we'll listen to for new connections
#define defaultPORT "/dev/ttyS1"

// size of client buffer
#define CLIENT_BUFFER 1024

// number of times to keep trying a read
#define TIMEOUT 0xfff

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
   printf("  -q [1:2:3]      run test read/read-raw/read-write\n");
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
   {0x0027, "SNUM", 5},
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
   {0x017D, "Unknown", 3},
   {0x0180, "Unknown", 3},
   {0x0183, "Unknown", 3},
   {0x0186, "Unknown", 3},
   {0x0189, "Unknown", 3},
   {0x018C, "Unknown", 3},
   {0x018F, "Unknown", 3},
   {0x0192, "Unknown", 3},
   {0x0195, "Unknown", 1},
   {0x0196, "EECHANNEL", 1},
   {0x0197, "Unknown", 1},
   {0x0198, "Unknown", 3},
   {0x019B, "Unknown", 3},
   {0x019E, "Unknown", 3},
   {0x01A1, "Unknown", 3},
   {0x01A4, "Unknown", 3},
   {0x01A7, "Unknown", 3},
   {0x01AA, "Unknown", 3},
   {0x01AD, "Unknown", 3},
   {0x01B0, "Unknown", 3},
   {0x01B3, "Unknown", 3},
   {0x01B6, "Unknown", 3},
   {0x01B9, "Unknown", 3},
   {0x01BC, "Unknown", 3},
   {0x01BF, "Unknown", 3},
   {0x01C2, "Unknown", 3},
   {0x01C5, "Unknown", 3},
   {0x01C8, "Unknown", 3},
   {0x01CB, "Unknown", 3},
   {0x01CE, "Unknown", 3},
   {0x01D1, "Unknown", 3},
   {0x01D4, "Unknown", 3},
   {0x01D7, "Unknown", 3},
   {0x01DA, "Unknown", 3},
   {0x01DD, "Unknown", 3},
   {0x01E0, "Unknown", 3},
   {0x01E3, "Unknown", 3},
   {0x01E6, "Unknown", 3},
   {0x01E9, "Unknown", 3},
   {0x01EC, "Unknown", 3},
   {0x01EF, "Unknown", 3},
   {0x01F2, "Unknown", 3},
   {0x01F5, "Unknown", 3},
   {0x01F8, "Unknown", 3},
   {0x01FB, "Unknown", 3},
   {0x01FE, "Unknown", 3},
   {0, NULL, 0},
};

/*****************************************************
 * Radio Functions (dtxm specific)                   *
 *****************************************************/
#define R_ECHO 1
#define R_NOECHO 0
char writeRadio(int fd, char *buf, int s, int echo) { // returns the first byte read of the response if echo is set.
   char retval = '\0';
   int timeout=TIMEOUT; // we won't loop forever
   // if we're not looking for an echo ...
   if (!echo) {
      // ... write the whole thing out at once if possible
      int r;
      // write it out
      DDCMSG(D_RADIO_VERY, BLUE, "!echo: Writing to radio: %s", buf);
      DDCMSG_HEXB(D_RADIO_VERY, BLUE, "!echo: In hex: ", buf, s);
      if ((r = write(fd, buf, s)) != s) {
         if (r <= 0) {
            DieWithError("!echo: Could not write data");
         } else if (r < s) {
            DieWithError("!echo: Could not write all data");
         }
      }
      fsync(fd); 
   } else {
      // ... but if we are looking for an echo, write a character, read it back, etc. until we've written the whole string
      int i; // character we're writing now
      int r; // response when writing/reading
      char ibuf; // single character buffer for reading back
      DDCMSG(D_RADIO_VERY, BLUE, "echo: Writing to radio: %s", buf);
      for (i = 0; i < s; i++) {
         DDCMSG(D_RADIO_MEGA, BLUE, "echo: Writing %i:%c (%02X) @ %i", buf[i], buf[i], buf[i], i);
         while ((r = write(fd, buf+i, 1)) <= 0) { // keep trying to write the single byte...
            if (errno != EAGAIN && timeout-- > 0) { // ...but die if we had a real error
               DieWithError("echo: Failed to write");
            }
            SMALL_DELAY; 
         }
         SMALL_DELAY; 
         while ((r = read(fd, &ibuf, 1)) <= 0) { // keep trying to read the single byte...
            if (errno != EAGAIN && timeout-- > 0) { // ...but die if we had a real error
               DieWithError("echo: Failed to write");
            }
            SMALL_DELAY; 
         }
         SMALL_DELAY; 
         DDCMSG(D_RADIO_MEGA, BLUE, "echo: Read back %i:%c (%02X)", ibuf, ibuf, ibuf);
         while ((buf[i] != '\r' && buf[i] != '\n') && (ibuf == '\r' || ibuf == '\n')) {
            // ignore extraneous <cr> or <nl> bytes
            while ((r = read(fd, &ibuf, 1)) <= 0) { // keep trying to read the single byte...
               if (errno != EAGAIN && timeout-- > 0) { // ...but die if we had a real error
                  DieWithError("echo: Failed to write");
               }
               SMALL_DELAY; 
            }
            SMALL_DELAY; 
         }
      }
      if (echo == 1) {
         // main "echo" read will look for 1 extra character beyond \r \n
         // and for good measure, read extra bytes (yes we need to do this to read the \r and \n it tacks on)
         ibuf = '\r';
         while (ibuf == '\r' || ibuf == '\n') {
            while ((r = read(fd, &ibuf, 1)) <= 0) { // keep trying to read the single byte...
               if (errno != EAGAIN && timeout-- > 0) { // ...but die if we had a real error
                  DieWithError("echo: Failed to write");
               }
            }
            DDCMSG(D_RADIO_MEGA, BLUE, "echo: Read back final %i:%c (%02X)", ibuf, ibuf, ibuf);
            SMALL_DELAY; 
         }
         retval = ibuf;
      } else {
         // alternate "echo" read will look for just \r \n
         echo = 2; // reusing echo for counter
         while (echo-- > 0) {
            while ((r = read(fd, &ibuf, 1)) <= 0) { // keep trying to read the single byte...
               if (errno != EAGAIN && timeout-- > 0) { // ...but die if we had a real error
                  DieWithError("echo2: Failed to write");
               }
            }
            DDCMSG(D_RADIO_MEGA, BLUE, "echo2: Read back final %i:%c (%02X)", ibuf, ibuf, ibuf);
            SMALL_DELAY; 
         }
      }
   }
   DDCMSG(D_RADIO_MEGA, BLUE, "...returning");
   return retval;
}

int readRadio(int fd, char *buf, int s) {
   int r = 0; // bytes read
   int c; // bytes read this time
   int m = s; // bytes remaining to read
   int timeout=TIMEOUT; // we won't loop forever
   DDCMSG(D_RADIO_VERY, YELLOW, "Going to read %i bytes", m);
   while (m > 0) {
      int err;
      DDCMSG(D_RADIO_MEGA, RED, "...attempting m: %i, r: %i, c: %i", m, r, c);
      c = read(fd, buf+r, m);
      err = errno;
      DDCMSG(D_RADIO_MEGA, RED, "...attempted m: %i, r: %i, c: %i, errno: %i", m, r, c, err);
      if (c <= 0 && err != EAGAIN && timeout-- > 0) {
         DieWithError("Fail on read...");
      }
      if (errno != EAGAIN) {
         DDCMSG(D_RADIO_MEGA, YELLOW, "...read %i bytes...", c);
         //buf[r+1] = '\0';
         //DDCMSG(D_RADIO_MEGA, YELLOW, "Bytes so far: %s", buf);
         DDCMSG_HEXB(D_RADIO_MEGA, YELLOW, "\nIn hex: ", buf, c);
         r += c; // bytes read, up
         m -= c; // bytes remaining, down
      }
   }
   DDCMSG(D_RADIO_VERY, YELLOW, "Read %i bytes", r);
   return r;
}

// opens a tty to the radio and gets it into programming mode (returns -1 on error)
void OpenRadio(char *tty, int *fd) {
   int times=0;
   uint8 test_int = 0x55; // will not be this value if read correctly (max of 7 if working)
   // open port
   *fd = open_port(tty, 2); // bit 1 for hardware flow control, bit 2(on) for blocking, bit 3 for IGNBRK | IGNCR

   // set into programming mode
   while (++times <= 3) {
      DDCMSG(D_RADIO, BLACK, "Entering programming mode (%i)", times);
      //writeRadioNow(*fd, "+", 1);
      writeRadio(*fd, "+", 1, R_NOECHO);
      sleep(1); // would usleep(990) be better? would usleep(1010) be better? sometimes it fails to enter programming mode, and I can only theorize it is this line
   }

   // verify we're in programming mode by reading a memory address
   DDCMSG(D_RADIO, BLACK, "Verifying programming mode");
   test_int = ReadRadioEepromInt8(*fd, 0x0196); // address corresponds to EECHANNEL
   DDCMSG(D_RADIO, BLACK, "Test value read %i, expected %i", test_int, 0x03);
   if (test_int >= 8) {
      DieWithError("Could not enter programming mode");
   }
}

// returns the radio to operating mode and closes a the radio file descriptor
void CloseRadio(int fd) {
   DDCMSG(D_RADIO, BLACK, "Closing programming mode");
   //writeRadioNow(fd, "*00\r", 4);
   writeRadio(fd, "*00\r", 4, R_NOECHO);
   close(fd);
}

// reads a string from the radio eeprom
void WriteRadioEepromStr(int fd, int addr, int size, char *src_buf) {
   int p = (8*2); // size of payload, double size (for hexification)
   int s = p + 19 + 13; // payload size + size of format1 + size of format2
   const char *format1 = "*0202620%i%02X%02X004452%s"; // most of command to write eeprom
   const char *format2 = "80\r*02026540\r"; // rest of command and verify command
   char *buf = malloc(p+1); // allocate area for payload and null terminator
   char *mid_buf = malloc(p + 19 + 1); // allow for payload and format1 and null terminator
   char *final_buf = malloc(s+1); // allow for null terminator
   char *ret_buf = malloc(p + 2 +1); // allow for hex return and <cr><nl>NULL
   int i;
   
   // check contraints
   if (size > 8 || size <= 0) {
      int nsize = 0;
      free(buf);
      free(mid_buf);
      free(final_buf);
      free(ret_buf);
      DDCMSG(D_RADIO_VERY,RED, "Wrong size value: %i...calling recursively", size);
//      DieWithError("Could not write radio EEPROM");
      while (nsize < size) {
         int n = min(8, size-nsize); // read up to 8 bytes at a time
         WriteRadioEepromStr(fd, addr+nsize, n, src_buf+nsize);
         nsize+=8; // move on to the next 8
      }
      return;
   }
   if (addr >= 0xffff || addr < 0) {
      free(buf);
      free(mid_buf);
      free(final_buf);
      free(ret_buf);
      DCMSG(RED, "Wrong addr value: %8X", addr);
      DieWithError("Could not write radio EEPROM");
   }

   // format message
   for (i = 0; i < size; i++) { // print payload
      DDCMSG(D_RADIO_MEGA, YELLOW, "Creating payload %i (%02X) @ %08X", i, src_buf[i], buf + (i*2));
      snprintf(buf + (i*2), 3, "%02X", src_buf[i]); // 3 = 2 hex digits + null
   }
   memset(buf+(size*2), '0', p-(size*2)); // fill up to null terminator with ascii zero
   buf[p] = '\0'; // null terminate
   DDCMSG(D_RADIO_VERY, YELLOW, "Created payload: %s @ %08X", buf, buf);
   DDCMSG_HEXB(D_RADIO_MEGA, YELLOW, "In hex:", buf, p+1);
   snprintf(mid_buf, p + 19 + 1, format1, size, (addr & 0xff00) >> 8, addr & 0xff, buf); // print most of command and payload
   DDCMSG(D_RADIO_MEGA, YELLOW, "Created partial message: %s", mid_buf);
   snprintf(final_buf, s + 1, "%s%s", mid_buf, format2);
   DDCMSG(D_RADIO_MEGA, YELLOW, "Created final message: %s", final_buf);

#if 0   
   free(buf);
   free(mid_buf);
   free(final_buf);
   free(ret_buf);
return;
#endif

   // tell radio what we're writing ...
   DDCMSG(D_RADIO_VERY, YELLOW, "Writing %i bytes to radio to write %i bytes to EEPROM:\r\n<<<\r\n\t\t\t%s\r\n>>>", s, size, final_buf);
   ret_buf[0] = writeRadio(fd, final_buf, s, R_ECHO);

   // ... and read verification ...
   p = (size*2) + 1; // for actual bytes written plus carraige return and line feed, minus one already read
   s=readRadio(fd, ret_buf+1, p);
   if (s > 0) {
      i = s; // for the byte we already read
      DDCMSG(D_RADIO_VERY, YELLOW, "Read %i bytes", s);
      if (s < p) {
         while ((s = read(fd, ret_buf+i, p)) > 0) {
            DDCMSG(D_RADIO_MEGA, RED, "Null terminating: %i %i", i, s);
            ret_buf[i+s+1] = '\0';
            DDCMSG(D_RADIO_MEGA, YELLOW, "Read back %i (%s) of %i", s, ret_buf+i, p);
            if (p == s) {
               DDCMSG(D_RADIO_MEGA, YELLOW, "done reading...");
               break;
            }
            p -= s; // amount left to read goes down
            i += s; // total amount read goes up
            DDCMSG(D_RADIO_MEGA, YELLOW, "going to read again %i", s);
         }
      }
      DDCMSG(D_RADIO_MEGA, YELLOW, "Total read: %i", i);

   } else {
      DDCMSG(D_RADIO_VERY, RED, "Read %i bytes", s);
      DieWithError("Did not write correctly");
   }
   ret_buf[i+1] = '\0'; // null terminate
   DDCMSG(D_RADIO_VERY, YELLOW, "Read back %i <<<%s>>>", i, ret_buf);
    
   free(buf);
   free(mid_buf);
   free(final_buf);
   free(ret_buf);
}

// writes an int to the radio eeprom
void WriteRadioEepromInt32(int fd, int addr, uint32 src_int) {
   DDCMSG(D_RADIO_VERY, CYAN, "Writeing uint32 to radio EEPROM (%04X) by using 4 byte string...", addr);
   WriteRadioEepromStr(fd, addr, sizeof(uint32), (char*)&src_int);
}

// writes an int to the radio eeprom
void WriteRadioEepromInt16(int fd, int addr, uint16 src_int) {
   DDCMSG(D_RADIO_VERY, CYAN, "Writeing uint16 to radio EEPROM (%04X) by using 2 byte string...", addr);
   WriteRadioEepromStr(fd, addr, sizeof(uint16), (char*)&src_int);
}

// writes an int to the radio eeprom
void WriteRadioEepromInt8(int fd, int addr, uint8 src_int) {
   DDCMSG(D_RADIO_VERY, CYAN, "Writeing uint8 to radio EEPROM (%04X) by using 1 byte string...", addr);
   WriteRadioEepromStr(fd, addr, sizeof(uint8), (char*)&src_int);
}

// reads a string from the radio eeprom
void ReadRadioEepromStr(int fd, int addr, int size, char *dest_buf) {
   int s, i, r=((size*2)+1);
   char *buf = malloc(r+3); // enough to de-hexify it and have extra on end for <cr><nl>NULL
   char msgbuf[17];
   int timeout=TIMEOUT; // we won't loop forever

   // check contraints
   if (size > 8 || size <= 0) {
      int nsize = 0;
      free(buf);
//      DDCMSG(D_RADIO_VERY,RED, "Wrong size value: %i...calling recursively", size);
//      DieWithError("Could not read radio EEPROM");
      while (nsize < size) {
         int n = min(8, size-nsize); // read up to 8 bytes at a time
         ReadRadioEepromStr(fd, addr+nsize, n, dest_buf+nsize);
         nsize+=8; // move on to the next 8
      }
      return;
   }
   if (addr >= 0xffff || addr < 0) {
      free(buf);
      DCMSG(RED, "Wrong addr value: %8X", addr);
      DieWithError("Could not read radio EEPROM");
   }

   // tell radio what EEPROM we're reading ...
//   DDCMSG(D_RADIO_VERY, CYAN, "Reading string (%i) from radio EEPROM (%04X)", size, addr);
   snprintf(msgbuf, 17, "*0202620%i%02X%02X40\r", size, (addr & 0xff00) >> 8, addr & 0xff);
//   DDCMSG(D_RADIO, CYAN, "Using msg: %s", msgbuf);
   buf[0] = writeRadio(fd, msgbuf, 16, R_ECHO);

   // ... and read it ...
   s=readRadio(fd, buf+1, r);
   if (s > 0) {
      i = s; // for the byte we already read
//      DDCMSG(D_RADIO_VERY, CYAN, "Read %i bytes", s);
      if (s < r) {
         while ((s = read(fd, buf+i, r)) > 0 && timeout-- > 0) {
//            DDCMSG(D_RADIO_MEGA, RED, "Null terminating: %i %i", i, s);
            buf[i+s+1] = '\0';
//            DDCMSG(D_RADIO_MEGA, BLUE, "Read back %i (%s) of %i", s, buf+i, r);
            if (r == s) {
//               DDCMSG(D_RADIO_MEGA, BLUE, "done reading...");
               break;
            }
            r -= s; // amount left to read goes down
            i += s; // total amount read goes up
//            DDCMSG(D_RADIO_MEGA, BLUE, "going to read again %i", s);
         }
      }
//      DDCMSG(D_RADIO_MEGA, BLUE, "Total read: %i", i);
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
uint32 ReadRadioEepromInt32(int fd, int addr) {
   uint32 ret=0;
   DDCMSG(D_RADIO_VERY, CYAN, "Reading uint32 from radio EEPROM (%04X) by using 4 byte string...", addr);
   ReadRadioEepromStr(fd, addr, sizeof(uint32), (char*)&ret);
   return ret;
}

// reads an int from the radio eeprom
uint16 ReadRadioEepromInt16(int fd, int addr) {
   uint16 ret=0;
   DDCMSG(D_RADIO_VERY, CYAN, "Reading uint16 from radio EEPROM (%04X) by using 2 byte string...", addr);
   ReadRadioEepromStr(fd, addr, sizeof(uint16), (char*)&ret);
   return ret;
}

// reads an int from the radio eeprom
uint8 ReadRadioEepromInt8(int fd, int addr) {
   uint8 ret=0;
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
   int test_r = 0, test_raw = 0, test_rw = 0;
   int opt;
   int c;
   uint8 test_int8;
   uint16 test_int16;
   uint32 test_int32;
   char test_buf[128];
   char ttyport[32];    /* default to ttyS1  */

   RFfd=-1;             /* File descriptor for RFmodem serial port */
   verbose=0;           /* Verbosity bitfield */

   strcpy(ttyport,"/dev/ttyS1");
   
   while((opt = getopt(argc, argv, "hv:t:q:")) != -1) {
      switch(opt) {
         case 'h':
            print_help(0);
            break;

         case 'q':
            switch (strtoul(optarg,NULL,16)) {
               case 1: test_r = 1; break;
               case 2: test_raw = 1; break;
               case 3: test_rw = 1; break;
            }
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

   if (test_r) { /* test read */
      // set up the RF modem link
      OpenRadio(ttyport, &RFfd); 

      // for now, just read what's in the radios EEPROM and write it out 
      c = 0;
      while (radio_map[c].name != NULL) {
         switch (radio_map[c].size) {
            case sizeof(uint8):
               test_int8 = ReadRadioEepromInt8(RFfd, radio_map[c].addr);
               DDCMSG(D_RADIO, BLACK, "%04X\t%08X\t%s", radio_map[c].addr, test_int8, radio_map[c].name);
               break;
            case sizeof(uint16):
               test_int16 = ReadRadioEepromInt16(RFfd, radio_map[c].addr);
               DDCMSG(D_RADIO, BLACK, "%04X\t%08X\t%s", radio_map[c].addr, test_int16, radio_map[c].name);
               break;
            case sizeof(uint32):
               test_int32 = ReadRadioEepromInt32(RFfd, radio_map[c].addr);
               DDCMSG(D_RADIO, BLACK, "%04X\t%08X\t%s", radio_map[c].addr, test_int32, radio_map[c].name);
               break;
            case 3:
               ReadRadioEepromStr(RFfd, radio_map[c].addr, radio_map[c].size, test_buf);
               DDCMSG(D_RADIO, BLACK, "%04X\t%02X.%02X.%02X\t%s", radio_map[c].addr, test_buf[0], test_buf[1], test_buf[2], radio_map[c].name);
               break;
            case 5:
               ReadRadioEepromStr(RFfd, radio_map[c].addr, radio_map[c].size, test_buf);
               DDCMSG(D_RADIO, BLACK, "%04X\t%02X.%02X.%02X.%02X.%02X\t%s", radio_map[c].addr, test_buf[0], test_buf[1], test_buf[2], test_buf[3], test_buf[4], radio_map[c].name);
               break;
            default:
               ReadRadioEepromStr(RFfd, radio_map[c].addr, radio_map[c].size, test_buf);
               DDCMSG(D_RADIO, BLACK, "Read %i from 0x%04X (%s)", radio_map[c].size, radio_map[c].addr, radio_map[c].name);
               DDCMSG_HEXB(D_RADIO, BLACK, "In hex: ", test_buf, radio_map[c].size);
               break;
         }
         usleep(300000); // small delay
         c++; // loop through entire map
      }

      // close radio and soft-reset it
      CloseRadio(RFfd);
   } else if (test_raw) { /* test raw-read */
      // set up the RF modem link
      OpenRadio(ttyport, &RFfd); 

      // grab raw eeprom (entire 0x000 -> 0x1ff
      for (c = 0; c < 0x1ff; c+=16) {
            ReadRadioEepromStr(RFfd, c, 16, test_buf);
            DDCMSG(D_RADIO, BLACK, "%04X\t%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", c, test_buf[0], test_buf[1], test_buf[2], test_buf[3], test_buf[4], test_buf[5], test_buf[6], test_buf[7], test_buf[8], test_buf[9], test_buf[10], test_buf[11], test_buf[12], test_buf[13], test_buf[14], test_buf[15]);
      }

      // close radio and soft-reset it
      CloseRadio(RFfd);
   } else if (test_rw) { /* test read/write */
      // set up the RF modem link
      OpenRadio(ttyport, &RFfd); 

      // grab customer id
      ReadRadioEepromStr(RFfd, 0x0033, 35, test_buf);
      DDCMSG(D_RADIO, BLACK, "Read %i from 0x%04X (%s)", 35, 0x0033, "CUSTID");
      DDCMSG_HEXB(D_RADIO, BLACK, "In hex: ", test_buf, 35);

      // change it
      test_buf[0] = 0x30;
      test_buf[1] = 0x31;
      test_buf[2] = 0x32;
      test_buf[3] = 0x33;
      test_buf[4] = 0x34;
      test_buf[5] = 0x00;
      DDCMSG(D_RADIO, BLACK, "Overwriting 5 bytes of 35: %s", test_buf);
      WriteRadioEepromStr(RFfd, 0x0033, 5, test_buf);
      DDCMSG(D_RADIO, BLACK, "Overwriting 1 byte: %02X", '?');
      WriteRadioEepromInt8(RFfd, 0x0038, '?');
      DDCMSG(D_RADIO, BLACK, "Overwriting 2 bytes: %04X", 0x5051);
      WriteRadioEepromInt16(RFfd, 0x0039, 0x5051);
      DDCMSG(D_RADIO, BLACK, "Overwriting 4 bytes: %08X", 0x40414243);
      WriteRadioEepromInt32(RFfd, 0x003B, 0x40414243);
      test_int8 = ReadRadioEepromInt8(RFfd, 0x0038);
      DDCMSG(D_RADIO, BLACK, "Read from 0x0038: %02X", test_int8);
      test_int16 = ReadRadioEepromInt16(RFfd, 0x0039);
      DDCMSG(D_RADIO, BLACK, "Read from 0x0039: %04X", test_int16);
      test_int32 = ReadRadioEepromInt32(RFfd, 0x003B);
      DDCMSG(D_RADIO, BLACK, "Read from 0x003B: %08X", test_int32);

      // read it again
      ReadRadioEepromStr(RFfd, 0x0033, 35, test_buf);
      DDCMSG(D_RADIO, BLACK, "Read %i from 0x%04X (%s)", 35, 0x0033, "CUSTID");
      DDCMSG_HEXB(D_RADIO, BLACK, "In hex: ", test_buf, 35);

      // close radio and soft-reset it
      CloseRadio(RFfd);
   } else { /* actual task is being run */
      char frequency[RADIO_FREQ_SIZE+1];  /* default frequency (as a string) */
      uint8 powerLow;        /* low power setting */
      uint8 powerHigh;       /* high power setting */
      char radioWritten;     /* radio parameters have been written to radio EEPROM */
      float frequency_f;      /* frequency as a float */
      char frequency_bcd[3]; /* frequency as bcd */
      char frequency_pll[24]; /* frequency for pll */
      char *frequency_check;
      int err1=0, err2=0;

      // read EEPROM settings from board
      ReadEepromStr(RADIO_FREQ_LOC, RADIO_FREQ_SIZE, RADIO_FREQ, frequency);
      DDCMSG(D_EEPROM, RED, "Read radio frequency: %s", frequency);
      powerLow = ReadEepromInt(RADIO_POWER_L_LOC, RADIO_POWER_L_SIZE, RADIO_POWER_L);
      DDCMSG(D_EEPROM, RED, "Read radio low power: %i", powerLow);
      powerHigh = ReadEepromInt(RADIO_POWER_H_LOC, RADIO_POWER_H_SIZE, RADIO_POWER_H);
      DDCMSG(D_EEPROM, RED, "Read radio high power: %i", powerHigh);
      radioWritten = ReadEepromInt(RADIO_WRITTEN_LOC, RADIO_WRITTEN_SIZE, (int)RADIO_WRITTEN);
      err1 = errno;
      DDCMSG(D_EEPROM, RED, "Read radio written: %i|%c", radioWritten, radioWritten);

      // verify frequency is valid
      frequency_f = strtof(frequency, &frequency_check);
      err2 = errno;
      DDCMSG(D_EEPROM_VERY, RED, "Read radio frequency float: %f", frequency_f);
      if (frequency_check == frequency || errno == ERANGE) {
         DCMSG(BLACK, "Other errors: %i %i", err1, err2);
         DieWithError("Invalid frequency");
      }
      
      // check if they need to be written
      if (radioWritten != 'Y') {
         // set up the RF modem link
         OpenRadio(ttyport, &RFfd); 

         // write power to radio
         for (c = 0; c < 8; c++) { // 8 channels of low power
            DDCMSG(D_EEPROM|D_RADIO, YELLOW, "Writing powerLow @ %04X : %i", (0x012e)+c, powerLow);
            WriteRadioEepromInt8(RFfd, (0x012e)+c, powerLow);
         }
         for (c = 0; c < 8; c++) { // 8 channels of high power
            DDCMSG(D_EEPROM|D_RADIO, YELLOW, "Writing powerHigh @ %04X : %i", 0x0126+c, powerHigh);
            WriteRadioEepromInt8(RFfd, 0x0126+c, powerHigh);
         }

         // convert frequency to bcd
         frequency_bcd[0] = ((((int)(frequency_f / 100) & 0x0f) << 4) | ((int)(frequency_f / 10) % 10));
         frequency_bcd[1] = ((((int)(frequency_f) % 10) << 4) | ((int)(frequency_f * 10) % 10));
         frequency_bcd[2] = ((((int)(frequency_f * 100) % 10) << 4) | (((int)(frequency_f * 1000) % 10)));
         DDCMSG(D_EEPROM_MEGA, YELLOW, "Converted frequency to bcd: %02X:%02X:%02X", frequency_bcd[0], frequency_bcd[1], frequency_bcd[2]);
         
         // write frequency to radio
         for (c = 0; c < 8; c++) { // 8 channels of low power
            DDCMSG(D_EEPROM|D_RADIO, YELLOW, "Writing TX frequency @ %04X : %02X:%02X:%02X", (0x0056)+(c*3), frequency_bcd[0], frequency_bcd[1], frequency_bcd[2]);
            WriteRadioEepromStr(RFfd, (0x0056)+(c*3), 3, frequency_bcd);
            snprintf(frequency_pll, 24, "*890%i%3.5f\r", c+1, frequency_f);
            DDCMSG(D_EEPROM|D_RADIO, YELLOW, "Writing TX frequency %3.5f direct: %s:%i", frequency_f, frequency_pll, strnlen(frequency_pll, 24));
            writeRadio(RFfd, frequency_pll, strnlen(frequency_pll, 24), 2); // alternate echo
            DDCMSG(D_EEPROM|D_RADIO, YELLOW, "Writing RX frequency @ %04X : %02X:%02X:%02X", (0x009E)+(c*3), frequency_bcd[0], frequency_bcd[1], frequency_bcd[2]);
            WriteRadioEepromStr(RFfd, (0x009E)+(c*3), 3, frequency_bcd);
            snprintf(frequency_pll, 24, "*880%i%3.5f\r", c+1, frequency_f);
            DDCMSG(D_EEPROM|D_RADIO, YELLOW, "Writing RX frequency %3.5f direct: %s:%i", frequency_f, frequency_pll, strnlen(frequency_pll, 24));
            writeRadio(RFfd, frequency_pll, strnlen(frequency_pll, 24), 2); // alternate echo
         }

         // write other radio settings
         DDCMSG(D_EEPROM|D_RADIO|D_VERY, YELLOW, "Writing EECHANNEL @ 0x0196 : 1");
         WriteRadioEepromInt8(RFfd, 0x0196, 1);
         DDCMSG(D_EEPROM|D_RADIO|D_VERY, YELLOW, "Writing Hardware Flow Control @ 0x015B : 0xFF");
         WriteRadioEepromInt8(RFfd, 0x015B, 0xFF);
         test_int8 = ReadRadioEepromInt8(RFfd, 0x0000);
         DDCMSG(D_EEPROM|D_RADIO|D_MEGA, YELLOW, "Read flags from 0x0000: %02X", test_int8);
         if (!(test_int8 & 0x02)) {
            test_int8 |= 0x02; // set extended channel mode
            DDCMSG(D_EEPROM|D_RADIO|D_MEGA, YELLOW, "Writing flags to 0x0000: %02X", test_int8);
            WriteRadioEepromInt8(RFfd, 0x0000, test_int8);
         }

         // close radio and soft-reset it
         CloseRadio(RFfd);

         // save that we wrote it to the radio
         WriteEepromInt(RADIO_WRITTEN_LOC, RADIO_WRITTEN_SIZE, 'Y');
      }
   }
}






