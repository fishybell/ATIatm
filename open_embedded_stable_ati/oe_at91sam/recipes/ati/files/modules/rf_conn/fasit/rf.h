#ifndef _RF_H_
#define _RF_H_

//  these are the verbosity bits.   fix the print_verbosity in rf.c if you change any
#define D_NONE		0
#define D_PACKET	1
#define D_RF		2
#define D_CRC		4
#define D_POLL		8
#define D_TIME		0x10
#define D_VERY		0x20
#define D_MEGA		0x80

#include "mcp.h"

typedef struct queue_tag {
    char *tail;		//  end of queue - insertion point
    char *head;		// start of queue - we remove fifo usually
    int size;		// how big the buffer is
    char buf[8];	// size here is a dummy
}  queue_t ;

queue_t *queue_init( int size);
void DeQueue(queue_t *M,int count);
void ReQueue(queue_t *Mdst,queue_t *Msrc,int count);
void EnQueue(queue_t *M, void *ptr,int count);

int QueuePtype(queue_t *M);

#define Queue_Depth(M) ((M)->tail - (M)->head)


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
#define LBC_STATUS_REQ			0
#define LBC_EXPOSE			1
#define LBC_MOVE			2
#define LBC_CONFIGURE_HIT		3
#define LBC_GROUP_CONTROL		4
#define LBC_AUDIO_CONTROL		5
#define LBC_POWER_CONTROL		6

#define LBC_PYRO_FIRE			7

#define LBC_STATUS_RESP_LIFTER		8
#define LBC_STATUS_RESP_MOVER		9
#define LBC_STATUS_RESP_EXT		10
#define LBC_STATUS_NO_RESP		11

#define LBC_QEXPOSE			16
#define LBC_QCONCEAL			17

#define LBC_DEVICE_REG			29
#define LBC_REQUEST_NEW			30
#define LBC_ASSIGN_ADDR			31

/********************************************/
/* Low Bandwidth Message Header             */
/********************************************/
typedef struct LB_packet_tag {
    // 26 * 16 bytes = 13 longs
    uint16 cmd:5 __attribute__ ((packed));
    uint16 addr:11 __attribute__ ((packed)); // source or destination address
    uint16 payload[24] __attribute__ ((packed)); // has room for some max payload (48 bytes now) and the CRC byte    
} __attribute__ ((packed))  LB_packet_t;

// LBC_STATUS_REQ packet
//
typedef struct LB_status_req_t {
    // 1 * 32 bytes = 1 long - padding = 3 bytes
    uint32 cmd:5 __attribute__ ((packed));
    uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
    uint32 crc:8 __attribute__ ((packed));
    uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_status_req_t;

// LBC_STATUS_RESP_LIFTER packet
//
typedef struct LB_status_resp_lifter_t {
    // 1 * 32 bytes = 1 long - padding = 4 bytes
    uint32 cmd:5 __attribute__ ((packed));
    uint32 addr:11 __attribute__ ((packed)); // source address (always to basestation)
    uint32 hits:7 __attribute__ ((packed)); // up to 127 hits
    uint32 expose:1 __attribute__ ((packed));
    uint32 crc:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_status_resp_lifter_t;

// LBC_STATUS_RESP_MOVER packet
//
typedef struct LB_status_resp_mover_t {
    // 2 * 32 bytes = 2 long - padding = 8 bytes
    uint32 cmd:5 __attribute__ ((packed));
    uint32 addr:11 __attribute__ ((packed)); // source address (always to basestation)
    uint32 hits:7 __attribute__ ((packed)); // up to 127 hits
    uint32 expose:1 __attribute__ ((packed));
    uint32 pad:8 __attribute__ ((packed));

    uint32 speed:11 __attribute__ ((packed)); // 100 * speed in mph
    uint32 dir:2 __attribute__ ((packed)); // 0 = stop, 1 = away from home, 2 = towards home
    uint32 location:11 __attribute__ ((packed)); // meters from home
    uint32 crc:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_status_resp_mover_t;

// LBC_STATUS_RESP_EXT packet
//
typedef struct LB_status_resp_ext_t {
    // 3 * 32 bytes = 3 long - padding = 11 bytes
    uint32 cmd:5 __attribute__ ((packed));
    uint32 addr:11 __attribute__ ((packed)); // source address (always to basestation)
    uint32 hits:7 __attribute__ ((packed)); // up to 127 hits
    uint32 expose:1 __attribute__ ((packed));
    uint32 pad:8 __attribute__ ((packed));

    uint32 speed:11 __attribute__ ((packed)); // 100 * speed in mph
    uint32 dir:2 __attribute__ ((packed)); // 0 = stop, 1 = towards home, 2 = away from home
    uint32 react:3 __attribute__ ((packed));
    uint32 location:11 __attribute__ ((packed)); // meters from home
    uint32 hitmode:1 __attribute__ ((packed));
    uint32 tokill:4 __attribute__ ((packed));

    uint32 sensitivity:4 __attribute__ ((packed));
    uint32 timehits:4 __attribute__ ((packed));
    uint32 fault:8 __attribute__ ((packed));
    uint32 crc:8 __attribute__ ((packed));
    uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_status_resp_ext_t;

// LBC_STATUS_NO_RESP packet
//
typedef struct LB_status_no_resp_t {
    // 1 * 32 bytes = 1 long - padding = 3 bytes
    uint32 cmd:5 __attribute__ ((packed));
    uint32 addr:11 __attribute__ ((packed)); // source address (always to basestation)
    uint32 crc:8 __attribute__ ((packed));
    uint32 padding:8 __attribute__ ((packed));
} __attribute__ ((packed))  LB_status_no_resp_t;

// LBC_REQUEST_NEW packet
typedef struct LB_request_new_t {
//  9 bytes
    uint32 cmd:5 __attribute__ ((packed));
    uint32 reregister:1 __attribute__ ((packed));
    uint32 padding0:2 __attribute__ ((packed));
    uint32 low_dev:24 __attribute__ ((packed));		// lowest devID

    uint32 high_dev:24 __attribute__ ((packed));	// highest devID
    uint32 slottime:8 __attribute__ ((packed));		// slottime (multiply by 5ms)

    uint32 crc:8 __attribute__ ((packed));
    uint32 padding:24 __attribute__ ((packed));
} __attribute__ ((packed))  LB_request_new_t;

// LBC_DEVICE_REG packet
//
// since this packet happens so seldom I see no good reason to try and
// bit pack smaller than this
typedef struct LB_device_reg_t {
    //  6 bytes
    uint32 cmd:5 __attribute__ ((packed));
    uint32 pad:3 __attribute__ ((packed));
    uint32 devid:24 __attribute__ ((packed));
    
    uint32 dev_type:8 __attribute__ ((packed));
    uint32 crc:8 __attribute__ ((packed));
    uint32 padding:16 __attribute__ ((packed));
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

// LBC_EXPOSE
//    we still have 4 more bits
typedef struct LB_expose {
    // 2 * 32 bytes = 2 long - padding = 5 bytes
    uint32 cmd:5 __attribute__ ((packed));
    uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
    uint32 expose:1 __attribute__ ((packed));
    uint32 hitmode:1 __attribute__ ((packed));
    uint32 tokill:4 __attribute__ ((packed));
    uint32 react:3 __attribute__ ((packed));
    uint32 mfs:2 __attribute__ ((packed));
    uint32 thermal:1 __attribute__ ((packed));
    uint32 pad:4 __attribute__ ((packed));

    uint32 crc:8 __attribute__ ((packed));
    uint32 padding:24 __attribute__ ((packed));
} __attribute__ ((packed))  LB_expose_t;

// LBC_MOVE
//    we still have 4 more bits
typedef struct LB_move {
    // 2 * 32 bytes = 2 long - padding = 5 bytes
    uint32 cmd:5 __attribute__ ((packed));
    uint32 addr:11 __attribute__ ((packed)); // destination address (always from basestation)
    uint32 direction:1 __attribute__ ((packed));
    uint32 speed:11 __attribute__ ((packed));
    uint32 pad:4 __attribute__ ((packed));

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



void set_crc8(void *buf);
uint8 crc8(void *buf);
int RF_size(int cmd);
uint32 getDevID (void);
int gather_rf(int fd, char *pos, char *start,int max);
void print_verbosity(void);

#endif
