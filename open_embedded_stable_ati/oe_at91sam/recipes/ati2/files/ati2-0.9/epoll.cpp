#include <sys/epoll.h>
#include <stdlib.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>

using namespace std;

#include "connection.h"
#include "serial.h"
#include "fasit_serial.h"
#include "fasit_tcp.h"
#include "tcp_factory.h"
#include "common.h"
#include "timeout.h"


// rough idea of how many connections we'll deal with and the max we'll deal with in a single loop
#define MAX_EVENTS 16

// tcp port we'll listen to for new connections
#define PORT 4000

// define REALTIME as 1 or 0 to enable or disable running as a realtime process
#define REALTIME 0

// utility function to properly configure a client TCP connection
void setnonblocking(int sock) {
FUNCTION_START("setnonblocking(int sock)")
   int opts;

   opts = fcntl(sock, F_GETFL);
   if (opts < 0) {
      perror("fcntl(F_GETFL)");
      exit(EXIT_FAILURE);
   }
   opts = (opts | O_NONBLOCK);
   if (fcntl(sock, F_SETFL, opts) < 0) {
      perror("fcntl(F_SETFL)");
      exit(EXIT_FAILURE);
   }
FUNCTION_END("setnonblocking(int sock)")
}

/**********************************
*          Main Function          *
**********************************/

int main(int argc, char *argv[]) {
PROG_START
   struct epoll_event ev, events[MAX_EVENTS];
   int client, listener, kdpfd; // file descriptors
   fd_set sfds; // select file descriptors
   int n, nfds, yes=1;
   socklen_t addrlen;
   struct sockaddr_in serveraddr, local;
   struct sched_param sp;
   FASIT_TCP_Factory *factory;

// TODO -- messages from base station with no reply after X seconds should cause a resend request

   /* parse argv for command line arguments: */
   int port = PORT;
   list<FASIT_Serial*> serials;
   int base = 0;
const char *usage = "Usage: %s [options]\n\
\t-p X   -- use port X rather than the default \n\
\t-s X   -- connect serial device X (multiple may be called) \n\
\t-r     -- act as a serial relay only \n\
\t-b X   -- act as a base station only, connecting to IP address X\n\
\t-h     -- print out usage information\n";
   for (int i = 1; i < argc; i++) {
      if (argv[i][0] != '-') {
HERE
         IERROR("invalid argument (%i)\n", i)
         return 1;
      }
      switch (argv[i][1]) {
         case 'p' :
HERE
            if (sscanf(argv[++i], "%i", &port) != 1) {
               IERROR("invalid argument (%i)\n", i)
               return 1;
            }
            break;
         case 's' :
HERE
            serials.push_back(new FASIT_Serial(argv[++i]));
            break;
         case 'r' :
HERE
            // TODO -- impliment serial relay program
            printf("Serial relay not implimented, sorry\n");
            return 0;
            break;
         case 'b' :
HERE
            base = ++i; // remember the base paramter for later
            break;
         case 'h' :
HERE
            printf(usage);
            return 0;
            break;
         case '-' :
HERE
            if (argv[i][2] == 'h') {
               printf(usage);
               return 0;
            }
         default :
HERE
            IERROR("invalid argument (%i)\n", i)
            return 1;
      }
   }

   /* clear the global timer */
   Timeout::clearTimeout();

   /* start as either a base station or a "target" */
   if (!base) {
      /* get the listener */
      if((listener = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
         perror("Server-socket() error ");

         /*just exit  */
         return 1;
      }

      /* "address already in use" error message */
      if(setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
         perror("Server-setsockopt() error ");
         return 1;
      }

      /* bind */
      serveraddr.sin_family = AF_INET;
      serveraddr.sin_addr.s_addr = INADDR_ANY;
      serveraddr.sin_port = htons(port);
      memset(&(serveraddr.sin_zero), '\0', 8);

      if(bind(listener, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
         perror("Server-bind() error ");
         return 1;
      }

      /* listen */
      if(listen(listener, 10) == -1) {
         perror("Server-listen() error ");
         return 1;
      }
   } else {
      /* start the factory */
      factory = new FASIT_TCP_Factory(argv[base], port);
   }


   /* set up polling */
   kdpfd = epoll_create(MAX_EVENTS);
   memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN;
   if (!base) {
      // not the base, listen to the listener
      ev.data.ptr = NULL; // indicates listener fd
      if (epoll_ctl(kdpfd, EPOLL_CTL_ADD, listener, &ev) < 0) {
         IERROR("epoll listener insertion error: fd=%d\n", listener)
         return 1;
      }
   }
   Connection::Init(kdpfd);
   list<FASIT_Serial*>::iterator sIt = serials.begin();
   while (sIt != serials.end()) {
      memset(&ev, 0, sizeof(ev));
      ev.events = EPOLLIN;
      ev.data.ptr = (*sIt);
      if (epoll_ctl(kdpfd, EPOLL_CTL_ADD, (*sIt)->getFD(), &ev) < 0) {
         IERROR("epoll serial insertion error: fd=%d\n", (*sIt)->getFD())
         return 1;
      }
DMSG("epoll_ctl(%i, EPOLL_CTL_ADD, %i, &ev) returned: %i\n", kdpfd, (*sIt)->getFD(), 0);
      sIt++;
   }
   
#if REALTIME
   /* set to soft real time scheduling */
   memset(&sp, '\0', sizeof(sp));
   sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
   sched_setscheduler(getpid(), SCHED_FIFO, &sp);
#endif


   for(;;) {
      // prepare the select set (it's modified by select directly)
      FD_ZERO(&sfds);
      FD_SET(kdpfd, &sfds);

      // grab the global timeout for use in select
      timeval timeout = Timeout::getTimeout();
      timeval *p_timeout = timeout.tv_sec + timeout.tv_usec > 0 ? &timeout : NULL; // if the timeout is zero, pass a NULL to select
      if (select(kdpfd+1, &sfds, &sfds, NULL, p_timeout) == -1) {
            perror("Server-select() error ");
            return 1;
      }

      // did we timeout?
      if (Timeout::timedOut() == 1) {
         Timeout::handleTimeoutEvents();
         continue;
      }

      // no timeout, handle epoll
      nfds = epoll_wait(kdpfd, events, MAX_EVENTS, -1);
      for(n = 0; n < nfds; ++n) {
         if(events[n].data.ptr == NULL) { // NULL inidicates the listener fd
            addrlen = sizeof(local);
            client = accept(listener, (struct sockaddr *) &local,
                        &addrlen);
            if(client < 0){
               perror("accept");
               continue;
            }
            setnonblocking(client);
            FASIT_TCP *fasit_tcp = new FASIT_TCP(client);
            ev.events = EPOLLIN;
            ev.data.ptr = (void*)fasit_tcp;
            if (epoll_ctl(kdpfd, EPOLL_CTL_ADD, client, &ev) < 0) {
               IERROR("epoll set insertion error: fd=%d\n", client)
               return 1;
            }
            IMSG("Accepted new client %i\n", client)
         } else {
            FASIT_TCP *fasit_tcp = (FASIT_TCP*)events[n].data.ptr;
            int ret = fasit_tcp->handleReady(&events[n]);
            if (ret == -1) {
               delete fasit_tcp;
            }
         }
      }
   }
   return 0;
}
