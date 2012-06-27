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

#define min(a, b) ({ \
    typeof(a) _a = (a); \
    typeof(b) _b = (b); \
    _a > _b ? _b : _a; \
})
#define max(a, b) ({ \
    typeof(a) _a = (a); \
    typeof(b) _b = (b); \
    _a > _b ? _a : _b; \
})

//   used to get a bit position mask
#define BV(BIT) (1<<(BIT))

// so I can print fixed point a little easier
#define DOTD(T) (T)/1000,(T)%1000

#if 0 /* old queue stuff */
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
#define Queue_Depth(M) ((int)((M)->tail - (M)->head))

#endif /* end of queue stuff */

/* new queue stuff */
typedef struct queue_item {
   struct queue_item *prev; 
   struct queue_item *next;

   char *msg;          // message payload


   int size;           // so we don't have to recalculate
   int cmd;         // so we don't have to re-look at message
   int addr;        // so we don't have to re-look at message
   int ptype;       // so we don't have to re-look at message
   int sequence;    // the address-unique sequence number
} queue_item_t;
void newItem(queue_item_t *prev, queue_item_t *next, char *msg); // creates a new, linked item
void enQueue(queue_item_t *qi, char *msg, int sequence); // queue a new RF message on the back of an existing queue
void reQueue(queue_item_t *into, queue_item_t *from, queue_item_t *until); // put the queue between "from" and "until" into the front of the "into" queue (from, until are linked at end, into item remains head of queue)
void removeItem(queue_item_t *qi, char *buf, int *bsize); // frees memory, unlinks, fills buf with a "removed" response message (bsize should come filled in with the size of buf, and after return contains size of data in buf)
void sentItem(queue_item_t *qi, char *buf, int *bsize); // same as removeItem(...), but response message is "sent" (bsize should come filled in with the size of buf, and after return contains size of data in buf)
// future -- collateCmd(...) -- takes all messages of the given command with the same (except addr) data and creates a single message in the queue, removing all others as necessary (no "removed" messages sent, as the original message is still in queue, just collated
int queueLength(queue_item_t *qi); // finds the total length of message data in the queue -- future -- to see if we should try collating or not
int queueSize(queue_item_t *qi); // finds the total number items in the queue
int queuePtype(queue_item_t *qi); // peeks at queue and returns message type
void clearQueue(queue_item_t *qi); // assuming qi is the head of the queue, this deallocates the memory and unlinks the whole queue, leaving the head unlinked but otherwise intact (no "removed" or "sent" messages are created)
int queueBurst(queue_item_t *Rx, queue_item_t *Tx, char *buf, int *bsize, int *remaining_time, int *slottime, int *inittime); // create a burst message to be sent from the Rx queue. all items put in the burst are put in the Tx queue for manual removal and sending of "sent" messages after actual send. if send is unsuccessful, items can be requeued to Rx by linking Tx's tail to Rx's head, then Rx's head becomes Tx's head. (bsize should come filled in with the size of buf, and after return contains size of data in buf) (slottime and inittime may change mid-burst, but it's unlikely as the first message of the first burst should set them, and they will then remain set the same forever unless mcp gives new values) -- return value is 0 for non-repeating, 1-15 if repeating (number is sequence number)
queue_item_t *queueTail(queue_item_t* qi); // find the tail of a queue

int Ptype(char *buf); // packet type (0: illegal, 1: request devs, 2: request stats, 3: normal, 4: control msg, 5: quick group, 6: repeating cmd)

void print_verbosity_bits(void);


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

//  the low-bandwidth command ID's
#define LBC_ILLEGAL                     0 /* illegal command will cause minion to be disconnected by mcp */
#define LBC_EXPOSE                      1
#define LBC_MOVE                        2
#define LBC_CONFIGURE_HIT               3
#define LBC_GROUP_CONTROL               4
#define LBC_AUDIO_CONTROL               5
#define LBC_POWER_CONTROL               6
#define LBC_PYRO_FIRE                   7
#define LBC_ACCESSORY                   8
#define LBC_HIT_BLANKING                9

#define LBC_STATUS_RESP                 10

#define LBC_RESET                       12

#define LBC_QUICK_GROUP                 15 /* automatically created group for repeat messages to multiple targets */
#define LBC_QEXPOSE                     16
#define LBC_QCONCEAL                    17
#define LBC_STATUS_REQ                  18
#define LBC_QUICK_GROUP_BIG             19 /* automatically created group for repeat messages to multiple targets */


#define LBC_EVENT_REPORT                20
#define LBC_REPORT_ACK                  21

#define LBC_BURST                   25

/* low-bandwidth control messages from minion/mcp to/from RFmaster, never sent over to air */
#define LB_CONTROL_QUEUE                26 /* minion/mcp to RFmaster telling it to queue next message for sending */
#define LB_CONTROL_SENT                 27 /* RFmaster to minion/mcp telling it that the message was sent */
#define LB_CONTROL_REMOVED              28 /* RFmaster to minion/mcp telling it that the message was not sent */
/* end of control messages */

#define LBC_DEVICE_REG                  29
#define LBC_REQUEST_NEW                 30
#define LBC_ASSIGN_ADDR                 31

/********************************************/
/* Low Bandwidth Message Header             */
/********************************************/
typedef struct LB_packet_tag {
   // 26 * 16 bits = 13 longs
   uint16 cmd:5 __attribute__ ((packed));
   uint16 addr:11 __attribute__ ((packed)); // source or destination address
   uint16 payload[24] __attribute__ ((packed)); // has room for some max payload (48 bytes now) and the CRC byte    
} __attribute__ ((packed))  LB_packet_t;


//                                                  LBC_STATUS_REQ packet
//   LBC_STATUS_REQ packet
typedef struct LB_status_req_t {
   // 1 * 32 bits = 1 long - padding = 3 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_status_req_t;

// LBC_STATUS_RESP packet
//
typedef struct LB_status_resp_t {
   // 3 * 32 bits = 3 long - padding = 11 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // source address (always to basestation)
   uint32 speed:11 __attribute__ ((packed)); // 100 * speed in mph
   uint32 move:1 __attribute__ ((packed)); // 0 = towards home, 1 = away from home
   uint32 did_exp_cmd:1 __attribute__ ((packed)); // 0 = didn't change exposure between last command and now, 1 = did
   uint32 react:3 __attribute__ ((packed));

   int32  location:10 __attribute__ ((packed)); // meters from home
   uint32 expose:1 __attribute__ ((packed)); // current state of exposure, 0 = concealed, 1 = exposed
   uint32 hitmode:1 __attribute__ ((packed));
   uint32 tokill:4 __attribute__ ((packed));
   uint32 sensitivity:4 __attribute__ ((packed));
   uint32 timehits:4 __attribute__ ((packed));
   uint32 fault:8 __attribute__ ((packed));

   uint32 event:13 __attribute__ ((packed));
   uint32 pad:3 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));
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
   uint32 move:1 __attribute__ ((packed)); // 0 = towards home, 1 = away from home
   uint32 pad2:1 __attribute__ ((packed));
   uint32 react:3 __attribute__ ((packed));
   int32  location:10 __attribute__ ((packed)); // meters from home
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
   // 2 * 32 bits = 1 long - padding = 7 bytes
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

typedef struct LB_quick_group_chunk {
   uint32 addr1:11 __attribute__ ((packed));
   uint32 addr2:11 __attribute__ ((packed));
   uint32 addr3_1:10 __attribute__ ((packed));

   uint32 addr3_2:1 __attribute__ ((packed));
   uint32 addr4:11 __attribute__ ((packed));
   uint32 addr5:11 __attribute__ ((packed));
   uint32 addr6_1:9 __attribute__ ((packed));

   uint32 addr6_2:2 __attribute__ ((packed));
   uint32 addr7:11 __attribute__ ((packed));
   uint32 addr8:11 __attribute__ ((packed));
   uint32 addr9_1:8 __attribute__ ((packed));

   uint32 addr9_2:3 __attribute__ ((packed));
   uint32 addr10:11 __attribute__ ((packed));
   uint32 addr11:11 __attribute__ ((packed));
   uint32 addr12_1:7 __attribute__ ((packed));

   uint32 addr12_2:4 __attribute__ ((packed));
   uint32 addr13:11 __attribute__ ((packed));
   uint32 addr14:11 __attribute__ ((packed));
   uint32 number:4 __attribute__ ((packed)); // number of addresses in this chunk used (max 14)
   uint32 padding:2 __attribute__ ((packed));
} __attribute__ ((packed)) LB_quick_group_chunk_t;

//                                                LBC_QUICK_GROUP
// LBC_QUICK_GROUP
//    
typedef struct LB_quick_group {
   // 65 byte message
   // 4 bytes...
   uint32 cmd:5 __attribute__ ((packed));
   uint32 event:13 __attribute__ ((packed));      // rolling event sequence number (ignored)
   uint32 temp_addr:11 __attribute__ ((packed));  // address to use -- this burst only -- as a group for these targets
   uint32 number:3 __attribute__ ((packed));      // number of groups used given below (max 3 for non-big version)

   // ...plus 3 * 20 bytes...
   LB_quick_group_chunk_t addresses[3];                     // the addresses to use this group

   // ...plus 1 byte
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:24 __attribute__ ((packed));

} __attribute__ ((packed))  LB_quick_group_t;

//                                                LBC_QUICK_GROUP_BIG
// LBC_QUICK_GROUP_BIG
//    
typedef struct LB_quick_group_big {
   // 165 byte message
   // 4 bytes...
   uint32 cmd:5 __attribute__ ((packed));
   uint32 event:13 __attribute__ ((packed));      // rolling event sequence number (ignored)
   uint32 temp_addr:11 __attribute__ ((packed));  // address to use -- this burst only -- as a group for these targets
   uint32 number:3 __attribute__ ((packed));      // number of groups used given below

   // ...plus 8 * 20 bytes...
   LB_quick_group_chunk_t addresses[8];                     // the addresses to use this group

   // ...plus 1 byte
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:24 __attribute__ ((packed));

} __attribute__ ((packed))  LB_quick_group_big_t;

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
   // 2 * 32 bits = 2 long - padding = 6 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 pad:2 __attribute__ ((packed));
   uint32 move:3 __attribute__ ((packed));       // 0=stop, 1=Move a direction, 2=Move other direction, 3=continuous, 4 = dock, 5 = go home
   uint32 speed:11 __attribute__ ((packed));

   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:24 __attribute__ ((packed));
} __attribute__ ((packed))  LB_move_t;

// LBC_CONFIGURE_HIT
typedef struct LB_configure {
   // 2 * 32 bits = 2 long - padding = 6 bytes
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
typedef struct LB_group_control {
   // 2 * 32 bits = 2 long - padding = 5 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 gcmd:2 __attribute__ ((packed));
   uint32 gaddr:11 __attribute__ ((packed));
   uint32 pad:3 __attribute__ ((packed));

   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:24 __attribute__ ((packed));
} __attribute__ ((packed))  LB_group_control_t;

// LBC_AUDIO_CONTROL
typedef struct LB_audio_control {
   // 2 * 32 bits = 2 long - padding = 6 bytes
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
typedef struct LB_power_control {
   // 1 * 32 bits = 1 long - padding = 4 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 pcmd:2 __attribute__ ((packed));
   uint32 pad:6 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_power_control_t;

// LBC_PYRO_FIRE
typedef struct LB_pyro_fire {
   // 1 * 32 bits = 1 long - padding = 4 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 zone:2 __attribute__ ((packed));
   uint32 pad:6 __attribute__ ((packed));
   uint32 crc:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_pyro_fire_t;

// LBC_ACCESSORY
typedef struct LB_accessory {
   // 2 * 32 bits = 2 long - padding = 6 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 on:1 __attribute__ ((packed));
   uint32 type:7 __attribute__ ((packed)); // 0=mfs,1=phi,2=mgl,3=msdh,4=
   uint32 rdelay:8 __attribute__ ((packed)); // repeat delay

   uint32 idelay:8 __attribute__ ((packed)); // initial delay
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:16 __attribute__ ((packed));
} __attribute__ ((packed))  LB_accessory_t;

// LBC_HIT_BLANKING
typedef struct LB_hit_blanking {
   // 2 * 32 bits = 2 long - padding = 5 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 blanking:16 __attribute__ ((packed)); // repeat delay

   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:24 __attribute__ ((packed));
} __attribute__ ((packed))  LB_hit_blanking_t;

// LBC_BURST
//    this message is intended to let slave devices know how many messages to expect in a burst (they were parsing and responding based on the only part of a burst, then receiving the rest)
typedef struct LB_burst {
   // 1 * 32 bits = 1 long - padding = 3 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 number:7 __attribute__ ((packed)); // max of 128 items in a burst (really max of 83)
   uint32 sequence:4 __attribute__ ((packed)); // 0 if not applicable, 1-15 if a repeating burst
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_burst_t;

// LBC_RESET
//    this message tells the slave device to reset its status
typedef struct LB_reset {
   // 1 * 32 bits = 1 long - padding = 3 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_reset_t;

//                                                  LB_CONTROL_QUEUE packet
//   LB_CONTROL_QUEUE packet
typedef struct LB_control_queue {
   // 2 * 32 bits = 2 long - padding = 5 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 sequence:16 __attribute__ ((packed)); // the next command's, unique to this addr, sequence number

   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:24 __attribute__ ((packed));
} __attribute__ ((packed))  LB_control_queue_t;

//                                                  LB_CONTROL_SENT packet
//   LB_CONTROL_SENT packet
typedef struct LB_control_sent {
   // 2 * 32 bits = 2 long - padding = 5 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 sequence:16 __attribute__ ((packed)); // the next command's, unique to this addr, sequence number

   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:24 __attribute__ ((packed));
} __attribute__ ((packed))  LB_control_sent_t;

//                                                  LB_CONTROL_REMOVED packet
//   LB_CONTROL_REMOVED packet
typedef struct LB_control_removed {
   // 2 * 32 bits = 2 long - padding = 5 bytes
   uint32 cmd:5 __attribute__ ((packed));
   uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
   uint32 sequence:16 __attribute__ ((packed)); // the next command's, unique to this addr, sequence number

   uint32 crc:8 __attribute__ ((packed));
   uint32 padding:24 __attribute__ ((packed));
} __attribute__ ((packed))  LB_control_removed_t;



void set_crc8(void *buf);
uint8 crc8(void *buf);
int RF_size(int cmd);
uint32 getDevID (void);
int gather_rf(int fd, char *pos, int max);
void print_verbosity(void);
void DDpacket_internal(const char *program, uint8 *buf,int len); // print debug output for entire buffer of messages
void DDqueue_internal(const char *qn, int v, queue_item_t *qi, const char *f, int line, char *fmt, ...); // print debug output for the given queue if verbose as bit "v"

int __ptype(int cmd);

#define DDpacket(B, L) { DDpacket_internal(__PROGRAM__, B, L); } /* auto add PROGRAM to output */
#define DDqueue(V, Q, FMT, ...) { DDqueue_internal(#Q, V, Q, __FILE__, __LINE__, FMT, ##__VA_ARGS__); } /* auto add FILE/LINE to output */

int getItemsQR(void *pkt, int *addrs); // get address from quick report packet (big or small) - passed in addrs pointer should have enough space for 8*14 addresses (big) or 3*14 address (normal)
int setItemsQR(void *pkt, int *addrs, int num); // set address for quick report packet (big or small) - passed in addrs packet pointer should already be allocated and set to the correct big/normal command (will enfore maximum of 3*14 & 8*14 and return however many didn't fit; ideally 0)


#endif
