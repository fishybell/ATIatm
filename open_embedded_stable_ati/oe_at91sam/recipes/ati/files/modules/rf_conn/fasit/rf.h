


// definitions of the low-bandwith RF packets
// they are bit-packed and between 3 and 18 bytes long


// Message envelope
//
//  16 bit header:
//  11 bit addr | 4 bit length | 1 bit CRC flag 
//
// 0-15 byte paylod
//
// 8 bit CRC present if the CRC Flag bit is set


// target command payload
// bits 0-3 are the command ID
// bits 4-N are the command specific payload



//  the command ID's
#define LB_STATUS_REQUEST	0
#define LB_EXPOSE_REQUEST	1 
#define LB_MOVE_REQUEST		2
#define LB_CONFIGURE_HIT	3
#define LB_GROUP_CONTROL	4
#define LB_AUDIO_CONTROL	5
#define LB_POWER_CONTROL	6

#define LB_DEVICE_CAP		13
#define LB_REQUEST_NEW		14
#define LB_DEVICE_ADDR		15



/********************************************/
/* FASIT Message Header                     */
/********************************************/
typedef struct LB_packet_tag {
    uint16 header ;
    uint8 payload[16] ;
} LB_packet_t;

