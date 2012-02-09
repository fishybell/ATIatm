




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
#define LBC_STATUS		0
#define LBC_EXPOSE		1
#define LBC_MOVE		2
#define LBC_CONFIGURE_HIT	3
#define LBC_GROUP_CONTROL	4
#define LBC_AUDIO_CONTROL	5
#define LBC_POWER_CONTROL	6

#define LBC_QEXPOSE		16
#define LBC_QCONCEAL		17

#define LBC_DEVICE_REG		29
#define LBC_REQUEST_NEW		30
#define LBC_DEVICE_ADDR		31

// used to create a header
#define LB_HEADER(LBADDR,LBCMD) (((LBADDR)&0x7FF)<<5|((LBCMD)&0x1F))

#define LB_ADDR(LBHEADER) ((LBHEADER>>5)&0x7FF)

/********************************************/
/* Low Bandwidth Message Header             */
/********************************************/
typedef struct LB_packet_tag {
    uint16 header;
    uint8 payload[34] ;	// has room for max payload and the CRC byte
    int length;	// total length - not sent/ not part of the actual packet
} LB_packet_t;

// LBC_DEVICE_REG packet
//
// since this packet happens so seldom I see no good reason to try and
// bit pack smaller than this
typedef struct LB_device_reg_t {
    uint16 header;
    uint32 dev_type:8;
    uint32 devid:24;	// last 3 byte of the MAC address
    uint32 temp_addr:11;
    uint32 pad:21;
    uint8 crc;
    int length;
} LB_device_reg_t;

// LBC_DEVICE_ADDR packet
//
// this packet assigns the Slave at the responding address (temp or
// not)  a new address.   it is two bytes long and can range from 1-1700

typedef struct LB_device_addr_t {
    uint16 header;
    uint16 new_addr;
//    uint16 pad:5;
    uint8 crc;
    int length;
} LB_device_addr_t;

// LBC_EXPOSE
//    we still have 4 more bits
typedef struct LB_expose {
    uint16 header;    
    uint16 expose:1;
    uint16 hitmode:1;
    uint16 tokill:4;
    uint16 react:3;
    uint16 mfs:2;
    uint16 thermal:1;
    uint8 crc;
    int length;
} LB_expose_t;

// LBC_MOVE
//    we still have 4 more bits
typedef struct LB_move {
    uint16 header;
    uint16 direction:1;
    uint16 speed:11;
    uint8 crc;
    int length;
} LB_move_t;

// LBC_CONFIGURE
//    we have 2 too many or 6 short
typedef struct LB_configure {
    uint16 header;
    uint16 hitmode:1;
    uint16 tokill:4;
    uint16 react:3;
    uint16 hitcountset:2;
    uint16 sensitivity:4;
    uint16 timehits:4;    
    uint8 crc;
    int length;
} LB_configure_t;

// LBC_AUDIO_CONTROL
//    we have 5 short
typedef struct LB_audio_control {
    uint16 header;
    uint16 function:2;
    uint16 track:8;
    uint16 volume:7;
    uint16 playmode:2;
    uint8 crc;
    int length;
} LB_audio_control_t;

// LBC_PYRO_FIRE
//    we have 5 short
typedef struct LB_pyro_fire {
    uint16 header;
    uint8 zone:2;
    uint8 crc;
    int length;
} LB_pyro_fire_t;


void LB_CRC_add(LB_packet_t *LB,int len);

