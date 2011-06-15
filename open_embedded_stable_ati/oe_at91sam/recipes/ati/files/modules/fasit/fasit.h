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

   virtual int validMessage(int *start, int* end) = 0; // does the buffer contain valid data? message number on yes, 0 on no; sets start and end for where in the buffer it is

   static __uint64_t swap64(__uint64_t s); // swap the byte order to switch between network and host modes

protected :
   void addToBuffer(int rsize, const char *rbuf); // appends data to read buffer
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

// Most FASIT responses start with these
typedef struct FASIT_RESPONSE {
   __uint16_t rnum PCKD;
   __uint32_t rseq PCKD;
} FASIT_RESPONSE;

/********************************************/
/* 100 - Device Definition Request          */
/********************************************/
// FASIT command has no message body

/********************************************/
/* 2000 - Pyro Event Command                */
/********************************************/
typedef struct FASIT_2000 {
   __uint8_t  cid;
   __uint16_t zone PCKD;
} FASIT_2000;

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

/********************************************/
/* 2100 - Event Command                     */
/********************************************/
typedef struct FASIT_2100 {
   __uint8_t  cid;
   // this group is refered to as 'Exposure record'
   __uint8_t  exp;
   __int16_t  asp PCKD;
   __uint16_t dir PCKD;
   __uint8_t  move;
   float      speed PCKD;
   // past this is refered to as 'Sensor Record'
   __uint8_t  on;		// enum
   __uint16_t hit PCKD;		// hit count
   __uint8_t  react;		// reaction/after_fall, enum 
   __uint16_t tokill PCKD;	// hits to kill/fall
   __uint16_t sens PCKD;	// sensitivity
   __uint8_t  mode;			// mode
   __uint16_t burst PCKD;	// burst seperation
} FASIT_2100;

// the Command ID values
enum {
   CID_No_Event,
   CID_Reserved01,
   CID_Status_Request,
   CID_Expose_Request,
   CID_Reset_Device,
   CID_Move_Request,
   CID_Config_Hit_Sensor,
   CID_GPS_Location_Request,
   CID_Shutdown = 177,		/* not a standard FASIT value */
};

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

/********************************************/
/* 2114 - Configure MILES Shootback Command */
/********************************************/
typedef struct FASIT_2114 {
   __uint8_t  code;
   __uint8_t  ammo;
   __uint16_t player PCKD;
   __uint8_t  delay;
} FASIT_2114;

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

/********************************************/
/* 2110 - Configure Muzzle Flash Command    */
/********************************************/
typedef struct FASIT_2110 {
   __uint8_t  on;
   __uint8_t  mode;
   __uint8_t  idelay;
   __uint8_t  rdelay;
} FASIT_2110;

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


#endif
