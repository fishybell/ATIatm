#include "connection.h"
#include "common.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

using namespace std;

// define static members
int Connection::efd; // global event fd
map<__uint64_t,Connection *> Connection::uuidMap; // map for finding my unique id
map<__uint32_t,Connection *> Connection::rnumMap; // map for finding my unique random number
map<__uint16_t,Connection *> Connection::tnumMap; // map for finding my unique target number


Connection::Connection(int fd) {
FUNCTION_START("::Connection(int fd)")
   // create random number and map this to it
   rnum = rand();
   this->fd = fd;
   rnumMap[rnum] = this;
   wbuf = NULL;
   wsize = 0;
   lwbuf = NULL;
   lwsize = 0;
   uuid = 0;
   tnum = 0;
FUNCTION_END("::Connection(int fd)")
}

Connection::~Connection() {
FUNCTION_START("::~Connection()")
   // clear this item out of maps
   map<__uint64_t,Connection *>::iterator uIt;
   map<__uint32_t,Connection *>::iterator rIt;

   uIt = uuidMap.find(uuid);
   if(uIt != uuidMap.end()) {
      uuidMap.erase(uIt);
   }

   rIt = rnumMap.find(rnum);
   if(rIt != rnumMap.end()) {
      rnumMap.erase(rIt);
   }

   // stop watching for the connection and close it
   epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
   close(fd);

   // free the write buffer
   if (wbuf) {
      delete [] wbuf;
   }

   // free the last write buffer
   if (lwbuf) {
      delete [] lwbuf;
   }

   IMSG("Closed connection %i\n", fd)
FUNCTION_END("::~Connection()")
}

void Connection::Init(int efd) {
FUNCTION_START("::Init(int efd)")
   Connection::efd = efd;

   // initialize random with seed with time in seconds and microseconds XOR'ed togetherd
   timeval tv;
   gettimeofday(&tv, NULL);
   int t = tv.tv_sec ^ tv.tv_usec;
   srand(t);
FUNCTION_END("::Init(int efd)")
}

// finds an existing Connection object or returns null
Connection* Connection::findByUUID(__uint64_t uuid) {
FUNCTION_START("::findByUUID(__uint64_t uuid)")
   map<__uint64_t,Connection *>::iterator uIt;
   uIt = uuidMap.find(uuid);
   if (uIt == uuidMap.end()) {
FUNCTION_HEX("::findByUUID(__uint64_t uuid)", NULL)
      return NULL;
   }
FUNCTION_HEX("::findByUUID(__uint64_t uuid)", uIt->second)
   return uIt->second;
}

// finds an existing Connection object or returns null
Connection* Connection::findByRnum(__uint32_t rnum) {
FUNCTION_START("::findByRnum(__uint32_t rnum)")
   map<__uint32_t,Connection *>::iterator rIt;
   rIt = rnumMap.find(rnum);
   if (rIt == rnumMap.end()) {
FUNCTION_HEX("::findByRnum(__uint32_t rnum)", NULL)
      return NULL;
   }
FUNCTION_HEX("::findByRnum(__uint32_t rnum)", rIt->second)
   return rIt->second;
}

// finds an existing Connection object or returns null
Connection* Connection::findByTnum(__uint16_t tnum) {
FUNCTION_START("::findByTnum(__uint16_t tnum)")
   map<__uint16_t,Connection *>::iterator tIt;
   tIt = tnumMap.find(tnum);
   if (tIt == tnumMap.end()) {
FUNCTION_HEX("::findByTnum(__uint16_t tnum)", NULL)
      return NULL;
   }
FUNCTION_HEX("::findByTnum(__uint16_t tnum)", tIt->second)
   return tIt->second;
}

// set, once, the uuid for this
void Connection::setUUID(__uint64_t uuid) {
FUNCTION_START("::setUUID(__uint64_t uuid)")
   // only add if not already mapped
   if(findByUUID(uuid) == NULL) {
      this->uuid = uuid;
      uuidMap[uuid] = this;
   }
FUNCTION_END("::setUUID(__uint64_t uuid)")
}

// set, once, the tnum for this
void Connection::setTnum(__uint16_t tnum) {
FUNCTION_START("::setTnum(__uint16_t tnum)")
   // only add if not already mapped
   if(findByTnum(tnum) == NULL) {
      this->tnum = tnum;
      tnumMap[tnum] = this;
   }
FUNCTION_END("::setTnum(__uint16_t tnum)")
}

// the file descriptor is ready to give us data, read as much as possible (max of BUF_SIZE)
int Connection::handleRead(epoll_event *ev) {
FUNCTION_START("::handleRead(epoll_event *ev)")
   char buf[BUF_SIZE+1];
   int rsize=0;
   rsize = read(fd, buf, BUF_SIZE);
   if (rsize > 0) {
      int ret = parseData(rsize, buf);
FUNCTION_INT("::handleRead(epoll_event *ev)", ret)
      return ret;
   } else if (rsize != -1 || (rsize == -1 && errno != EAGAIN)) {
      // the client has closed this connection, schedule the deletion by returning -1
FUNCTION_INT("::handleRead(epoll_event *ev)", -1)
      return -1;
   }
FUNCTION_INT("::handleRead(epoll_event *ev)", 0)
   return 0;
}

// the file descriptor is ready to receive the data, send it on through
int Connection::handleWrite(epoll_event *ev) {
FUNCTION_START("::handleWrite(epoll_event *ev)")
   if (wsize <= 0) {
      // we only send data, or listen for writability, if we have something to write
      ev->events = EPOLLIN;
      epoll_ctl(efd, EPOLL_CTL_MOD, fd, ev);
FUNCTION_INT("::handleWrite(epoll_event *ev)", 0)
      return 0;
   }

   // write all the data we can
   int s = write(fd, wbuf, wsize);

   // did it fail?
   if (s == -1) {
      if (errno == EAGAIN) {
FUNCTION_INT("::handleWrite(epoll_event *ev)", 0)
         return 0;
      } else {
FUNCTION_INT("::handleWrite(epoll_event *ev)", -1)
         return -1;
      }
   }

   // copy what we did write to the "last write buffer"
   if (lwbuf != NULL) { delete [] lwbuf; } // clear out old buffer
   lwsize = s;
   lwbuf = new char[lwsize];
   memcpy(lwbuf, wbuf, lwsize);

   if (s < wsize) {
      // create a new, smaller write buffer if we didn't write everything
      char *tbuf = new char[(wsize - s)];
      memcpy(tbuf, wbuf + (sizeof(char) * s), wsize - s);
      delete [] wbuf;
      wbuf = tbuf;
      wsize -= s;
   } else {
      // everything was written, clear write buffer
      wsize = 0;
      delete [] wbuf;
      wbuf = NULL;
   }

   // success
FUNCTION_INT("::handleWrite(epoll_event *ev)", 0)
   return 0;
}

// handles an incoming event (ready for read or ready for write or both), possibly writing the other connections write buffers in the process
// -1 is returned if this object needs to be deleted afterwards
int Connection::handleReady(epoll_event *ev) {
FUNCTION_START("::handleReady(epoll_event *ev)")
   int ret = 0;
   // handle writing out first as it is potentially time sensitive
   if (ev->events & EPOLLOUT) {
      if (handleWrite(ev) == -1) {
         ret = -1;
      }
   }
   if (ev->events & EPOLLIN || ev->events & EPOLLPRI) {
      if (handleRead(ev) == -1) {
         ret = -1;
      }
   }
   // checking for ev->events & EPOLLRDHUP doesn't work on the board, new kernel needed
   if (ev->events & EPOLLERR || ev->events & EPOLLHUP) {
      ret = -1;
   }
FUNCTION_INT("::handleReady(epoll_event *ev)", ret)
   return ret;
}

// add this message to the write buffer
// as this function does not actually write and will not cause the caller function to be
//   preempted, the caller may call this function multiple times to create a complete
//   message and be sure that the entire message is sent
void Connection::queueMsg(char *msg, int size) {
FUNCTION_START("::queueMsg(char *msg, int size)")
   if (wsize > 0) {
      // append
      char *tbuf = new char[(size+wsize)];
      memcpy(tbuf, wbuf, wsize);
      memcpy(tbuf+(sizeof(char) * size), msg, size);
      wsize += size;
      delete [] wbuf;
      wbuf = tbuf;
   } else {
      // copy
      wbuf = new char[size];
      memcpy(wbuf, msg, size);
      wsize = size;
   }

   // set this connection to watch for writeability
   epoll_event ev;
   ev.data.ptr = (void*)this;
   ev.events = EPOLLIN | EPOLLOUT;
   epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev);
FUNCTION_END("::queueMsg(char *msg, int size)")
}

