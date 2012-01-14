#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include <sys/epoll.h>
#include <bits/types.h>
#include <termios.h>
#include <map>
#include <list>


// max amount of data we'll read per fd in a single loop
#define BUF_SIZE 1024

using namespace std;

// generic base class for handling asynchronous i/o for a given file descriptor
class Connection {
public:
   Connection(int fd); // a new connection always starts with a file descriptor
   virtual ~Connection(); // closes, cleans up, etc.
   static Connection *findByUUID(__uint64_t uuid); // returns null if not found
   static Connection *findByRnum(__uint32_t rnum); // returns null if not found
   static Connection *findByTnum(__uint16_t tnum); // returns null if not found
   virtual void queueMsg(const char *msg, int size);
   void queueMsg(const void *msg, int size) {queueMsg((const char*)msg, size);} // auto-cast for various data pointers
   void finishMsg() {newMsg = true;}; // make the currently queued message a singular message (good for packet sending)
   void forceQueueDump(); // sends all pending messages now (don't use normally)
   __uint64_t getUUID() {return uuid;};
   __uint32_t getRnum() {return rnum;};
   __uint16_t getTnum() {return tnum;};
   void setUUID(__uint64_t uuid); // if none is already set, this will set the uuid and add it to the map
   void setTnum(__uint16_t tnum); // if none is already set, this will set the tnum and add it to the map
   static void Init(class TCP_Factory *factory, int efd); // initialize with the global factory and event fd
   int handleReady(const epoll_event *ev); // called when either ready to read or write; returns -1 if needs to be deleted afterwards
   int getFD() { return fd; }; // retrieve the file descriptor for use in epoll or select or similar
   void deleteLater(); // cause this tcp to be deleted at a later point in time
   virtual void makeWritable(bool writable); // allows the global event fd to watch for writable status of this connection or not

   static bool addToEPoll(int fd, void *ptr); // add a file descriptor to the epoll

   static Connection *getFirst() { return flink; }
   Connection *getNext() { return link; }

   virtual bool reconnect() { return false; }; // by default, no reconnect attempt is made

private :
   // for linked list
   Connection *link; // link to next
   static Connection *flink; // link to first
   void initChain(); // initialize place in linked list

protected:
   virtual int handleWrite(const epoll_event *ev); // could be overwritten
   virtual int handleRead(const epoll_event *ev); // could be overwritten
   virtual int parseData(int rsize, const char *rbuf) = 0; // must be defined in the final message handler

   static class TCP_Factory *factory; // global factory
   static int efd; // global event fd
   static map<__uint64_t,Connection *> uuidMap; // map for finding my unique id
   static map<__uint32_t,Connection *> rnumMap; // map for finding my unique random number
   static map<__uint16_t,Connection *> tnumMap; // map for finding my unique target number
   int fd; // file descriptor for reading and writing
   __uint32_t rnum; // random number to find this connection by
   __uint64_t uuid; // mac address to find this connection by
   __uint16_t tnum; // random assigned number to find this connection by
   list <char*> wbuf; // write buffer list (for handling multiple messages)
   list <int> wsize; // write buffer size list (for handling multiple messages)
   bool newMsg; // will the next queueMsg call create a new item in the list?
   char *lwbuf; // last write buffer (in case a resend is required)
   int lwsize; // last write buffer size (in case a resend is required)
   int needDelete; // for delayed disconnection
};

#endif
