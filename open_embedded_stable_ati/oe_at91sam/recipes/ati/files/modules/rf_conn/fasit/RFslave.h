#ifndef _RFSLAVE_H_
#define _RFSLAVE_H_

#include "rf.h"

#define MAX_CONNECTIONS 4
#define MAX_IDS 64

#define RF_BUF_SIZE 1024

#define INFINITE -1

typedef struct rf_connection {
   // file descriptors and buffers
   int tty; // the file descriptor to talk to the tty port on
   int sock; // the file descriptor to talk to slaveboss on
   char tty_obuf[RF_BUF_SIZE]; // outgoing buffer for rf tty messages
   char sock_obuf[RF_BUF_SIZE]; // outgoing buffer for rf sock messages
   char tty_ibuf[RF_BUF_SIZE]; // incoming buffer for rf tty messages
   char sock_ibuf[RF_BUF_SIZE]; // incoming buffer for rf sock messages
   int tty_olen; // length of outgoing rf tty buffer
   int sock_olen; // length of outgoing rf sock buffer
   int tty_ilen; // length of incoming rf tty buffer
   int sock_ilen; // length of incoming rf sock buffer

   // delay
   struct timespec time_start; // the time when we last received a message
   struct timespec timeout_when; // the time when we must send the message

   // timeslot stuff
   int timeslot_length; // the amount of time each timeslot is, as determined by the RFmaster
   int ids[MAX_IDS]; // the ids we're sending from this time (standard, and group)
   int id_index; // the last id in the ids list we used
   int devids[MAX_IDS]; // the devids we're sending from this time
   int devid_index; // the last devid in the devids list we used
   int ids_lasttime[MAX_IDS]; // the ids from the last burst message (even a burst of 1)
   int id_lasttime_index; // the last id in the ids_lasttime list we used
   int devid_last_low; // the low_dev from the last LBC_REQUEST_NEW packet
   int devid_last_high; // the high_dev from the last LBC_REQUEST_NEW packet
} rf_connection_t;

extern int verbose;

// time delay functions
int getTimeout(rf_connection_t *rc); // returns the timeout value to pass to epoll
void doTimeAfter(rf_connection_t *rc, int msecs); // set the timeout for X milliseconds after the start time
void waitRest(rf_connection_t *rc); // wait until the timeout time arrives (the epoll timeout will get us close)


// read/write function helpers
int rcRead(rf_connection_t *rc, int tty); // if tty is 1, read data from tty into buffer, otherwise read data from socket into buffer
int rcWrite(rf_connection_t *rc, int tty); // if tty is 1, write data from buffer to tty, otherwise write data from buffer into socket


// buffer commands
void addToBuffer_tty_out(rf_connection_t *rc, char *buf, int s);
void addToBuffer_tty_in(rf_connection_t *rc, char *buf, int s);
void addToBuffer_sock_out(rf_connection_t *rc, char *buf, int s);
void addToBuffer_sock_in(rf_connection_t *rc, char *buf, int s);
void clearBuffer(rf_connection_t *rc, int end, int tty); // if tty is 1, clear "in" tty buffer, otehrwise clear "in" socket buffer


// data transfer functions
//  all: pass connection and new message data, return bit flags:
//       0) do nothing
//       1) mark rf tty as writeable in epoll
//       2) mark rf sock as writeable in epoll
//       3) unmark rf tty as writeable in epoll
//       4) unmark rf sock as writeable in epoll
//       5) remove rf tty from epoll and close
//       6) remove rf sock from epoll and close
int tty2sock(rf_connection_t *rc); // transfer tty data to the socket
int sock2tty(rf_connection_t *rc); // transfer socket data to the tty and set up delay times
#define doNothing 0
#define mark_ttyWrite (1<<1)
#define mark_sockWrite (1<<2)
#define mark_ttyRead (1<<3)
#define mark_sockRead (1<<4)
#define rem_ttyEpoll (1<<5)
#define rem_sockEpoll (1<<6)

// file descriptor helper functions
void setnonblocking(int fd, int sock_stuff); // sock_stuff set to 1 the first time on a socket
void setblocking(int fd);

// for some reason we have a ntohs/htons, but no ntohf/htonf
float ntohf(float f);

#define htonf(f) (ntohf(f))

#define min(a, b) ( a > b ? b : a )
#define max(a, b) ( a > b ? a : b )

// for debugging memset/memcpy
//#define DEBUG_MEM

#ifdef DEBUG_MEM

void *__D_memcpy(void *dest, const void *src, size_t n, char* f, int l);
void *__D_memset(void *s, int c, size_t n, char* f, int l);
#define D_memcpy(dest,src,n) __D_memcpy(dest,src,n, __FILE__, __LINE__)
#define D_memset(s,c,n) __D_memset(s,c,n, __FILE__, __LINE__)

#else
#define D_memcpy(dest,src,n) memcpy(dest,src,n)
#define D_memset(s,c,n) memset(s,c,n)
#endif

#endif

