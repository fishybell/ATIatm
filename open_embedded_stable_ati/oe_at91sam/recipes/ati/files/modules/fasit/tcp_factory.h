#ifndef _TCP_FACTORY_H_
#define _TCP_FACTORY_H_

#include <netinet/in.h>
#include "connection.h"
#include "tcp_client.h"

class TCP_Factory : public Connection { // we only use Connection for the use of its epoll file descriptor
public :
   TCP_Factory(const char *destIP, int port);
   ~TCP_Factory();

   template <class TCP_Class> // class needs to be similar to or inherit TCP_Client
      TCP_Class *newConn(); // creates a new connection to the IP and port given in the constructor

   int newClientSock(); // returns a connected and ready socket file descriptor for use in a client

protected :
   // we don't actually have a valid file descriptor, so we'll just overwrite these to do nothing
   int handleWrite(const epoll_event *ev) { return 0; };
   int handleRead(const epoll_event *ev) { return 0; };
   int parseData(int rsize, const char *rbuf) { return 0; };

#ifdef EVENT_CONN
   public:
#endif
   int findNextTnum(); // look for the lowest available tnum
#ifdef EVENT_CONN
   protected:
#endif

   struct sockaddr_in server;
};

#endif

