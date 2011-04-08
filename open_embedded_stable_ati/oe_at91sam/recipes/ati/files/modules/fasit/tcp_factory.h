#ifndef _TCP_FACTORY_H_
#define _TCP_FACTORY_H_

#include <netinet/in.h>
#include "connection.h"
#include "tcp_client.h"

class FASIT_TCP_Factory : public Connection { // we only use Connection for the use of its epoll file descriptor
public :
   FASIT_TCP_Factory(const char *destIP, int port);
   ~FASIT_TCP_Factory();

   TCP_Client *newConn(); // creates a new connection to the IP and port given in the constructor

protected :
   // we don't actually have a valid file descriptor, so we'll just overwrite these to do nothing
   int handleWrite(const epoll_event *ev) { return 0; };
   int handleRead(const epoll_event *ev) { return 0; };
   int parseData(int rsize, const char *rbuf) { return 0; };

   int findNextTnum(); // look for the lowest available tnum

   struct sockaddr_in server;
};

#endif

