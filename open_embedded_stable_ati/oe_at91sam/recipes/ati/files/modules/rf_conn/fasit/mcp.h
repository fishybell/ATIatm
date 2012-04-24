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

#include "fasit_c.h"

// because the Minion ID's match the LB addressing
#define MAX_NUM_Minions 2048
#define BufSize 1024

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

typedef struct state_exp_item { /* has both expose state and conceal state in single item */
   uint8         data;     // current exposure value (0/45/90)
   uint8         newdata;  // destination exposure value (0/90)
   uint16        event;
   uint16        exp_flags;
   uint16        con_flags;
   uint16        old_exp_flags;
   uint16        old_con_flags;
   uint16        exp_timer;
   uint16        con_timer;
   uint16        old_exp_timer;
   uint16        old_con_timer;
   struct timespec elapsed_time;
} state_exp_item_t ;

typedef struct state_rf_timeout { /* has both slow state and fast state in single item */
   uint8         data; // ignored
   uint8         newdata; // ignored
   uint16        fast_flags;
   uint16        slow_flags;
   uint16        old_fast_flags;
   uint16        old_slow_flags;
   uint16        fast_timer;
   uint16        slow_timer;
   uint16        old_fast_timer;
   uint16        old_slow_timer;
   uint16        slow_missed; // number of missed responses
   uint16        fast_missed; // number of missed responses
   struct timespec elapsed_time;
} state_rf_timeout_t ;

typedef struct event_timeout {
   uint8         data;
   uint8         newdata;
   uint16        flags;
   uint16        old_flags;
   uint16        timer;
   uint16        old_timer;
   uint16        missed; // number of missed responses
} event_timeout_t ;

typedef struct state_u8_item {
   uint8         data;
   uint8         newdata;
   uint16        _pad1;
   uint16        flags;
   uint16        old_flags;
   uint16        timer;
   uint16        old_timer;
} state_u8_item_t;

typedef struct state_s16_item {
   int16 data;
   int16 newdata;
   uint16 flags;
   uint16 old_flags;
   uint16 timer;
   uint16 old_timer;
} state_s16_item_t;

typedef struct state_u16_item {
   uint16 data;
   uint16 newdata;
   uint16 flags;
   uint16 old_flags;
   uint16 timer;
   uint16 old_timer;
} state_u16_item_t;

typedef struct state_float_item {
   float data;
   float newdata;
   uint16 flags;
   uint16 old_flags;
   uint16 timer;
   uint16 old_timer;
} state_float_item_t;


typedef struct minion_state {
   uint32                       cap;    // actual capability bitfield - u32 to keep alignment    
   uint8                        dev_type;   // FASIT device type
   uint16                       state_timer;    // when we next need to process the state
   uint16                       padding0;       //   extra space for now
   state_u16_item_t             fault;  // maybe not really an item
   //  miles=1, NES=2, gps=4, the rest are reserved for now - but should maybe include movers and stuff 
   state_rf_timeout_t           rf_t;   // rf timeout timer
   state_u8_item_t              status; 
   state_exp_item_t             exp;            // exposure state has more stuff
   event_timeout_t              event;          // used for timer events
   state_u8_item_t              asp;            //  FUTURE FASIT aspect of target
   state_u16_item_t             dir;            //  FUTURE FASIT 0-359 degree angle of target (dir??)
   state_u8_item_t              move;           //  movement direction  0=stopped, 1=forward (away from home), 2=reverse (to home)
   state_float_item_t           speed;          //  speed in 0 to 20 MPH     at some level 20.47 means emergency stop
   state_u16_item_t             position;       // MIT/MAT rail position  in meters from home
   //                                   hit configuration (aka sensor)
   state_u8_item_t              on;             // 4 states of on
   state_u16_item_t             hit;
   state_u8_item_t              react;
   state_u16_item_t             tokill;
   state_u16_item_t             sens;
   state_u8_item_t              mode;
   state_u16_item_t             burst;
   // MISC

   state_u8_item_t              type;
   state_u8_item_t              hit_config;
   state_u16_item_t             blanking;
   // MILES
   state_u8_item_t              miles_code;
   state_u8_item_t              miles_ammo;
   state_u8_item_t              miles_player;
   state_u8_item_t              miles_delay;
   // MFS
   state_u8_item_t              mfs_on;
   state_u8_item_t              mfs_mode;
   state_u8_item_t              mfs_idelay;
   state_u8_item_t              mfs_rdelay;
   // MGS
   state_u8_item_t              mgs_on;
   // PHI
   state_u8_item_t              phi_on;

} minion_state_t;

typedef struct slave_state {
   uint32               cap;    // actual capability bitfield - u32 to keep alignment    
   uint8                dev_type;   // FASIT device type
   uint16               state_timer;    // when we next need to process the state
   uint16               padding0;       //   extra space for now
   uint8                status; 
   uint8                exp;            // exposure state
   uint8                event;          // used for timer events
   uint8                asp;            //  FUTURE FASIT aspect of target
   uint8                dir;            //  FUTURE FASIT 0-359 degree angle of target (dir??)
   uint16               position;       // MIT/MAT rail position
   uint8                move;           //  MIT/MAT movement direction  0=stopped, 1=forward (away from home), 2=reverse (to home)
   uint16               speed;          //  speed in 0 to 20 MPH   integer (11 bits) 0-2047 it is the floating speed *100     2047 means emergency stop
   uint32               start_time;     // might not be used anymore
   //           hit configuration (aka sensor)
   uint8                on;
   uint16               hit;
   uint8                react;
   uint16               tokill;
   uint16               sens;
   uint8                mode;
   uint16               burst;
   uint16               timehits;
   // MISC
//   uint8                pos;
   uint8                type;
   uint8                hit_config;
   uint16               blanking;
   // MILES
   uint8                miles_code;
   uint8                miles_ammo;
   uint8                miles_player;
   uint8                miles_delay;
   // MFS
   uint8                mfs_on;
   uint8                mfs_mode;
   uint8                mfs_idelay;
   uint8                mfs_rdelay;
   // MGS
   uint8                mgs_on;
   // PHI
   uint8                phi_on;

} slave_state_t;

#if 0 /* start of old state timer code */
#define F_exp_ok        0
#define F_exp_expose_A  1
#define F_exp_expose_B  2
#define F_exp_expose_C  3
#define F_exp_conceal_A 4
#define F_exp_conceal_B 5
#define F_exp_conceal_C 6
#define F_exp_expose_D  7

#define F_up2date               0       // RF and internal state match
#define F_tell_RF               0x100   // RF needs update 
#define F_told_RF               0x200   // RF updated, waiting for ack 
#define F_tell_RCC              0x400   // internal state right, FASIT needs update
#define F_told_RCC              0x800   // We told FASIT our internal state, waiting on RF 
#define F_needs_report          0x010   // there needs to be an event report
#define F_waiting_for_report    0x020   // there needs to be an event report

#define F_rf_t_waiting_short 1 /* the minion is currently waiting for a response from a TM */
#define F_rf_t_waiting_long 2 /* the minion hasn't talked to the TM in a long time */
#endif /* end of old state timer code */

enum {
   F_exp_none = 0, /* no state, doing nothing */
   F_exp_start_transition, /* starts transition, goes to end transition */
   F_exp_end_transition, /* ends transition */
};

enum {
   F_con_none = 0, /* no state, doing nothing */
   F_con_start_transition, /* starts transition, goes to end transition */
   F_con_end_transition, /* ends transition */
};

enum {
   F_move_none = 0, /* no state, doing nothing */
   F_move_start_movement, /* starts movement, goes to start movement */
   F_move_end_movement, /* ends movement */
};

enum {
   F_fast_none = 0, /* no state, doing nothing */
   F_fast_start, /* starts fast lookup, goes to fast start */
   F_fast_once, /* starts fast lookup, goes to fast end */
   F_fast_end, /* ends fast lookup, goes to slow start */
};

enum {
   F_slow_none = 0, /* no state, doing nothing */
   F_slow_start, /* starts slow lookup, goes to slow start */
};

enum {
   F_event_none = 0, /* no state, doing nothing */
   F_event_start, /* starts event lookup, goes to event start */
   F_event_end, /* ends event lookup */
};


uint64 htonll( uint64 id);
int open_port(char *sport, int blocking);
void timestamp(struct timespec *elapsed_time, struct timespec *istart_time, struct timespec *time_diff);

/* create thread argument struct for thr_func() */
typedef struct _thread_data_t {
   /* don't mess with the order of mcp and minion */
   int mcp_sock;        // socket to mcp
   int minion;          // fd to minion
   /*  okay for changes again */
   int status;
   int PID;
   int mID;             // minion ID which matches the slave registration
   int rcc_sock;        // socket to RCC
   int seq;             // sequence number for the next packet this minion sends as a fasit message
   int RF_addr; // current RF address 
   uint32 devid;        // mac address of this minion which we got back from the RF
   minion_state_t S;    // the whol state of this minion
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
#define S_open   1
#define S_busy   2

/*   create the structure type to use to store what the mcp knows about our minions */
typedef struct minion {
   pthread_t thread;
   thread_data_t thr_data;
   FILE pipe;
} minion_t;

void *minion_thread(thread_data_t *);

/* create the structure type to use for minion state timing */
typedef struct minion_time {
   struct timespec elapsed_time, delta_time;
   struct timespec istart_time;
   struct timeval timeout;
} minion_time_t;

/* create the structure type to use for minion state timing */
typedef struct minion_bufs {
   FASIT_header *header;
   char buf[BufSize];
   char hbuf[100];
} minion_bufs_t;

void minion_state(thread_data_t *minion, minion_time_t *mt, minion_bufs_t *mb);


//  colors for the DCMSG  
#include "colors.h"

#define C_DEBUG 1

#define DDCMSG(DBG,SC, FMT, ...) { if (((DBG)&verbose)==(DBG)) { fprintf(stdout, "\x1B[3%d;%dm[%03x] " FMT "\x1B[30;0m\n",SC&7,(SC>>3)&1,(DBG), ##__VA_ARGS__ ); fflush(stdout);}}
#define DCMSG(SC, FMT, ...) { if (C_DEBUG) { fprintf(stdout, "\x1B[3%d;%dm      " FMT "\x1B[30;0m\n",SC&7,(SC>>3)&1, ##__VA_ARGS__ ); fflush(stdout);}}
#define DCCMSG(SC, EC, FMT, ...) {if (C_DEBUG){ fprintf(stdout, "\x1B[3%d;%dm      " FMT "\x1B[3%d;%dm\n",SC&7,(SC>>3)&1, ##__VA_ARGS__ ,EC&7,(EC>>3)&1); fflush(stdout);}}
#define DCOLOR(SC) { if (C_DEBUG){ fprintf(stdout, "\x1B[3%d;%dm",SC&7,(SC>>3)&1); fflush(stdout);}}

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

typedef enum fasit_target_type {
   Type_Default,
   Type_SIT,
   Type_MIT,
   Type_SAT,
   Type_HSAT,
   Type_MAT,
   Type_BES,
   Type_SES = 200,
} target_type_t;

typedef enum rf_target_type {
   RF_Type_SIT_W_MFS,
   RF_Type_SIT,
   RF_Type_SAT,
   RF_Type_HSAT,
   RF_Type_SES,
   RF_Type_BES,
   RF_Type_MIT,
   RF_Type_MAT,
   RF_Type_Unknown,
} rf_target_type_t;

/* stop a state timer, allowing it to be resumed at the same stop later */
#define stopTimer(S, T, F) { \
   S.old_ ## T = S.T; \
   S.old_ ## F = S.F; \
   S.T = 0; \
   S.F = 0; \
}
/* resume a stopped timer */
#define resumeTimer(S, T, F) { \
   S.T = S.old_ ## T; \
   S.F = S.old_ ## F; \
   S.old_ ## T = 0; \
   S.old_ ## F = 0; \
}
/* set a state timers flags and timeout value (in tenths of seconds) */
#define setTimerTo(S, T, F, T_val, F_val) { \
   S.T = T_val; \
   S.F = F_val; \
}

// common timer values
#define FAST_SOON_TIME        3   /* 3/10 second */
#define FAST_TIME             30  /* 3 seconds */
#define SLOW_SOON_TIME        3   /* 3/10 second */
#define SLOW_TIME             200 /* 20 seconds */
#define SLOW_RESPONSE_TIME    30  /* 3 seconds */
#define EVENT_START_TIME      5   /* 1/2 second */
#define EVENT_RESPONSE_TIME   30  /* 3 seconds */
#define TRANSITION_START_TIME 1   /* 1/10 second */
#define TRANSITION_TIME       5   /* 1/2 second */

// other state constants
#define FAST_TIME_MAX_MISS 3 /* maximum value of the "missed message" counter */
#define SLOW_TIME_MAX_MISS 1 /* maximum value of the "missed message" counter */

// common complex timer starts
#define START_EXPOSE_TIMER(S) { \
   /* start fast, stop slow */ \
   setTimerTo(S.rf_t, fast_timer, fast_flags, FAST_SOON_TIME, F_fast_start); \
   stopTimer(S.rf_t, slow_timer, slow_flags); \
   /* start transition */ \
   setTimerTo(S.exp, exp_timer, exp_flags, TRANSITION_START_TIME, F_exp_start_transition); \
}
#define START_CONCEAL_TIMER(S) { \
   /* stop fast, start slow soon */ \
   stopTimer(S.rf_t, fast_timer, fast_flags); \
   setTimerTo(S.rf_t, slow_timer, slow_flags, SLOW_SOON_TIME, F_slow_start); \
   /* start transition */ \
   setTimerTo(S.exp, con_timer, con_flags, TRANSITION_START_TIME, F_con_start_transition); \
   /* start event request */ \
   setTimerTo(S.event, timer, flags, EVENT_START_TIME, F_event_start); \
}
#define START_MOVE_TIMER(S) { \
   /* start fast, stop slow */ \
   setTimerTo(S.rf_t, fast_timer, fast_flags, FAST_SOON_TIME, F_fast_start); \
   stopTimer(S.rf_t, slow_timer, slow_flags); \
   /* start move simulation */ \
   setTimerTo(S.move, timer, flags, TRANSITION_START_TIME, F_move_start_movement); \
}
#define START_STANDARD_LOOKUP(S) {\
   /* start fast once, if needed */ \
   if (S.rf_t.slow_flags != F_slow_none) { \
      stopTimer(S.rf_t, slow_timer, slow_flags); /* will be resumed later */ \
      setTimerTo(S.rf_t, fast_timer, fast_flags, FAST_SOON_TIME, F_fast_once); \
   } \
}


void sendStatus2102(int force, FASIT_header *hdr,thread_data_t *minion);

#endif
