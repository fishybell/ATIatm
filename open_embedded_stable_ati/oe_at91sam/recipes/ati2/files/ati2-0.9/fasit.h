#ifndef _FASIT_H_
#define _FASIT_H_

#include <sys/epoll.h>
#include <bits/types.h>
#include <map>

#define UNASSIGNED 0x0000
#define BAD_TNUM 0x5000
#define BASE_STATION 0xffff

// generic base class for parsing and generating FASIT protocol messages
class FASIT {
public :
   FASIT();
   virtual ~FASIT();
   static struct ATI_header createHeader(int mnum, int source, void *data, int size); // creates a valid ATI header for the given message (may need tweaking in certain circumstances)

   virtual int validMessage(int *start, int* end) = 0; // does the buffer contain valid data? message number on yes, 0 on no; sets start and end for where in the buffer it is

   static __uint32_t crc32(void *buf, int start, int end); // create a 32 bit crc for the given buffer
   static __uint8_t crc8(void *buf, int start, int end); // create an 8 bit crc for the given buffer
   static int parity(void *buf, int size); // create a single parity bit for the given buffer

   static __uint64_t swap64(__uint64_t s); // swap the byte order to switch between network and host modes

   virtual int handleEvent(epoll_event *ev) = 0; // called when either ready to read or write; returns -1 if needs to be deleted afterwards

protected :
   void addToBuffer(int rsize, char *rbuf); // appends data to read buffer
   void clearBuffer(int end); // clears buffer up to a certain point
   char *rbuf;
   int rsize;
   static int messageSeq;
};

/***************************************************
*    definition of various messages follows        *
***************************************************/

// we need to add the packed attribute to anything over 1 byte to ensure proper packing
#define PCKD __attribute__ ((packed))
#define PCKD8

/****************************************************************************************/
/* FASIT commands                                                                       */
/****************************************************************************************/

// the byte order of the device and the host are the same, but may be non-obviously the same as described below

/********************************************/
/* FASIT Message Header                     */
/********************************************/
typedef struct FASIT_header {
   __uint16_t num PCKD;
   __uint16_t icd1 PCKD;
   __uint16_t icd2 PCKD;
   __uint32_t seq PCKD;
   __uint32_t rsrvd PCKD;
   __uint16_t length PCKD;
} FASIT_header;
typedef struct ATI_header {
   __uint8_t  magic:3 PCKD8;         // byte 1, bits 0-2
   __uint8_t  parity:1 PCKD8;        // byte 1, bit 3
   __uint8_t  length:4 PCKD8;        // used as delay value when needed
   __uint16_t num PCKD;             // bytes 2,3
   __uint16_t source PCKD;		// bytes 4,5
} ATI_header;
#define ATI_MAGIC 0x3

// Most FASIT responses start with these
typedef struct FASIT_RESPONSE {
   __uint16_t rnum PCKD;
   __uint32_t rseq PCKD;
} FASIT_RESPONSE;

/********************************************/
/* 100 - Device Definition Request          */
/********************************************/
// FASIT command has no message body
typedef struct ATI_100 {
   __uint16_t dest PCKD;
} ATI_100;

/********************************************/
/* 2000 - Pyro Event Command                */
/********************************************/
typedef struct FASIT_2000 {
   __uint8_t  cid;
   __uint16_t zone PCKD;
} FASIT_2000;
typedef struct ATI_2000 {
   __uint16_t dest PCKD;
   FASIT_2000 embed;
} ATI_2000;

/********************************************/
/* 2004 - Pyro Event Command Acknowledge    */
/********************************************/
typedef struct FASIT_2004b { // body
   __u_char   resp;
} FASIT_2004b;
typedef struct FASIT_2004 {
   FASIT_RESPONSE response;
   FASIT_2004b body;
} FASIT_2004;
typedef struct ATI_2004 {
   FASIT_2004b embed;
} ATI_2004;

/********************************************/
/* 2005 - Pyro Device ID & Capabilities     */
/********************************************/
typedef struct FASIT_2005b { // body
   __uint64_t devid PCKD;
   __uint8_t  flags;
} FASIT_2005b;
typedef struct FASIT_2005 {
   FASIT_RESPONSE response;
   FASIT_2005b body;
} FASIT_2005;
#define PYRO_LEGACY (1 << 0)
#define PYRO_PYRO (1 << 1)
#define PYRO_RESERVED ((1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7))
typedef struct ATI_2005s {      // Short : for ATI MAC address that start with 70:5E:AA
   __uint8_t  mac[3];
   __uint8_t  flags;
} ATI_2005s;
typedef struct ATI_2005f {      // Full : for non-ATI MAC address that don't start with 70:5E:AA
   __uint8_t  mac[6];
   __uint8_t  flags;
} ATI_2005f;
#define ATI_MAC1 (0x70)
#define ATI_MAC2 (0x5E)
#define ATI_MAC3 (0xAA)

/********************************************/
/* 2006 - Pyro Device Status                */
/********************************************/
typedef struct FASIT_2006b { // body
   __uint8_t  batt;
   __uint16_t fault PCKD;
   __uint16_t zones PCKD;
} FASIT_2006b;
typedef struct FASIT_2006 {
   FASIT_RESPONSE response;
   FASIT_2006b body;
} FASIT_2006;
typedef struct FASIT_2006z {
   __uint16_t znum PCKD;
   __uint16_t tcount PCKD;
   __uint16_t ccount PCKD;
} FASIT_2006z;
typedef struct ATI_2006 {
   FASIT_2006b embed;
} ATI_2006;
typedef struct ATI_63556 {       // request of zone data ... reply with num (2006 | 0xf000)
   __uint16_t dest PCKD;
   __uint16_t zones[8] PCKD;
   __uint8_t  crc;
} ATI_63556;
typedef struct ATI_42966 {       // response with zone data ... using num (2006 | 0xa000)
   FASIT_2006z zones[8];
   __uint8_t  crc;
} ATI_42966;

/********************************************/
/* 2100 - Event Command                     */
/********************************************/
typedef struct FASIT_2100 {
   __uint8_t  cid;
   __uint8_t  exp;
   __int16_t  asp PCKD;
   __uint16_t dir PCKD;
   __uint8_t  move;
   float      speed PCKD;
   __uint8_t  on;
   __uint16_t hit PCKD;
   __uint8_t  react;
   __uint16_t tokill PCKD;
   __uint16_t sens PCKD;
   __uint8_t  mode;
   __uint16_t burst PCKD;
} FASIT_2100;
// the ATI's 2100 message is made up like this:
// 1x ATI_2100
// 1-4x ATI_2100m
// 1x ATI_2100c
// this allows up to 4 event commands to be sent at once to a large number of different targets
typedef struct ATI_2100 {
   __uint8_t dest[150]; // 1200 1-bit destinations
} ATI_2100;
typedef struct ATI_2100m {
   FASIT_2100 embed;
   __uint16_t reserved PCKD;
} ATI_2100m;
typedef struct ATI_2100c {
   __uint32_t crc PCKD;
} ATI_2100c;

/********************************************/
/* 2101 - Event Command Acknowledge         */
/********************************************/
typedef struct FASIT_2101b { // body
   __u_char   resp;
} FASIT_2101b;
typedef struct FASIT_2101 {
   FASIT_RESPONSE response;
   FASIT_2101b body;
} FASIT_2101;
// for values of resp = 'S' : send back header with number 2101
// for values of resp = 'F' : send back header with number (2101 | 0xa000)
// for other values of resp : send the following with number (2101 | 0xb000)
typedef struct ATI_47157 {
   FASIT_2101b embed;
} ATI_47157;

/********************************************/
/* 2111 - Device ID & Capabilities          */
/********************************************/
typedef struct FASIT_2111b { // body
   __uint64_t devid PCKD;
   __uint8_t  flags;
} FASIT_2111b;
typedef struct FASIT_2111 {
   FASIT_RESPONSE response;
   FASIT_2111b body;
} FASIT_2111;
#define PD_MILES (1 << 0)
#define PD_MUZZLE (1 << 1)
#define PD_GPS (1 << 2)
#define PD_RESERVED ((1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7))
typedef struct ATI_2111s {      // Short : for ATI MAC address that start with 70:5E:AA
   __uint8_t  mac[3];
   __uint8_t  flags;
} ATI_2111s;
typedef struct ATI_2111f {      // Full : for non-ATI MAC address that don't start with 70:5E:AA
   __uint8_t  mac[6];
   __uint8_t  flags;
} ATI_2111f;

/********************************************/
/* 2102 - Presentation Device Status        */
/********************************************/
typedef struct FASIT_2102h { // hit sensor configuration
   __uint8_t  on;
   __uint8_t  react;
   __uint16_t tokill PCKD;
   __uint16_t sens PCKD;
   __uint8_t  mode;
   __uint16_t burst PCKD;
} FASIT_2102h;
typedef struct FASIT_2102b { // body
   __uint8_t  pstatus;
   __uint16_t fault PCKD;
   __uint8_t  exp;
   __int16_t  asp PCKD;
   __uint16_t dir PCKD;
   __uint8_t  move;
   float      speed PCKD;
   __uint16_t pos PCKD;
   __uint8_t  type;
   __uint16_t hit PCKD;
   FASIT_2102h hit_conf;
} FASIT_2102b;
typedef struct FASIT_2102 {
   FASIT_RESPONSE response;
   FASIT_2102b body;
} FASIT_2102;
typedef struct ATI_2102 {       // if possible, this extremely shortened version is sent alone
   __uint16_t pstatus:8 PCKD;       // byte 1
   __uint16_t exp:2 PCKD;    // byte 2, bits 1,2
   __uint16_t move:1 PCKD;   // byte 2, bit 3
   __uint16_t type:3 PCKD;   // byte 2, bits 4,5,6
   __uint16_t pos:10 PCKD;   // byte 2, bits 7,8, byte 3
   __uint16_t speed:12 PCKD; // byte 4, byte 5, bits 1,2,3,4
   __uint16_t hit:12 PCKD;   // byte 5, bits 5,6,7,8, byte 6
} ATI_2102;
typedef struct ATI_2102x {      // if required, this is sent in addtion to ATI_2102 
   FASIT_2102b embed;
   __uint8_t  crc;
} ATI_2102x;

/********************************************/
/* 2114 - Configure MILES Shootback Command */
/********************************************/
typedef struct FASIT_2114 {
   __uint8_t  code;
   __uint8_t  ammo;
   __uint16_t player PCKD;
   __uint8_t  delay;
} FASIT_2114;
typedef struct ATI_2114 {
   __uint16_t dest PCKD;
   FASIT_2114 embed;
} ATI_2114;

/********************************************/
/* 2115 - MILES Shootback Status            */
/********************************************/
typedef struct FASIT_2115b { // body
   __uint8_t  code;
   __uint8_t  ammo;
   __uint16_t player PCKD;
   __uint8_t  delay;
} FASIT_2115b;
typedef struct FASIT_2115 {
   FASIT_RESPONSE response;
   FASIT_2115b body;
} FASIT_2115;
typedef struct ATI_2115 {
   FASIT_2115b embed;
} ATI_2115;

/********************************************/
/* 2110 - Configure Muzzle Flash Command    */
/********************************************/
typedef struct FASIT_2110 {
   __uint8_t  on;
   __uint8_t  mode;
   __uint8_t  idelay;
   __uint8_t  rdelay;
} FASIT_2110;
typedef struct ATI_2110 {
   __uint16_t dest PCKD;
   FASIT_2110 embed;
} ATI_2110;

/********************************************/
/* 2112 - Muzzle Flash Simulation Status    */
/********************************************/
typedef struct FASIT_2112b { // body
   __uint8_t  on;
   __uint8_t  mode;
   __uint8_t  idelay;
   __uint8_t  rdelay;
} FASIT_2112b;
typedef struct FASIT_2112 {
   FASIT_RESPONSE response;
   FASIT_2112b body;
} FASIT_2112;
typedef struct ATI_2112 {
   FASIT_2112b embed;
} ATI_2112;

/********************************************/
/* 2113 - GPS Location                      */
/********************************************/
typedef struct FASIT_2113b { // body
   __uint8_t  fom;
   __uint16_t ilat PCKD;
   __uint32_t flat PCKD;
   __uint16_t ilon PCKD;
   __uint32_t flon PCKD;
} FASIT_2113b;
typedef struct FASIT_2113 {
   FASIT_RESPONSE response;
   FASIT_2113b body;
} FASIT_2113;
typedef struct ATI_2113 {
   FASIT_2113b embed;
   __uint8_t crc;
} ATI_2113;


/****************************************************************************************/
/* Non-FASIT commands                                                                   */
/****************************************************************************************/

/********************************************/
/* 16000 - Disable/Enable Device            */
/********************************************/
typedef struct ATI_16000 {
   __uint16_t dest PCKD;
   __uint8_t  enable;
} ATI_16000;

/********************************************/
/* 16001 - Extended Shutdown                */
/********************************************/
typedef struct ATI_16001 {
   __uint16_t dest PCKD;
} ATI_16001;

/********************************************/
/* 16002 - Retry Message                    */
/********************************************/
typedef struct ATI_16002 {
   __uint16_t dest PCKD;
} ATI_16002;

/********************************************/
/* 16003 - New Connection                   */
/********************************************/
typedef struct ATI_16003 {
   __uint32_t rand PCKD;
} ATI_16003;

/********************************************/
/* 16004 - Assign                           */
/********************************************/
typedef struct ATI_16004 {
   __uint32_t rand PCKD;
   __uint16_t id PCKD;
} ATI_16004;

/********************************************/
/* 16005 - Change Frequency Channel         */
/********************************************/
typedef struct ATI_16005 {
   __uint16_t dest PCKD;
   __uint8_t  broadcast:1 PCKD8;
   __uint8_t  channel:7 PCKD8;
} ATI_16005;

/********************************************/
/* 16006 - Close Connection                 */
/********************************************/
typedef struct ATI_16006 {
   __uint16_t dest PCKD;
} ATI_16006;

/********************************************/
/* 16007 - Heartbeat                        */
/********************************************/
typedef struct ATI_16007 {
   __uint8_t sequence PCKD8;
} ATI_16007;

/********************************************/
/* 16008 - Resubscribe                      */
/********************************************/
typedef struct ATI_16008 {
   __uint8_t sequence PCKD8;
} ATI_16008;

#endif
