#ifndef _TCP_FACTORY_H_
#define _TCP_FACTORY_H_

#include "connection.h"
#include "fasit_tcp.h"
#include <netinet/in.h>

class FASIT_TCP_Factory : public Connection {
public :
   FASIT_TCP_Factory(char *destIP, int port);
   ~FASIT_TCP_Factory();

   FASIT_TCP *newConn(); // creates a new connection to the IP and port given in the constructor

   static void SendResubscribe(); // tell all downrange units to resubscribe

protected :
   // we don't actually have a valid file descriptor, so we'll just overwrite these to do nothing
   int handleWrite(epoll_event *ev) { return 0; };
   int handleRead(epoll_event *ev) { return 0; };
   int parseData(int rsize, char *rbuf) { return 0; };

   int findNextTnum(); // look for the lowest available tnum

   struct sockaddr_in server;
};

#endif

