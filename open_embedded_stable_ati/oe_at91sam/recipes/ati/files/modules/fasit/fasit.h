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
   CID_Stop = 176,			/* not a standard FASIT value */
   CID_Shutdown = 177,			/* not a standard FASIT value */
   CID_Sleep = 178,			/* not a standard FASIT value */
   CID_Wake = 179,			/* not a standard FASIT value */
   CID_Dock = 180,			/* not a standard FASIT value */
   CID_Continuous_Move_Request = 181,/* not a standard FASIT value */
   CID_Hit_Count_Reset = 182,/* not a standard FASIT value */
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
#define PD_NES (1 << 1)
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


/********************************************/
/* 13110 - Configure Moon Glow    */
/********************************************/
typedef struct FASIT_13110 {
	__uint8_t  on;
} FASIT_13110;

/********************************************/
/* 13112 - Muzzle Moon Glow Status    */
/********************************************/
typedef struct FASIT_13112b { // body
	__uint8_t  on;
} FASIT_13112b;
typedef struct FASIT_13112 {
	FASIT_RESPONSE response;
	FASIT_13112b body;
} FASIT_13112;

/********************************************/
/* 14110 - Configure Positive Hit Indicator    */
/********************************************/
typedef struct FASIT_14110 {
	__uint8_t  on;
} FASIT_14110;

/********************************************/
/* 14112 - Muzzle Positive Hit Indicator Status    */
/********************************************/
typedef struct FASIT_14112b { // body
	__uint8_t  on;
} FASIT_14112b;
typedef struct FASIT_14112 {
	FASIT_RESPONSE response;
	FASIT_14112b body;
} FASIT_14112;

/********************************************/
/* 15110 - Configure Thermals    */
/********************************************/
typedef struct FASIT_15110 {
	__uint8_t  on;  // 0 = n/a, 1 = on, 2 = off
    //__uint8_t  num;
} FASIT_15110;

/********************************************/
/* 14112 - Thermal Status    */
/********************************************/
typedef struct FASIT_15112b { // body
	__uint8_t  on;
    //__uint8_t  num;
} FASIT_15112b;
typedef struct FASIT_15112 {
	FASIT_RESPONSE response;
	FASIT_15112b body;
} FASIT_15112;

/********************************************/
/* 14200 - Configure Hit Blanking           */
/********************************************/
typedef struct FASIT_14200 {
   __uint16_t blank PCKD; // hundredths of seconds
} FASIT_14200;

/********************************************/
/* 14400 - SES Command                      */
/* NON-FASIT-COMPLIANT                      */
/* cid = command id:                        */
/*                  0  = No command         */
/*                       data = unused      */
/*                  1  = Request status     */
/*                       data = unused      */
/*                  2  = Play track         */
/*                       data = track name  */
/*                  3  = Record track       */
/*                       data = track name  */
/*                  4  = Play stream        */
/*                       data = stream uri  */
/*                  5  = Stop all playback  */
/*                       data = unused      */
/*                  6  = Encode recording   */
/*                       data = unused      */
/*                  7  = Abort recording    */
/*                       data = unused      */
/*                  8  = Copy start         */
/*                       data = track name  */
/*                  9  = Copy data chunk    */
/*                       data = data chunk  */
/*                  10 = Copy abort         */
/*                       data = unused      */
/*                  11 = Maintenance volume */
/*                       data = unused      */
/*                  12 = Testing volume     */
/*                       data = unused      */
/*                  13 = Live-fire volume   */
/*                       data = unused      */
/*                  14 = Set looping        */
/*                       data = loop count  */
/* length = length of data chunk            */
/* data = data parameter (defined per cid)  */
/********************************************/
typedef struct FASIT_14400 {
   __uint8_t  cid;
   __uint16_t length PCKD;
   __uint8_t  data[2048];
} FASIT_14400;
// the Command ID values
enum {
   SES_No_Event,
   SES_Request_Status,
   SES_Play_Track,
   SES_Record_Track,
   SES_Play_Stream,
   SES_Stop_Playback,
   SES_Encode_Recording,
   SES_Abort_Recording,
   SES_Copy_Start,
   SES_Copy_Chunk,
   SES_Copy_Abort,
   SES_Maint_Volume,
   SES_Test_Volume,
   SES_Livefire_Volume,
   SES_Loop,
};


/********************************************/
/* 14401 - SES Status                       */
/* NON-FASIT-COMPLIANT                      */
/* mode = playback volume level:            */
/*                              0 = maint   */
/*                              1 = testing */
/*                              2 = record  */
/*                              3 = live    */
/* status = playback status:                */
/*                          0 = unknown     */
/*                          1 = stopped     */
/*                          2 = playing     */
/*                          3 = play ready  */
/*                          4 = streaming   */
/*                          5 = recording   */
/*                          6 = encoding    */
/*                          7 = rec ready   */
/*                          8 = copy ready  */
/* track = knob track number (0-15)         */
/********************************************/
typedef struct FASIT_14401b { // body
   __uint8_t  mode;
   __uint8_t  status;
   __uint8_t  track;
} FASIT_14401b;
typedef struct FASIT_14401 {
   FASIT_RESPONSE response;
   FASIT_14401b body;
} FASIT_14401;

#endif
