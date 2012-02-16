#define MAX_CONNECTIONS 16
#define MAX_GROUPS 32

#define FASIT_BUF_SIZE 1024
#define RF_BUF_SIZE 128

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
   int groups[MAX_GROUPS]; // a list of group ids to listen for in addition to the main id
} fasit_connection_t;

extern fasit_connection_t fconns[MAX_CONNECTIONS]; // a slot available for each connection with max groups
extern int last_slot; // the last slot used

// read/write function helpers
int rfRead(int fd, char **dest, int *dests); // read a single RF message into given buffer, return msg num
int fasitRead(int fd, char **dest, int *dests); // same as above, but for FASIT messages
int rfWrite(fasit_connection_t *fc); // write all RF messages for connection in fconns
int fasitWrite(fasit_connection_t *fc); // same as above, but for FASIT messages

// mangling/demangling functions
//  all: pass connection from fconns array, return bit flags:
//       0) do nothing
//       1) mark rf as writeable in epoll
//       2) mark fasit as writeable in epoll
//       3) unmark rf as writeable in epoll
//       4) unmark fasit as writeable in epoll
//       5) remove rf from epoll and close
//       6) remove fasit from epoll and close
int rf2fasit(fasit_connection_t *fc, char *buf, int s, int rfnum); // mangle an rf message into 1 or more fasit messages
int fasit2rf(fasit_connection_t *fc, char *buf, int s, int fasitnum); // mangle one or more fasit message into 1 rf message
#define doNothing 0
#define mark_rfWrite (1<<1)
#define mark_fasitWrite (1<<2)
#define mark_rfRead (1<<3)
#define mark_fasitRead (1<<4)
#define rem_rfEpoll (1<<5)
#define rem_fasitEpoll (1<<6)

