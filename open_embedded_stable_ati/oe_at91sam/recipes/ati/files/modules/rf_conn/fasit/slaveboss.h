#include "fasit_c.h"

#define MAX_CONNECTIONS 16
#define MAX_GROUPS 32

#define FASIT_BUF_SIZE 4096
#define RF_BUF_SIZE 1024

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

typedef struct fasit_connection {
   int rf; // the file descriptor to talk low-bandwidth rf on
   int fasit; // the file descriptor to talk fasit on
   int id; // the id to listen for (temp or standard)
   char rf_obuf[RF_BUF_SIZE]; // outgoing buffer for rf messages
   char fasit_obuf[FASIT_BUF_SIZE]; // outgoing buffer for fasit messages
   char rf_ibuf[RF_BUF_SIZE]; // incoming buffer for rf messages
   char fasit_ibuf[FASIT_BUF_SIZE]; // incoming buffer for fasit messages
   int rf_olen; // length of outgoing rf buffer
   int fasit_olen; // length of outgoing fasit buffer
   int rf_ilen; // length of incoming rf buffer
   int fasit_ilen; // length of incoming fasit buffer
   int index; // the index that this one is in the array

   // Data for FASIT handling
   int seq; // this connections fasit sequence number

   // for power control
   int sleeping;

   // for pyro commands
   int p_zone; // last zone commanded
   int p_fire; // sent "set" fire command (1) or not (0)

   // for 2100 commands
   int hit_on;
   int hit_hit;
   int hit_react;
   int hit_tokill;
   int hit_sens;
   int hit_mode;
   int hit_burst;

   // cached responses
   rf_target_type_t target_type;
   int has_MFS;
   FASIT_2005 f2005_resp;
   FASIT_2101 f2101_resp;
   FASIT_2102 f2102_resp;
   FASIT_2111 f2111_resp;
   FASIT_2112 f2112_resp;
   FASIT_2113 f2113_resp;

   // Data for RF handling
   int groups[MAX_GROUPS]; // a list of group ids to listen for in addition to the main id
   int groups_disabled[MAX_GROUPS]; // a list of group ids that are disabled for me
   
} fasit_connection_t;

extern fasit_connection_t fconns[MAX_CONNECTIONS]; // a slot available for each connection
extern int last_slot; // the last slot used

// read/write function helpers
int rfRead(int fd, char **dest, int *dests); // read a single RF message into given buffer, return do next
int fasitRead(int fd, char **dest, int *dests); // same as above, but for FASIT messages
int rfWrite(fasit_connection_t *fc); // write all RF messages for connection in fconns
int fasitWrite(fasit_connection_t *fc); // same as above, but for FASIT messages

// fasit commands
int send_100(fasit_connection_t *fc);
int send_2000(fasit_connection_t *fc, int zone);
int handle_2004(fasit_connection_t *fc, int start, int end);
int handle_2005(fasit_connection_t *fc, int start, int end);
int handle_2006(fasit_connection_t *fc, int start, int end);
int send_2100_status_req(fasit_connection_t *fc);
int send_2100_exposure(fasit_connection_t *fc, int exp);
int send_2100_power(fasit_connection_t *fc, int cmd);
int send_2100_movement(fasit_connection_t *fc, int move, float speed);
int send_2100_conf_hit(fasit_connection_t *fc, int on, int hit, int react, int tokill, int sens, int mode, int burst);
int handle_2101(fasit_connection_t *fc, int start, int end);
int handle_2102(fasit_connection_t *fc, int start, int end);
int send_2110(fasit_connection_t *fc, int on, int mode, int idelay, int rdelay);
int handle_2111(fasit_connection_t *fc, int start, int end);
int handle_2112(fasit_connection_t *fc, int start, int end);
int handle_2113(fasit_connection_t *fc, int start, int end);
int send_2114(fasit_connection_t *fc);
int handle_2115(fasit_connection_t *fc, int start, int end);
int send_13110(fasit_connection_t *fc);
int handle_13112(fasit_connection_t *fc, int start, int end);
int send_14110(fasit_connection_t *fc, int on);
int handle_14112(fasit_connection_t *fc, int start, int end);
int send_14200(fasit_connection_t *fc, int blank);
int send_14400(fasit_connection_t *fc, int cid, int length, char *data);
int handle_14401(fasit_connection_t *fc, int start, int end);

// rf commands
int handle_STATUS(fasit_connection_t *fc, int start, int end);
int handle_EXPOSE(fasit_connection_t *fc, int start, int end);
int handle_MOVE(fasit_connection_t *fc, int start, int end);
int handle_CONFIGURE_HIT(fasit_connection_t *fc, int start, int end);
int handle_GROUP_CONTROL(fasit_connection_t *fc, int start, int end);
int handle_AUDIO_CONTROL(fasit_connection_t *fc, int start, int end);
int handle_POWER_CONTROL(fasit_connection_t *fc, int start, int end);
int handle_PYRO_FIRE(fasit_connection_t *fc, int start, int end);
int handle_QEXPOSE(fasit_connection_t *fc, int start, int end);
int handle_QCONCEAL(fasit_connection_t *fc, int start, int end);
int send_DEVICE_REG(fasit_connection_t *fc);
int handle_REQUEST_NEW(fasit_connection_t *fc, int start, int end);
int handle_DEVICE_ADDR(fasit_connection_t *fc, int start, int end);


// mangling/demangling functions
//  all: pass connection from fconns array and new message data, return bit flags:
//       0) do nothing
//       1) mark rf as writeable in epoll
//       2) mark fasit as writeable in epoll
//       3) unmark rf as writeable in epoll
//       4) unmark fasit as writeable in epoll
//       5) remove rf from epoll and close
//       6) remove fasit from epoll and close
int rf2fasit(fasit_connection_t *fc, char *buf, int s); // mangle an rf message into 1 or more fasit messages, and potentially respond with rf message
int fasit2rf(fasit_connection_t *fc, char *buf, int s); // mangle one or more fasit messages into 1 rf message (often just caches information until needed by rf2fasit)
#define doNothing 0
#define mark_rfWrite (1<<1)
#define mark_fasitWrite (1<<2)
#define mark_rfRead (1<<3)
#define mark_fasitRead (1<<4)
#define rem_rfEpoll (1<<5)
#define rem_fasitEpoll (1<<6)

// for some reason we have a ntohs/htons, but no ntohf/htonf
float ntohf(float f);

#define htonf(f) (ntohf(f))

