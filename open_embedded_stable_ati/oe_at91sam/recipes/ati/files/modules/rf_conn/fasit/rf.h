#ifndef _RF_H_
#define _RF_H_

//  these are the verbosity bits.   fix the print_verbosity in rf.c if you change any
#define D_NONE          0
#define D_PACKET        1
#define D_RF            2
#define D_CRC           4
#define D_POLL          8
#define D_TIME          0x10
#define D_VERY          0x20
#define D_NEW           0x40
#define D_MEGA          0x80
#define D_MINION        0x100
#define D_MSTATE        0x200
#define D_QUEUE         0x400
#define D_PARSE         0x800
#define D_POINTER       0x1000
#define D_T_SLOT        0x2000

#include "mcp.h"

#define min(a, b) ( a > b ? b : a )
#define max(a, b) ( a > b ? a : b )

//   used to get a bit position mask
#define BV(BIT) (1<<(BIT))

// so I can print fixed point a little easier
#define DOTD(T) (T)/1000,(T)%1000

typedef struct queue_tag {
   char *tail;          //  end of queue - insertion point
   char *head;          // start of queue - we remove fifo usually
   int size;            // how big the buffer is
   char buf[8]; // size here is a dummy
}  queue_t ;

queue_t *queue_init( int size);
void DeQueue(queue_t *M,int count);
void ClearQueue(queue_t *M);
void ReQueue(queue_t *Mdst,queue_t *Msrc,int count);
void EnQueue(queue_t *M, void *ptr,int count); // queue in back
void QueuePush(queue_t *M, void *ptr,int count); // "queue" in front

int QueuePtype(queue_t *M);
int Ptype(char *buf);

#define Queue_Depth(M) ((int)((M)->tail - (M)->head))

void print_verbosity_bits(void);

//}


// definitions of the low-bandwith RF packets
// they are bit-packed and between 3 and whatever (up to 35) bytes long


// Message envelope
//
//  16 bit header:
//  11 bit destination addr | 5 bit command 
//
//  up to 32 byte max payload for now.  dependant on command
//
// 8 bit CRC always present

// target command payload
// bytes 0 to 31 are the command specific payload
//     that is probably bit-packed

//  the command ID's
#define LBC_ILLEGAL                     0 /* illegal command will cause minion to be disconnected by mcp */
#define LBC_EXPOSE                      1
#define LBC_MOVE                        2
#define LBC_CONFIGURE_HIT               3
#define LBC_GROUP_CONTROL               4
#define LBC_AUDIO_CONTROL               5
#define LBC_POWER_CONTROL               6

#define LBC_PYRO_FIRE                   7

#define LBC_STATUS_RESP                 10

#define LBC_RESET                       12

#define LBC_QEXPOSE                     16
#define LBC_QCONCEAL                    17
#define LBC_STATUS_REQ                  18
#define LBC_ILLEGAL_CANCEL              19 /* illegal command will cause minion's status requests to be cancelled on mcp and RFmaster */


#define LBC_EVENT_REPORT                20
#define LBC_REPORT_ACK                  21

#define LBC_BURST                   25

#define LBC_DEVICE_REG                  29
#define LBC_REQUEST_NEW                 30
#define LBC_ASSIGN_ADDR                 31

/********************************************/
/* Low Bandwidth Message Header             */
/********************************************/
typedef struct LB_packet_tag {
   // 26 * 16 bytes = 13 longs
   uint16 cmd:5 __attribute__ ((packed));
   uint16 addr:11 __attribute__ ((packed)); // source or destination address
   uint16 payload[24] __attribute__ ((packed)); // has room for some max payload (48 bytes now) and the CRC byte    
} __attribute__ ((packed))  LB_packet_t;


//                                                  LBC_STATUS_REQ packet
//   LBC_STATUS_REQ packet
typedef struct LB_status_req_t {
   // 1 * 32 bytes = 1 long - padding = 3 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_status_req_t;

// LBC_STATUS_RESP packet
//
typedef struct LB_status_resp_t {
   // 3 * 32 bytes = 3 long - padding = 9 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // source address (always to basestation)
   uint32 speed:11 __attribute__ ((packed)); // 100 * speed in mph
   uint32 move:2 __attribute__ ((packed)); // 0 = stop, 1 = towards home, 2 = away from home
   uint32 react:3 __attribute__ ((packed));

   uint32 location:10 __attribute__ ((packed)); // meters from home
   uint32 expose:1 __attribute__ ((packed));
   uint32 hitmode:1 __attribute__ ((packed));
   uint32 tokill:4 __attribute__ ((packed));
   uint32 sensitivity:4 __attribute__ ((packed));
   uint32 timehits:4 __attribute__ ((packed));
   uint32 fault:8 __attribute__ ((packed));

   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:24 __attribute__ ((packed));
} __attribute__ ((packed))  LB_status_resp_t;

// LBC_REQUEST_NEW packet
typedef struct LB_request_new_t {
   //  10 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 padding0:3 __attribute__ ((packed));
   uint32 low_dev:24 __attribute__ ((packed));          // lowest devID

   uint32 forget_addr:8 __attribute__ ((packed));      // bitfield of which slaves should forget their current addresses
   uint32 slottime:8 __attribute__ ((packed));          // slottime (multiply by 5ms)
   uint32 inittime:8 __attribute__ ((packed));          // initial time (multiply by 5ms)
   uint32 crc:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_request_new_t;

// LBC_DEVICE_REG packet
//
// since this packet happens so seldom I see no good reason to try and
// bit pack smaller than this
typedef struct LB_device_reg_t {
   // 3 * 32 bits = 3 long - padding = 12 bytes
   uint32 cmd:5 __attribute__ ((packed));   
   uint32 pad:3 __attribute__ ((packed));
   uint32 devid:24 __attribute__ ((packed));
   
   uint32 speed:11 __attribute__ ((packed)); // 100 * speed in mph
   uint32 move:2 __attribute__ ((packed)); // 0 = stop, 1 = towards home, 2 = away from home
   uint32 react:3 __attribute__ ((packed));
   uint32 location:10 __attribute__ ((packed)); // meters from home
   uint32 expose:1 __attribute__ ((packed));
   uint32 hitmode:1 __attribute__ ((packed));
   uint32 tokill:4 __attribute__ ((packed));

   uint32 dev_type:8 __attribute__ ((packed));
   uint32 sensitivity:4 __attribute__ ((packed));
   uint32 timehits:4 __attribute__ ((packed));
   uint32 fault:8 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_device_reg_t;

// LBC_ASSIGN_ADDR packet
//   
// this packet assigns the Slave at the responding address (temp or
// not)  a new address.   it is two bytes long and can range from 1-1700

typedef struct LB_assign_addr_t {
   // 2 * 32 bytes = 1 long - padding = 7 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 reregister:1 __attribute__ ((packed));
   uint32 padding0:2 __attribute__ ((packed));
   uint32 devid:24 __attribute__ ((packed));        

   uint32 new_addr:11 __attribute__ ((packed));
   uint32 pad:5 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed)) LB_assign_addr_t;


//                                                LBC_EXPOSE
// LBC_EXPOSE
//    we still have 4 more bits
typedef struct LB_expose {
   // 7
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 expose:1 __attribute__ ((packed)); // wait...the expose command always does expose...we now use this bit to say "yes, we're doing an expose" vs. "no, just change the 'event' number we're on, the hit sensing, etc."
   uint32 hitmode:1 __attribute__ ((packed));
   uint32 tokill:4 __attribute__ ((packed));
   uint32 react:3 __attribute__ ((packed));
   uint32 mfs:2 __attribute__ ((packed));
   uint32 thermal:1 __attribute__ ((packed));
   uint32 pad:7 __attribute__ ((packed));

   uint32 event:13 __attribute__ ((packed));
   uint32 pad2:3 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_expose_t;

//                                                LBC_QCONCEAL
// LBC_QCONCEAL
//    
typedef struct LB_qconceal {
   // 7 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed));     // destination address (always from basestation)
   uint32 event:13 __attribute__ ((packed));     // rolling event sequence number
   uint32 pad:3 __attribute__ ((packed));

   uint32 uptime:11 __attribute__ ((packed));   // time target was up, in deciseconds ( max 204.7 seconds)
   uint32 pad2:5 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_qconceal_t;

//                                                  LBC_EVENT_REPORT packet
//   LBC_EVENT_REPORT packet
typedef struct LB_event_report {
   // 7 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // source address (always to basestation)
   uint32 hits:8 __attribute__ ((packed)); // hits I know are correct (inbetween expose/conceal)
   uint32 report:8 __attribute__ ((packed)); // the report number (0-255, then wraps back to 0)

   uint32 event:13 __attribute__ ((packed));
   uint32 qualified:1 __attribute__ ((packed)); // I know hits are correct (inbetween expose/conceal) vs. unknown (target still exposed)
   uint32 pad:2 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));

} __attribute__ ((packed))  LB_event_report_t;


//                                                  LBC_REPORT_ACK packet
//   LBC_REPORT_ACK packet
typedef struct LB_report_ack {
   // 7 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 hits:8 __attribute__ ((packed)); // hits I know are correct (inbetween expose/conceal)
   uint32 report:8 __attribute__ ((packed)); // the report number (0-255, then wraps back to 0)

   uint32 event:13 __attribute__ ((packed));
   uint32 pad:3 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));

} __attribute__ ((packed))  LB_report_ack_t;

// LBC_MOVE
//    we still have 4 more bits
typedef struct LB_move {
   // 2 * 32 bytes = 2 long - padding = 6 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 pad:3 __attribute__ ((packed));
   uint32 move:2 __attribute__ ((packed));       // 0=stop, 1=Move a direction, 2=Move other direction, 3=continuous
   uint32 speed:11 __attribute__ ((packed));

   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:24 __attribute__ ((packed));
} __attribute__ ((packed))  LB_move_t;

// LBC_CONFIGURE_HIT
//    we have 2 too many or 6 short
typedef struct LB_configure {
   // 2 * 32 bytes = 2 long - padding = 6 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 hitmode:1 __attribute__ ((packed));
   uint32 tokill:4 __attribute__ ((packed));
   uint32 react:3 __attribute__ ((packed));
   uint32 sensitivity:4 __attribute__ ((packed));
   uint32 timehits:4 __attribute__ ((packed));

   uint32 hitcountset:2 __attribute__ ((packed));
   uint32 pad:6 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:16 __attribute__ ((packed));
} __attribute__ ((packed))  LB_configure_t;

// LBC_GROUP_CONTROL
//    we have 11 short
typedef struct LB_group_control {
   // 2 * 32 bytes = 2 long - padding = 5 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 gcmd:2 __attribute__ ((packed));
   uint32 gaddr:11 __attribute__ ((packed));
   uint32 pad:3 __attribute__ ((packed));

   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:24 __attribute__ ((packed));
} __attribute__ ((packed))  LB_group_control_t;

// LBC_AUDIO_CONTROL
//    we have 5 short
typedef struct LB_audio_control {
   // 2 * 32 bytes = 2 long - padding = 6 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 function:2 __attribute__ ((packed));
   uint32 volume:7 __attribute__ ((packed));
   uint32 playmode:2 __attribute__ ((packed));
   uint32 pad:5 __attribute__ ((packed));    

   uint32 track:8 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:16 __attribute__ ((packed));
} __attribute__ ((packed))  LB_audio_control_t;

// LBC_POWER_CONTROL
//    we have 6 short
typedef struct LB_power_control {
   // 1 * 32 bytes = 1 long - padding = 4 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 pcmd:2 __attribute__ ((packed));
   uint32 pad:6 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_power_control_t;

// LBC_PYRO_FIRE
//    we have 5 short
typedef struct LB_pyro_fire {
   // 1 * 32 bytes = 1 long - padding = 4 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 zone:2 __attribute__ ((packed));
   uint32 pad:6 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_pyro_fire_t;

// LBC_BURST
//    this message is intended to let slave devices know how many messages to expect in a burst (they were parsing and responding based on the only part of a burst, then receiving the rest)
typedef struct LB_burst {
   // 1 * 32 bytes = 1 long - padding = 3 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 number:7 __attribute__ ((packed)); // max of 128 items in a burst (really max of 83)
   uint32 pad:4 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_burst_t;

// LBC_RESET
//    this message tells the slave device to reset its status
typedef struct LB_reset {
   // 1 * 32 bytes = 1 long - padding = 3 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_reset_t;



void set_crc8(void *buf);
uint8 crc8(void *buf);
int RF_size(int cmd);
uint32 getDevID (void);
int gather_rf(int fd, char *pos, int max);
void print_verbosity(void);
void DDpacket(uint8 *buf,int len);



#endif
