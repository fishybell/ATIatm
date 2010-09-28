#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include <sys/epoll.h>
#include <bits/types.h>
#include <termios.h>
#include <map>


// max amount of data we'll read per fd in a single loop
#define BUF_SIZE 256

using namespace std;

// generic base class for handling asynchronous i/o for a given file descriptor
class Connection {
public:
   Connection(int fd); // a new connection always starts with a file descriptor
   virtual ~Connection(); // closes, cleans up, etc.
   static Connection *findByUUID(__uint64_t uuid); // returns null if not found
   static Connection *findByRnum(__uint32_t rnum); // returns null if not found
   static Connection *findByTnum(__uint16_t tnum); // returns null if not found
   void queueMsg(char *msg, int size);
   void queueMsg(void *msg, int size) { queueMsg((char*)msg, size); }
   __uint64_t getUUID() {return uuid;};
   __uint32_t getRnum() {return rnum;};
   __uint16_t getTnum() {return tnum;};
   void setUUID(__uint64_t uuid); // if none is already set, this will set the uuid and add it to the map
   void setTnum(__uint16_t tnum); // if none is already set, this will set the tnum and add it to the map
   static void Init(int efd); // initialize with the global event fd
   int handleReady(epoll_event *ev); // called when either ready to read or write; returns -1 if needs to be deleted afterwards
   int getFD() { return fd; }; // retrieve the file descriptor for use in epoll or select or similar

protected:
   virtual int handleWrite(epoll_event *ev); // could be overwritten
   virtual int handleRead(epoll_event *ev); // could be overwritten
   virtual int parseData(int rsize, char *rbuf) = 0; // must be defined in the final message handler

   static int efd; // global event fd
   static map<__uint64_t,Connection *> uuidMap; // map for finding my unique id
   static map<__uint32_t,Connection *> rnumMap; // map for finding my unique random number
   static map<__uint16_t,Connection *> tnumMap; // map for finding my unique target number
   int fd; // file descriptor for reading and writing
   __uint32_t rnum; // random number to find this connection by
   __uint64_t uuid; // mac address to find this connection by
   __uint16_t tnum; // random assigned number to find this connection by
   char *wbuf; // write buffer
   int wsize; // write buffer size
   char *lwbuf; // last write buffer (in case a resend is required)
   int lwsize; // last write buffer size (in case a resend is required)
};

#endif
