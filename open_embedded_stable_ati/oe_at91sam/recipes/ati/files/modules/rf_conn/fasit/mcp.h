#ifndef __MCP_H__
#define __MCP_H__

//#undef CLOCK_MONOTONIC
//#define CLOCK_MONOTONIC CLOCK_REALTIME
//#undef CLOCK_MONOTONIC_RAW
//#define CLOCK_MONOTONIC_RAW CLOCK_REALTIME

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

// maximum number of events to keep track of
#define MAX_HIT_EVENTS 8192 /* definined here because it is used almost everywhere */

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

extern const char *__PROGRAM__;

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

// the steps a lifter goes through with lifting (can start on exposed or concealed, but then moves through in order)
typedef enum transition_steps {
   TS_concealed,
   TS_con_to_exp,
   TS_exposed,
   TS_exp_to_con,
   TS_too_far, /* used to tell if we've gone too far and need to start over */
} trans_step_t;

extern int steps_translation[];
extern const char *step_words[];

typedef struct state_exp_item { /* has both expose state and conceal state in single item */
   uint8         data;     // current exposure value (0/45/90)
   uint8         newdata;  // destination exposure value (0/90)
   uint8         lastdata; // last exposure value sent (0/45/90)
   int           recv_dec; // we've received a "did_exp_cmd" message
   int           data_uc_count; // count the number of times we've received&ignored unchanged data
   int           data_uc;       // the data value for checking whether the received data is unchanged or not
   uint16        event;
   uint16        last_event; // for retrying event request even if we've moved on to a different event
   uint16        exp_flags;
   uint16        con_flags;
   uint16        old_exp_flags;
   uint16        old_con_flags;
   uint16        exp_timer;
   uint16        con_timer;
   uint16        old_exp_timer;
   uint16        old_con_timer;
   int           log_start_time[MAX_HIT_EVENTS]; // time FASIT server would have logged start of exposure
   int           cmd_start_time[MAX_HIT_EVENTS]; // time we sent exposure over RF
   int           log_end_time[MAX_HIT_EVENTS];   // time FASIT server would have logged end of exposure
   int           cmd_end_time[MAX_HIT_EVENTS];   // time we sent conceal over RF (or told it to end over RF)
   trans_step_t last_step; // the last transition step we took
} state_exp_item_t;

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

typedef struct state_u8_item {
   uint8         data;
   uint8         newdata;
   uint8         lastdata;
   uint16        _pad1;
   uint16        flags;
   uint16        old_flags;
   uint16        timer;
   uint16        old_timer;
} state_u8_item_t;

typedef struct state_s16_item {
   int16 data;
   int16 newdata;
   int16 lastdata;
   uint16 flags;
   uint16 old_flags;
   uint16 timer;
   uint16 old_timer;
} state_s16_item_t;

typedef struct state_u16_item {
   uint16 data;
   uint16 newdata;
   uint16 lastdata;
   uint16 flags;
   uint16 old_flags;
   uint16 timer;
   uint16 old_timer;
} state_u16_item_t;

typedef struct state_float_item {
   float data; // what we tell SR
   float newdata; // what we're going towards
   float lastdata; // what we last told SR
   int towards_index; // what we're going towards (as an index 0-4)
   int last_index; // what we last told SR (as an index 0-4)
   float fpos; // position as a float (so we can accrue small amounts of position)
   float lastfpos; // position as a float (so we can accrue small amounts of position)
   uint16 flags;
   uint16 old_flags;
   uint16 timer;
   uint16 old_timer;
} state_speed_item_t;

typedef struct report_memory_item { /* matching reports will be ignored, items in chain will be deleted when we receive a chunk of event reports that does not include this report */
   int report;
   int event;
   int hits;
   int unreported; // keep track of how many times this one wasn't re-reported, then vacuum when it gets too big
   // each remembered report has its own state timers
   state_u8_item_t s;
   // we're a chain
   struct report_memory_item *next;
} report_memory_t;

enum {
   move_stop,
   move_left,
   move_right,
   move_continuous,
   move_dock,
   move_away,
   lift_expose,
   lift_conceal,
};
enum {
   react_fall,
   react_kill,
   react_stop,
   react_stop_and_fall,
   react_bob,
};
typedef struct state_response_item {
   int data;     // number of hits left before killed
   int newdata;  // has current reponse = 1, 0 otherwise
   int lastdata; // has last response = 1, 0 otherwise
   int mover_command; // last command sent to mover
   int lifter_command; // last command sent to lifter
   int did_exp_cmd; // only valid for current response
   // most recent status response
   int current_exp;
   int current_direction;
   int current_speed;
   int current_position;
   int current_hit;
   // 2nd most recent status response
   int last_exp;
   int last_direction;
   int last_speed;
   int last_position;
   int last_hit;
   // overall "did we ever do this" statuses
   int ever_exp;
   int ever_con;
   int ever_move_left;
   int ever_move_right;
   int ever_stop;
   int ever_hit;
   int ever_move;
   uint16 flags;
   uint16 old_flags;
   uint16 timer;
   uint16 old_timer;
} state_response_item_t;


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
   report_memory_t              *report_chain;  // used for remembered reports and their timers
   state_u8_item_t              asp;            //  FUTURE FASIT aspect of target
   state_u16_item_t             dir;            //  FUTURE FASIT 0-359 degree angle of target (dir??)
   state_u8_item_t              move;           //  movement direction  0=stopped, 1=forward (away from home), 2=reverse (to home)
   state_speed_item_t           speed;          //  speed in 0 to 20 MPH     at some level 20.47 means emergency stop
   state_s16_item_t             position;       // MIT/MAT rail position  in meters from home
   int                          last_move_time; // the time we last sent a movement status (to calculate difference on)
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

   struct sequence_tracker *tracker;
   int last_sequence_sent;

   state_response_item_t         resp;
   int ignoring_response; // 0 = not ignoring, anything else = ignoring

} minion_state_t;

// used in RFslave.c
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
   F_move_start_movement, /* starts movement, goes to continue movement */
   F_move_continue_movement, /* starts movement, goes to start movement */
   F_move_end_movement, /* ends movement */
};

enum {
   F_fast_none = 0, /* no state, doing nothing */
   F_fast_start, /* starts fast lookup, goes to fast start */
   F_fast_wait, /* pauses fast lookup, goes to fast start */
};

enum {
   F_slow_none = 0, /* no state, doing nothing */
   F_slow_start, /* starts slow lookup, goes to slow start */
   F_slow_wait, /* pauses slow lookup, goes to slow start */
};

enum {
   F_event_none = 0, /* no state, doing nothing */
   F_event_ack, /* sends event acks, then nothing else */
};

enum {
   F_resp_none = 0, /* no state, doing nothing */
   F_resp_handle,   /* does the response handling */
};


uint64 htonll( uint64 id);
int open_port(char *sport, int blocking);
//void timestamp(struct timespec *elapsed_time, struct timespec *istart_time, struct timespec *time_diff);
int ts2ms(struct timespec *ts); // convert timespec to milliseconds
void ms2ts(int ms, struct timespec *ts); // convert milliseconds to timespec
void ts_minus_ts(struct timespec *in1, struct timespec *in2, struct timespec *out); // out = in1 - in2, sub-ms precision is lost
#define DEBUG_TS(ts) ts.tv_sec, ts.tv_nsec/1000000l /* useful for doing a printf("%3i.%03i", DEBUG_TS(x)) */
#define DEBUG_MS(ms) (ms / 1000), (ms % 1000)       /* useful for doing a printf("%3i.%03i", DEBUG_MS(x)) */

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
   int rcc_connected;
   int seq;             // sequence number for the next packet this minion sends as a fasit message
   int RF_addr; // current RF address 
   uint32 devid;        // mac address of this minion which we got back from the RF
   minion_state_t S;    // the whole state of this minion
   uint16 mit_length;   // length of mit track
   uint16 mit_home;     // where on mit track home is
   uint16 mit_end;      // where on mit track end is
   uint16 mat_length;   // length of mat track
   uint16 mat_home;     // where on mat track home is
   uint16 mat_end;      // where on mat track end is
} thread_data_t;

/* create simple struct to keep track of the address pool */
typedef struct addr_t {
   uint32 devid;
   int mID;
   uint32 inuse:1 __attribute__ ((packed));
   uint32 something_else:1 __attribute__ ((packed));
   uint32 someother_thing:1 __attribute__ ((packed));
   uint32 pad:5 __attribute__ ((packed));
   uint32 timer:24 __attribute__ ((packed));
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
   struct timespec last_time; // don't use directly
} minion_time_t;

/* create the structure type to use for minion state timing */
typedef struct minion_bufs {
   FASIT_header *header;
   char buf[BufSize];
   char hbuf[100];
} minion_bufs_t;

void minion_state(thread_data_t *minion, minion_bufs_t *mb);

// command tracking to do stuff (whatever stuff really) when a packet is actually sent from the radio
typedef void (*tracker_callback)(thread_data_t*, minion_time_t*, char*, void*);
typedef struct sequence_tracker {
   char *pkt; // copy of message (up to 48 byte messages at least) that we're tracking
   tracker_callback sent_callback; // a function to call when we've received the response to this command
   tracker_callback removed_callback; // a function to call when we've received the response to this command
   void *data; // random data pointer that is passed to do_func
   int sequence; // the sequence we're tracking
   int missed; // a count-down value for how many times we can "miss" either sending or removing this sequence (to prevent stale values remaining allocated forever)

   // we're a chain
   struct sequence_tracker *next;
} sequence_tracker_t;

//  colors for the DCMSG  
#include "colors.h"

#define C_DEBUG 1
#define DEBUG_DCMSG 1
#if DEBUG_DCMSG
#define DCMSG(SC, FMT, ...) { if (C_DEBUG) { fprintf(stdout, "\x1B[3%i;%im%s      " FMT "\x1B[30;0m @ %s:%i\n",SC&7,(SC>>3)&1,__PROGRAM__, ##__VA_ARGS__, __FILE__, __LINE__); fflush(stdout);}}
#else
#define DCMSG(SC, FMT, ...) { if (C_DEBUG) { fprintf(stdout, "\x1B[3%i;%im%s      " FMT "\x1B[30;0m\n",SC&7,(SC>>3)&1,__PROGRAM__, ##__VA_ARGS__ ); fflush(stdout);}}
#endif

#define DEBUG_DDCMSG 1
#if DEBUG_DDCMSG
#define DDCMSG(DBG,SC, FMT, ...) { if (((DBG)&verbose)==(DBG)) { fprintf(stdout, "\x1B[3%i;%im%s[%04x] " FMT "\x1B[30;0m @ %s:%i\n",SC&7,(SC>>3)&1,__PROGRAM__,(DBG), ##__VA_ARGS__, __FILE__, __LINE__); fflush(stdout);}}
#else
#define DDCMSG(DBG,SC, FMT, ...) { if (((DBG)&verbose)==(DBG)) { fprintf(stdout, "\x1B[3%i;%im%s[%04x] " FMT "\x1B[30;0m\n",SC&7,(SC>>3)&1,__PROGRAM__,(DBG), ##__VA_ARGS__ ); fflush(stdout);}}
#endif

#define DCCMSG(SC, EC, FMT, ...) {if (C_DEBUG){ fprintf(stdout, "\x1B[3%i;%im%s      " FMT "\x1B[3%i;%im\n",SC&7,(SC>>3)&1,__PROGRAM__, ##__VA_ARGS__ ,EC&7,(EC>>3)&1); fflush(stdout);}}
#define DCOLOR(SC) { if (C_DEBUG){ fprintf(stdout, "\x1B[3%i;%im",SC&7,(SC>>3)&1); fflush(stdout);}}

#define EMSG(FMT, ...) { fprintf(stderr, "%s      " FMT " @ %s:%i" , __PROGRAM__ , ##__VA_ARGS__ , __FILE__, __LINE__); fflush(stderr);}
#define PERROR(FMT, ...) { fprintf(stderr, "%s      " FMT "@ %s:%i :" , __PROGRAM__ , ##__VA_ARGS__ , __FILE__, __LINE__); perror(""); fflush(stderr);}

//  here are two usage examples of DCMSG
//DCMSG(RED,"example of DCMSG macro  with arguments  enum = %i  biff=0x%x",ghdr->cmd,biff) ;
//DCMSG(blue,"example of DCMSG macro with no args") ;   
//   I always like to include the trailing ';' so my editor can indent automatically

#define CPRINT_HEXB(SC,data, size)  { if (C_DEBUG) {{ \
                                       fprintf(stdout, "DEBUG:\x1B[3%i;%im     ",(SC)&7,((SC)>>3)&1); \
                                       char *_data = (char*)data; \
                                       fprintf(stdout, "          "); \
                                       for (int _i=0; _i<size; _i++) fprintf(stdout, "%02x.", (__uint8_t)_data[_i]); \
                                       fprintf(stdout, " in %s at line %i\x1B[30;0m\n", __FILE__, __LINE__); \
                                    }; fflush(stdout); }}



#define DCMSG_HEXB(SC,hbuf,data, size)  { if (C_DEBUG) {{ \
                                           fprintf(stdout, "\x1B[3%i;%im%s      %s",(SC)&7,((SC)>>3)&1,__PROGRAM__,hbuf); \
                                           char *_data = (char*)data; \
                                           fprintf(stdout, "          "); \
                                           for (int _i=0; _i<size; _i++) fprintf(stdout, "%02x.", (__uint8_t)_data[_i]); \
                                           fprintf(stdout, " in %s at line %i\x1B[30;0m\n", __FILE__, __LINE__); \
                                        }; fflush(stdout); }}

//  this one prints the hex dump on next line, and prefixs the second line with [dbg]
#define DDCMSG2_HEXB(DBG,SC,hbuf,data, size)  { if ((verbose&(DBG))==(DBG)) {{ \
                                                 fprintf(stdout, "\x1B[3%i;%im%s[%04x] %s\n",(SC)&7,((SC)>>3)&1,__PROGRAM__,(DBG),hbuf); \
                                                 char *_data = (char*)data; \
                                                 fprintf(stdout, "[%04x]    ", (DBG)); \
                                                 for (int _i=0; _i<size; _i++) fprintf(stdout, "%02x.", (__uint8_t)_data[_i]); \
                                                 fprintf(stdout, " in %s at line %i\x1B[30;0m\n", __FILE__, __LINE__); \
                                              }; fflush(stdout); }}


//  this one prints the hex dump on the same line as 'hbuf'
#define DDCMSG_HEXB(DBG,SC,hbuf,data, size)  { if ((verbose&(DBG))==(DBG)) {{ \
                                                fprintf(stdout, "\x1B[3%i;%im%s[%04x] %s",(SC)&7,((SC)>>3)&1,__PROGRAM__,(DBG),hbuf); \
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
#define FAST_SOON_TIME        45  /* 4 1/2 seconds */
#define FAST_WAIT_TIME        900 /* 90 seconds */
#define FAST_TIME             130 /* 13 seconds */
#define SLOW_SOON_TIME        45  /* 4 1/2 seconds */
#define SLOW_WAIT_TIME        900 /* 90 seconds */
#define SLOW_TIME             1200 /* 2 minutes */
#define EVENT_SOON_TIME       5   /* 1/2 second */
#define TRANSMISSION_TIME     4   /* 4/10 second */
#define SIT_TRANSITION_TIME   4   /* 4/10 second */
#define SAT_TRANSITION_TIME   90  /* 9 seconds */
#define HSAT_TRANSITION_TIME  115 /* 11 1/2 second */
#define MIT_MOVE_START_TIME   8   /* 4/5 second */
#define MAT_MOVE_START_TIME   120 /* 12 seconds */
#define RESP_TIME             8   /* 8/10 second */
extern int fast_time, slow_time; // global, user-configurable versions of FAST_TIME and SLOW_TIME

#if 0 /* old timer stuff */
enum {
   F_fast_none = 0, /* no state, doing nothing */
   F_fast_start, /* starts fast lookup, goes to fast start */
   F_fast_medium, /* starts medium fast lookup, goes to fast medium */
   F_fast_once, /* starts fast lookup, goes to fast end */
   F_fast_end, /* ends fast lookup, goes to slow start */
};

enum {
   F_slow_none = 0, /* no state, doing nothing */
   F_slow_start, /* starts slow lookup, goes to slow continue */
   F_slow_continue, /* continues slow lookup, goes to slow start */
};

#define FAST_SOON_TIME        5   /* 1/2 second */
#define FAST_TIME             40  /* 4 seconds */
#define SLOW_SOON_TIME        5   /* 1/2 second */
#define SLOW_TIME             600 /* 1 minute */
#define FAST_RESPONSE_TIME    90  /* 9 seconds */
#define SLOW_RESPONSE_TIME    90  /* 9 seconds */
#define START_STANDARD_LOOKUP(S) {\
   /* ensure we're doing some sort of slow lookup */ \
   if (S.rf_t.slow_flags == F_slow_none) { \
      /* wasn't going slow, so either it was going fast and we should change it, or it is already doing the fast lookup */ \
      if (!(S.rf_t.fast_flags == F_fast_once || \
            S.rf_t.fast_flags == F_fast_end)) { \
         /* wasn't already going slow and wasn't doing fast lookup, go slow, but first time fast */ \
         stopTimer(S.rf_t, fast_timer, fast_flags); \
         setTimerTo(S.rf_t, fast_timer, fast_flags, FAST_SOON_TIME, F_fast_once); \
         DDCMSG(D_MSTATE, GRAY, "Started lookup timer: rf_t.fast=%i.%i rf_t.slow=%i.%i", S.rf_t.fast_timer, S.rf_t.fast_flags, S.rf_t.slow_timer, S.rf_t.slow_flags); \
      } else { \
         /* is already doing fast lookup .... will be slow soon */ \
         DDCMSG(D_MSTATE, GRAY, "Didn't do anything with lookup timers because fast: rf_t.fast=%i.%i rf_t.slow=%i.%i", S.rf_t.fast_timer, S.rf_t.fast_flags, S.rf_t.slow_timer, S.rf_t.slow_flags); \
      } \
   } else { \
      /* is already going slow... */ \
      DDCMSG(D_MSTATE, GRAY, "Didn't do anything with lookup timers because slow: rf_t.fast=%i.%i rf_t.slow=%i.%i", S.rf_t.fast_timer, S.rf_t.fast_flags, S.rf_t.slow_timer, S.rf_t.slow_flags); \
   } \
}

#endif

#define RF_BURST_DELAY 150 /* the amount of time to wait between sending bursts of data */
#define RF_COLLECT_DELAY 350 /* the amount of time to wait for multiple messages to be combined together */

// other state constants
#define MAX_MISS_TIME 3 /* maximimum number of minutes to go without a response */
#define FAST_TIME_MAX_MISS ((MAX_MISS_TIME * 600) / fast_time) /* maximum value of the "missed message" counter, calculated from X minutes */
#define SLOW_TIME_MAX_MISS 5 /* maximum value of the "missed message" counter, not based on X minutes */
#define EVENT_MAX_MISS 10 /* maximum value of the "missed message" counter */
#define EVENT_MAX_UNREPORT (4*15) /* max number of reports per burst * max number of non-reports before vacuum */

// MIT/MAT speed constants
extern int MIT_SPEEDS[]; // in meters per second (times 1000)
extern int MAT_SPEEDS[]; // in meters per second (times 1000)
extern int MIT_ACCEL[];  // in meters per second per second (times 1000)
extern int MAT_ACCEL[];  // in meters per second per second (times 1000)

// common complex timer starts
#define START_EXPOSE_TIMER(S) { \
   /* start expose simulation */ \
   setTimerTo(S.exp, exp_timer, exp_flags, TRANSMISSION_TIME, F_exp_start_transition); \
}
#define START_CONCEAL_TIMER(S) { \
   /* start conceal simulation */ \
   setTimerTo(S.exp, con_timer, con_flags, TRANSMISSION_TIME, F_con_start_transition); \
}
#define START_MOVE_TIMER(S) { \
   /* start move simulation */ \
   setTimerTo(S.move, timer, flags, TRANSMISSION_TIME, F_move_start_movement); \
}
#define DO_FAST_LOOKUP(S) {\
   S.rf_t.fast_missed = 0; \
   stopTimer(S.rf_t, slow_timer, slow_flags); \
   if (S.rf_t.fast_flags == F_fast_none || S.rf_t.fast_flags == F_fast_wait) { \
      DDCMSG(D_POINTER, GRAY,"Minion %i:%2x DO_FAST_LOOKUP success @ %s:%i", minion->mID, minion->devid, __FILE__, __LINE__); \
      setTimerTo(S.rf_t, fast_timer, fast_flags, FAST_SOON_TIME, F_fast_start); \
   } else { \
      /* is already going fast */ \
      DDCMSG(D_POINTER, GRAY,"Minion %i:%2x DO_FAST_LOOKUP attempt @ %s:%i", minion->mID, minion->devid, __FILE__, __LINE__); \
   } \
}
#define DO_SLOW_LOOKUP(S) {\
   S.rf_t.slow_missed = 0; \
   stopTimer(S.rf_t, fast_timer, fast_flags); \
   if (S.rf_t.slow_flags == F_slow_none || S.rf_t.slow_flags == F_slow_wait) { \
      setTimerTo(S.rf_t, slow_timer, slow_flags, SLOW_SOON_TIME, F_slow_start); \
      DDCMSG(D_POINTER, GRAY,"Minion %i:%2x DO_SLOW_LOOKUP success @ %s:%i", minion->mID, minion->devid, __FILE__, __LINE__); \
   } else { \
      /* is already going slow */ \
      DDCMSG(D_POINTER, GRAY,"Minion %i:%2x DO_SLOW_LOOKUP attempt @ %s:%i", minion->mID, minion->devid, __FILE__, __LINE__); \
   } \
}
#define STOP_FAST_LOOKUP(S) {\
   if (S.rf_t.fast_flags != F_fast_none) { \
      setTimerTo(S.rf_t, fast_timer, fast_flags, FAST_WAIT_TIME, F_fast_wait); \
      /* stopTimer(S.rf_t, fast_timer, fast_flags); */ \
   } \
}
#define STOP_SLOW_LOOKUP(S) {\
   if (S.rf_t.slow_flags != F_slow_none) { \
      setTimerTo(S.rf_t, slow_timer, slow_flags, SLOW_WAIT_TIME, F_slow_wait); \
      /* stopTimer(S.rf_t, slow_timer, slow_flags); */ \
   } \
}

// macro to disconnect minion
//  -- can be used only in certain parts of minion.c and minion_state.c
#define DISCONNECT { \
   /* break connection to FASIT server */ \
   close(minion->rcc_sock); /* close FASIT */ \
   DCMSG(BLACK,"\n\n-----------------------------------\nDisconnected minion %i:%i:%i\n----------------------------------- %i\n\n", \
         minion->mID, minion->rcc_sock, minion->mcp_sock, __LINE__); \
   minion->rcc_sock = -1; \
   minion->status = S_closed; \
   LB_buf.cmd = LBC_ILLEGAL; /* send an illegal packet, which makes mcp forget me */ \
   /* now send it to the MCP master */ \
   DDCMSG(D_PACKET,BLUE,"Minion %i:  LBC_ILLEGAL cmd=%i", minion->mID,LB_buf.cmd); \
   result= psend_mcp(minion,&LB_buf); \
   fsync(minion->mcp_sock); /* make sure the data gets written to the mcp before we close */ \
   close(minion->mcp_sock); /* close this half of the connection to mcp */ \
   exit(0); /* exit the forked minion -- all unfreed data is will now free */ \
}

#define change_states(M, T, S, F) { \
   const char *st; \
   switch (S) { \
      case TS_concealed: st = "TS_concealed"; break; \
      case TS_con_to_exp: st = "TS_con_to_exp"; break; \
      case TS_exposed: st = "TS_exposed"; break; \
      case TS_exp_to_con: st = "TS_exp_to_con"; break; \
      case TS_too_far: st = "TS_too_far"; break; \
      default: st = "Nothing I expected"; break; \
   } \
   /*DDCMSG(D_POINTER, RED, "called change_states(%i, %s=%s, %s=%i) @ %s:%i", M->mID, #S, st, #F, F, __FILE__, __LINE__);*/ \
   change_states_internal(M, T, S, F); \
}

// macro to send a status request
#define SEND_STATUS_REQUEST(LB_buf) { \
   /* send out a request for actual position */ \
   LB_status_req_t *L=(LB_status_req_t *)LB_buf; \
   LB_status_req_t *_L; \
   sequence_tracker_t *t; \
   L->cmd=LBC_STATUS_REQ;          /* start filling in the packet */ \
   L->addr=minion->RF_addr; \
   DDCMSG(D_MSTATE,GREEN,"Minion %i:   build and send L LBC_STATUS_REQ", minion->mID); \
   result = psend_mcp_seq(minion,L); \
   /* fill in tracker info */ \
   t = malloc(sizeof(sequence_tracker_t)); \
   t->sequence = minion->S.last_sequence_sent; \
   _L = malloc(sizeof(LB_status_req_t)); /* turn link into allocated data */ \
   memcpy(_L, L, sizeof(LB_status_req_t)); \
   t->pkt = (char*)_L; \
   t->data = NULL; \
   t->sent_callback = StatusReqSentCallback; \
   t->removed_callback = StatusReqRemovedCallback; \
   t->missed = 50; /* after we don't get a callback for this message X times, but we do for other messages, give up */ \
   /* place at head of chain */ \
   t->next = minion->S.tracker; \
   minion->S.tracker = t; \
}


void sendStatus2102(int response, FASIT_header *hdr, thread_data_t *minion, minion_time_t *mt);
void change_states_internal(thread_data_t *minion, minion_time_t *mt, trans_step_t step, int force);
void inform_state(thread_data_t *minion, minion_time_t *mt, FASIT_header *hdr);
void Cancel_Status_Resp_Timer(thread_data_t *minion);
void Handle_Status_Resp(thread_data_t *minion, minion_time_t *mt);

void StatusReqSentCallback(thread_data_t *minion, minion_time_t *mt, char *msg, void *data);
void StatusReqRemovedCallback(thread_data_t *minion, minion_time_t *mt, char *msg, void *data);

int psend_mcp(thread_data_t *minion,void *Lc);
int psend_mcp_seq(thread_data_t *minion,void *Lc);

void timestamp(minion_time_t *mt);

#endif
