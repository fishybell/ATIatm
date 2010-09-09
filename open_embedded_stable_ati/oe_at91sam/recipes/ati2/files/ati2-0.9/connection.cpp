#include "connection.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

using namespace std;

// define static members
int Connection::efd; // global event fd
map<__uint64_t,Connection *> Connection::uuidMap; // map for finding my unique id
map<__uint32_t,Connection *> Connection::rnumMap; // map for finding my unique random number
map<__uint16_t,Connection *> Connection::tnumMap; // map for finding my unique target number


Connection::Connection(int fd) {
   // create random number and map this to it
   rnum = rand();
   this->fd = fd;
   rnumMap[rnum] = this;
   wsize = 0;
   uuid = 0;
   tnum = 0;
}

Connection::~Connection() {
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

   printf("Closed connection %i\n", fd);
}

void Connection::Init(int efd) {
   // initialize random with seed of seconds since epoch (TODO -- find smarter way)
   int t = time(NULL);
   srand(t);
   Connection::efd = efd;
}

// finds an existing Connection object or returns null
Connection* Connection::findByUUID(__uint64_t uuid) {
   map<__uint64_t,Connection *>::iterator uIt;
   uIt = uuidMap.find(uuid);
   if (uIt == uuidMap.end()) {
      return NULL;
   }
   return uIt->second;
}

// finds an existing Connection object or returns null
Connection* Connection::findByRnum(__uint32_t rnum) {
   map<__uint32_t,Connection *>::iterator rIt;
   rIt = rnumMap.find(rnum);
   if (rIt == rnumMap.end()) {
      return NULL;
   }
   return rIt->second;
}

// finds an existing Connection object or returns null
Connection* Connection::findByTnum(__uint16_t tnum) {
   map<__uint16_t,Connection *>::iterator tIt;
   tIt = tnumMap.find(tnum);
   if (tIt == tnumMap.end()) {
      return NULL;
   }
   return tIt->second;
}

// set, once, the uuid for this
void Connection::setUUID(__uint64_t uuid) {
   // only add if not already mapped
   if(findByUUID(uuid) == NULL) {
      this->uuid = uuid;
      uuidMap[uuid] = this;
   }
}

// set, once, the tnum for this
void Connection::setTnum(__uint16_t tnum) {
   // only add if not already mapped
   if(findByTnum(tnum) == NULL) {
      this->tnum = tnum;
      tnumMap[tnum] = this;
   }
}

// the file descriptor is ready to give us data, read as much as possible (max of BUF_SIZE)
int Connection::handleRead(epoll_event *ev) {
   char buf[BUF_SIZE+1];
   int rsize=0;
   rsize = read(fd, buf, BUF_SIZE);
   if (rsize > 0) {
      return parseData(rsize, buf);
   } else if (rsize != -1 || (rsize == -1 && errno != EAGAIN)) {
      // the client has closed this connection, schedule the deletion by returning -1
      return -1;
   }
   return 0;
}

// the file descriptor is ready to receive the data, send it on through
int Connection::handleWrite(epoll_event *ev) {
   if (wsize <= 0) {
      // we only send data, or listen for writability, if we have something to write
      ev->events = EPOLLIN;
      epoll_ctl(efd, EPOLL_CTL_MOD, fd, ev);
      return 0;
   }

   // write all the data we can
   int s = write(fd, wbuf, wsize);

   // did it fail?
   if (s == -1) {
      if (errno == EAGAIN) {
         return 0;
      } else {
         return -1;
      }
   }

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
   return 0;
}

// handles an incoming event (ready for read or ready for write or both), possibly writing the other connections write buffers in the process
// -1 is returned if this object needs to be deleted afterwards
int Connection::handleReady(epoll_event *ev) {
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
   if (ev->events & EPOLLRDHUP || ev->events & EPOLLERR || ev->events & EPOLLHUP) {
      ret = -1;
   }
   return ret;
}

// add this message to the write buffer
// as this function does not actually write and will not cause the caller function to be
//   preempted, the caller may call this function multiple times to create a complete
//   message and be sure that the entire message is sent
void Connection::queueMsg(char *msg, int size) {
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
}

