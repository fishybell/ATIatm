




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

#define LBC_PYRO_FIRE		7

#define LBC_QEXPOSE		16
#define LBC_QCONCEAL		17

#define LBC_DEVICE_REG		29
#define LBC_REQUEST_NEW		30
#define LBC_DEVICE_ADDR		31

/********************************************/
/* Low Bandwidth Message Header             */
/********************************************/
typedef struct LB_packet_tag {
    uint16 cmd:5 __attribute__ ((packed));
    uint16 addr:11 __attribute__ ((packed));
    uint16 payload[24] __attribute__ ((packed)); // has room for max payload and the CRC byte    
} LB_packet_t;

// LBC_REQUEST_NEW packet
//
typedef struct LB_request_new_t {
    uint16 cmd:5 __attribute__ ((packed));
    uint16 addr:11 __attribute__ ((packed));
    
    uint16 crc:8 __attribute__ ((packed));
    uint16 length:8 __attribute__ ((packed));
} LB_request_new_t;

// LBC_DEVICE_REG packet
//
// since this packet happens so seldom I see no good reason to try and
// bit pack smaller than this
typedef struct LB_device_reg_t {
    uint16 cmd:5 __attribute__ ((packed));
    uint16 addr:11 __attribute__ ((packed));    

    uint32 dev_type:8 __attribute__ ((packed));
    uint32 devid:24 __attribute__ ((packed));
    
    uint16 temp_addr:11 __attribute__ ((packed));
    uint16 pad:5 __attribute__ ((packed));
    
    uint16 crc:8 __attribute__ ((packed));
    uint16 length:8 __attribute__ ((packed));
} LB_device_reg_t;

// LBC_DEVICE_ADDR packet
//
// this packet assigns the Slave at the responding address (temp or
// not)  a new address.   it is two bytes long and can range from 1-1700

typedef struct LB_device_addr_t {
    uint16 cmd:5 __attribute__ ((packed));
    uint16 addr:11 __attribute__ ((packed));

    uint16 new_addr:11 __attribute__ ((packed));
    uint16 pad:5 __attribute__ ((packed));
    
    uint16 crc:8 __attribute__ ((packed));
    uint16 length:8 __attribute__ ((packed));
} __attribute__ ((packed)) LB_device_addr_t;

// LBC_EXPOSE
//    we still have 4 more bits
typedef struct LB_expose {
    uint16 cmd:5 __attribute__ ((packed));
    uint16 addr:11 __attribute__ ((packed));

    uint16 expose:1 __attribute__ ((packed));
    uint16 hitmode:1 __attribute__ ((packed));
    uint16 tokill:4 __attribute__ ((packed));
    uint16 react:3 __attribute__ ((packed));
    uint16 mfs:2 __attribute__ ((packed));
    uint16 thermal:1 __attribute__ ((packed));
    uint16 pad:4 __attribute__ ((packed));

    uint16 crc:8 __attribute__ ((packed));
    uint16 length:8 __attribute__ ((packed));
} LB_expose_t;

// LBC_MOVE
//    we still have 4 more bits
typedef struct LB_move {
    uint16 cmd:5 __attribute__ ((packed));
    uint16 addr:11 __attribute__ ((packed));

    uint16 direction:1 __attribute__ ((packed));
    uint16 speed:11 __attribute__ ((packed));
    uint16 pad:4 __attribute__ ((packed));

    uint16 crc:8 __attribute__ ((packed));
    uint16 length:8 __attribute__ ((packed));
} LB_move_t;

// LBC_CONFIGURE_HIT
//    we have 2 too many or 6 short
typedef struct LB_configure {
    uint16 cmd:5 __attribute__ ((packed));
    uint16 addr:11 __attribute__ ((packed));

    uint16 hitmode:1 __attribute__ ((packed));
    uint16 tokill:4 __attribute__ ((packed));
    uint16 react:3 __attribute__ ((packed));
    uint16 sensitivity:4 __attribute__ ((packed));
    uint16 timehits:4 __attribute__ ((packed));

    uint16 hitcountset:2 __attribute__ ((packed));
    uint16 pad:6 __attribute__ ((packed));
    uint16 crc:8 __attribute__ ((packed));
    
    uint16 length:8 __attribute__ ((packed));
    uint16 pad2:8 __attribute__ ((packed));
} LB_configure_t;

// LBC_AUDIO_CONTROL
//    we have 5 short
typedef struct LB_audio_control {
    uint16 cmd:5 __attribute__ ((packed));
    uint16 addr:11 __attribute__ ((packed));

    uint16 function:2 __attribute__ ((packed));
    uint16 track:8 __attribute__ ((packed));
    uint16 volume:7 __attribute__ ((packed));
    uint16 playmode:2 __attribute__ ((packed));
    uint16 pad:5 __attribute__ ((packed));    
    uint16 crc:8 __attribute__ ((packed));
    
    uint16 length:8 __attribute__ ((packed));
    uint16 pad2:8 __attribute__ ((packed));
} LB_audio_control_t;

// LBC_PYRO_FIRE
//    we have 5 short
typedef struct LB_pyro_fire {
    uint16 cmd:5 __attribute__ ((packed));
    uint16 addr:11 __attribute__ ((packed));

    uint16 zone:2 __attribute__ ((packed));
    uint16 pad:6 __attribute__ ((packed));
    uint16 crc:8 __attribute__ ((packed));
    
    uint16 length:8 __attribute__ ((packed));
    uint16 pad2:8 __attribute__ ((packed));
} LB_pyro_fire_t;

void set_crc8(void *buf, uint8 length);
uint8 crc8(void *buf, uint8 length);
int RF_size(int cmd);
uint32 getDevID (void);
int gather_rf(int fd, char *pos, char *start,int max);

