#ifndef __MCP_H__
#define __MCP_H__

#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <termios.h> /* POSIX terminal control definitions */


// because the Minion ID's match the LB addressing
#define MAX_NUM_Minions 2048

typedef unsigned char uint8;
typedef char int8;
typedef unsigned short uint16;
typedef short int16;
typedef unsigned int uint32;
typedef int int32;
typedef unsigned long long int uint64;
typedef long long int int64;

typedef struct sockpair {
    int mcp;
    int minion;
} socketpair_t;

/*   create the structure type to use to store what each minion knows
 *   about himself
 *   - all items are a multiple of 4 bytes long
 *   */

enum {
    BLANK_ON_CONCEALED,    /* blank when fully concealed (enabled most of the time) */
    ENABLE_ALWAYS,       /* enable full-time (even when concealed) */
    ENABLE_AT_POSITION,  /* enable when reach next position (don't change now) */
    DISABLE_AT_POSITION, /* disable when reach next position (don't change now) */
    BLANK_ALWAYS,           /* hit sensor disabled blank */
};

typedef struct state_exp_item {
    uint8	 data;
    uint8	 newdata;
    uint16	 event;
    uint16	 flags;
    uint16	 timer;
    struct timespec elapsed_time;
} state_exp_item_t ;

typedef struct state_u8_item {
    uint8	 data;
    uint8	 newdata;
    uint16	 _pad1;
    uint16	 flags;
    uint16	 timer;
} state_u8_item_t;

typedef struct state_s16_item {
    int16 data;
    int16 newdata;
    uint16 flags;
    uint16 timer;
} state_s16_item_t;

typedef struct state_u16_item {
    uint16 data;
    uint16 newdata;
    uint16 flags;
    uint16 timer;
} state_u16_item_t;

typedef struct state_float_item {
    float data;
    float newdata;
    uint16 flags;
    uint16 timer;
} state_float_item_t;


typedef struct minion_state {
    uint32			cap;	// actual capability bitfield - u32 to keep alignment    
    uint16			state_timer;	// when we next need to process the state
    uint16			padding0;	//   extra space for now
    state_u8_item_t		fault;	// maybe not really an item
//  miles=1, NES=2, gps=4, the rest are reserved for now - but should maybe include movers and stuff	
    state_u8_item_t		status;	
    state_exp_item_t		exp;	// exposure state has more stuff
    state_u8_item_t		event;	// used for timer events
    state_u8_item_t		asp;
    state_u16_item_t		dir;
    state_u8_item_t		move;
    state_float_item_t		speed;
    //					hit configuration (aka sensor)
    state_u8_item_t		on;
    state_u16_item_t		hit;
    state_u8_item_t		react;
    state_u16_item_t		tokill;
    state_u16_item_t		sens;
    state_u8_item_t		mode;
    state_u16_item_t		burst;
// MISC
    state_u8_item_t		pos;
    state_u8_item_t		type;
    state_u8_item_t		hit_config;
    state_u16_item_t		blanking;
// MILES
    state_u8_item_t		miles_code;
    state_u8_item_t		miles_ammo;
    state_u8_item_t		miles_player;
    state_u8_item_t		miles_delay;
// MFS
    state_u8_item_t		mfs_on;
    state_u8_item_t		mfs_mode;
    state_u8_item_t		mfs_idelay;
    state_u8_item_t		mfs_rdelay;
// MGS
    state_u8_item_t		mgs_on;
// PHI
    state_u8_item_t		phi_on;

} minion_state_t;

#define F_exp_ok	0
#define F_exp_expose_A	1
#define F_exp_expose_B	2
#define F_exp_expose_C	3
#define F_exp_conceal_A	4
#define F_exp_conceal_B	5
#define F_exp_conceal_C	6

#define F_up2date	0	// RF and internal state match
#define F_tell_RF	0x100	// RF needs update 
#define F_told_RF	0x200	// RF updated, waiting for ack 
#define F_tell_RCC	0x400	// internal state right, FASIT needs update
#define F_told_RCC	0x800	// We told FASIT our internal state, waiting on RF 
#define F_needs_report	0x010	// there needs to be an event report


uint64 htonll( uint64 id);
int open_port(char *sport, int blocking);
void timestamp(struct timespec *elapsed_time, struct timespec *istart_time, struct timespec *time_diff);

/* create thread argument struct for thr_func() */
typedef struct _thread_data_t {
    /* don't mess with the order of mcp and minion */
    int mcp_sock;	// socket to mcp
    int minion;		// fd to minion
    /*  okay for changes again */
    int status;
    int PID;
    int mID;		// minion ID which matches the slave registration
    int rcc_sock;	// socket to RCC
    int seq;		// sequence number for the next packet this minion sends as a fasit message
    int RF_addr;	// current RF address 
    uint32 devid;	// mac address of this minion which we got back from the RF
    minion_state_t S;	// the whol state of this minion
} thread_data_t;

/* create simple struct to keep track of the address pool */
typedef struct addr_t {
    uint32 devid;
    int mID;
    uint32 inuse:1;
    uint32 something_else:1;
    uint32 someother_thing:1;
    uint32 timer:24;
} addr_t;

//   possible status for thread_data status that we need to deal with
#define S_closed 0
#define S_open	 1
#define S_busy	 2

/*   create the structure type to use to store what the mcp knows about our minions */
typedef struct minion {
	pthread_t thread;
	thread_data_t thr_data;
	FILE	pipe;
} minion_t;

void *minion_thread(thread_data_t *);


//  colors for the DCMSG  
#define black	0
#define red	1
#define green	2
#define yellow	3
#define blue	4
#define magenta 5
#define cyan	6
#define gray	7

//  these are the 'bold/bright' colors
#define BLACK	8
#define RED	9
#define GREEN	10
#define YELLOW	11
#define BLUE	12
#define MAGENTA 13
#define CYAN	14
#define GRAY	15

#define C_DEBUG 1

#define DDCMSG(DBG,SC, FMT, ...) { if (((DBG)&verbose)==(DBG)) { fprintf(stdout, "\x1B[3%d;%dm[%03x] " FMT "\x1B[30;0m\n",SC&7,(SC>>3)&1,(DBG), ##__VA_ARGS__ ); fflush(stdout);}}
#define DCMSG(SC, FMT, ...) { if (C_DEBUG) { fprintf(stdout, "\x1B[3%d;%dm      " FMT "\x1B[30;0m\n",SC&7,(SC>>3)&1, ##__VA_ARGS__ ); fflush(stdout);}}
#define DCCMSG(SC, EC, FMT, ...) {if (C_DEBUG){ fprintf(stdout, "\x1B[3%d;%dm      " FMT "\x1B[3%d;%dm\n",SC&7,(SC>>3)&1, ##__VA_ARGS__ ,EC&7,(EC>>3)&1); fflush(stdout);}}
#define DCOLOR(SC) { if (C_DEBUG){ fprintf(stdout, "\x1B[3%d;%dm      ",SC&7,(SC>>3)&1); fflush(stdout);}}

//  here are two usage examples of DCMSG
//DCMSG(RED,"example of DCMSG macro  with arguments  enum = %d  biff=0x%x",ghdr->cmd,biff) ;
//DCMSG(blue,"example of DCMSG macro with no args") ;   
//   I always like to include the trailing ';' so my editor can indent automatically

#define CPRINT_HEXB(SC,data, size)  { if (C_DEBUG) {{ \
					fprintf(stdout, "DEBUG:\x1B[3%d;%dm     ",(SC)&7,((SC)>>3)&1); \
					char *_data = (char*)data; \
					fprintf(stdout, "          "); \
					for (int _i=0; _i<size; _i++) fprintf(stdout, "%02x.", (__uint8_t)_data[_i]); \
					fprintf(stdout, " in %s at line %i\x1B[30;0m\n", __FILE__, __LINE__); \
				    }; fflush(stdout); }}



#define DCMSG_HEXB(SC,hbuf,data, size)  { if (C_DEBUG) {{ \
					fprintf(stdout, "\x1B[3%d;%dm      %s",(SC)&7,((SC)>>3)&1,hbuf); \
					char *_data = (char*)data; \
					fprintf(stdout, "          "); \
					for (int _i=0; _i<size; _i++) fprintf(stdout, "%02x.", (__uint8_t)_data[_i]); \
					fprintf(stdout, " in %s at line %i\x1B[30;0m\n", __FILE__, __LINE__); \
					}; fflush(stdout); }}

//  this one prints the hex dump on next line, and prefixs the second line with [dbg]
#define DDCMSG2_HEXB(DBG,SC,hbuf,data, size)  { if ((verbose&(DBG))==(DBG)) {{ \
					    fprintf(stdout, "\x1B[3%d;%dm[%03x] %s\n",(SC)&7,((SC)>>3)&1,(DBG),hbuf); \
					    char *_data = (char*)data; \
					    fprintf(stdout, "[%03x]    ", (DBG)); \
					    for (int _i=0; _i<size; _i++) fprintf(stdout, "%02x.", (__uint8_t)_data[_i]); \
					    fprintf(stdout, " in %s at line %i\x1B[30;0m\n", __FILE__, __LINE__); \
					}; fflush(stdout); }}


//  this one prints the hex dump on the same line as 'hbuf'
#define DDCMSG_HEXB(DBG,SC,hbuf,data, size)  { if ((verbose&(DBG))==(DBG)) {{ \
						 fprintf(stdout, "\x1B[3%d;%dm[%03x] %s",(SC)&7,((SC)>>3)&1,(DBG),hbuf); \
						 char *_data = (char*)data; \
						 fprintf(stdout, "  "); \
						 for (int _i=0; _i<size; _i++) fprintf(stdout, "%02x.", (__uint8_t)_data[_i]); \
						 fprintf(stdout, " in %s at line %i\x1B[30;0m\n", __FILE__, __LINE__); \
					     }; fflush(stdout); }}





/*
// state items
enum {
    exp,
    asp,
    dir,
    move,
    speed,
    on,
    hit,
    react,
    tokill,
    sens,
    mode,
    burst,
    fault,
    pos,
    type,
    hit_config,
    blanking,
    miles_code,
    miles_ammo,
    miles_player,
    miles_delay,
    mfs_on,
    mfs_mode,
    mfs_idelay,
    mfs_rdelay,
    mgs_on,
    phi_on,
    number_of_states,
};
*/


#endif
