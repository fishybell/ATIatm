#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

using namespace std;

#include "tcp_factory.h"
#include "tcp_client.h"
#include "common.h"
#include "timers.h"

TCP_Client::TCP_Client(int fd, int tnum) : FASIT_TCP(fd, tnum) {
FUNCTION_START("::TCP_Client(int fd, int tnum) : Connection(fd)")
   server = NULL;
FUNCTION_END("::TCP_Client(int fd, int tnum) : Connection(fd)")
}

TCP_Client::~TCP_Client() {
FUNCTION_START("::~TCP_Client()")
   if (server != NULL) {
      // if we are deleted outside of the server being deleted, kill the server too
      server->clientLost();
   }
FUNCTION_END("::~TCP_Client()")
}

bool TCP_Client::reconnect() {
FUNCTION_START("::reconnect()")

   // were we really in need of deletion?
   if (needDelete) {
      return false; // don't reconnect
   }

   // remove dead socket from of epoll
   epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
   close(fd);

   // clear buffers
   if (wbuf) {			// clear write buffer
      delete [] wbuf;
      wbuf = NULL;
   }
   if (lwbuf) {			// clear last write buffer
      delete [] lwbuf;
      lwbuf = NULL;
   }
   if (rbuf) {			// clear read buffer
      delete [] rbuf;
      rbuf = NULL;
   }

   // queue the reconnect for 10 seconds from now
   new ReconTimer(this, RECONNECTION);

FUNCTION_INT("::reconnect()", true)
   return true;
}

void TCP_Client::handleReconnect() {
FUNCTION_START("::handleReconnect()")

   // attempt reconnection now
   int sock = factory->newClientSock();
   if (sock == -1) {
      // queue another reconnect for 10 seconds from now
      new ReconTimer(this, RECONNECTION);
      return; // we'll be back
   }

   // set fd to new socket
   fd = sock;

   // add back in to epoll
   if (!addToEPoll(sock, this)) {
      // failure this far along = failure always
      deleteLater();
   }

FUNCTION_END("::handleReconnect()")
}
