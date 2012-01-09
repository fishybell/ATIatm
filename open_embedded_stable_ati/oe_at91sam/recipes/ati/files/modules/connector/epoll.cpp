#include <sys/epoll.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sched.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <netinet/tcp.h>

using namespace std;

// Sorry Nate, I can't get it to work for me with these as statics declaired in common.h
volatile int C_TRACE;
volatile int C_DEBUG;
volatile int C_INFO;
volatile int C_ERRORS;
volatile int C_KERNEL = 0;

volatile int KERN_ROLE = 0;

#include "connection.h"
#include "tcp_factory.h"
#include "common.h"
#include "timers.h"
#include "timeout.h"
#include "kernel_tcp.h"
#include "scenario.h"

// rough idea of how many connections we'll deal with and the max we'll deal with in a single loop
#define MAX_CONNECTIONS 2048
#define MAX_EVENTS 16

// tcp port we'll listen to for new connections
#define PORT 4466

// define REALTIME as 1 or 0 to enable or disable running as a realtime process
#define REALTIME 0

// kill switch to program
static int close_nicely = 0;
static void quitproc(int sig) {
   switch (sig) {
      case SIGINT:
         IMSG("Caught signal: SIGINT\n");
         break;
      case SIGQUIT:
         IMSG("Caught signal: SIGQUIT\n");
         break;
      default:
         IMSG("Caught signal: %i\n", sig);
         break;
   }
   close_nicely = 1;
}

// utility function to properly configure a client TCP connection
void setnonblocking(int sock, bool socket) {
FUNCTION_START("setnonblocking(int sock, bool socket)")
   int opts, yes=1;

   // socket specific setup
   if (socket) {
      // disable Nagle's algorithm so we send messages as discrete packets
      if (setsockopt(sock, SOL_SOCKET, TCP_NODELAY, &yes, sizeof(int)) == -1) {
         IERROR("Could not disable Nagle's algorithm\n");
         perror("setsockopt(TCP_NODELAY)");
      }

      if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int)) < 0) { // set keepalive so we disconnect on link failure or timeout
         perror("setsockopt(SO_KEEPALIVE)");
         exit(EXIT_FAILURE);
      }
   }

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
FUNCTION_END("setnonblocking(int sock, bool socket)")
}

/**********************************
*          Main Function          *
**********************************/

int main(int argc, char *argv[]) {
PROG_START

   struct epoll_event ev, events[MAX_EVENTS];
   int client, listener, kdpfd; // file descriptors
   int n, nfds, yes=1;
   socklen_t addrlen;
   struct sockaddr_in serveraddr, local;
   struct sched_param sp;
   TCP_Factory *factory;

   // install signal handlers
   signal(SIGINT, quitproc);
   signal(SIGQUIT, quitproc);

   /* parse argv for command line arguments: */
   int cport = PORT;
   int sport = PORT;
   int base = 0;
   const char *defIP = "192.168.0.1";
   bool startClient = false;
   bool standAlone = false;
   
   const char *roles[] = {
      "UNSPECIFIED",
      "LIFTER",
      "MOVER",
      "SOUND",
      "GUNNER",
      "DRIVER",
   };
const char *usage = "Usage: %s [options]\n\
\t-l X   -- listen on port X rather than the default \n\
\t-p X   -- connect to port X rather than the default \n\
\t-i X   -- connect to IP address X\n\
\t-C     -- initiate a client connection\n\
\t-S     -- initiate a standalone client\n\
\t-r X   -- handling role X\n\
\t-v     -- Enable ERROR messages\n\
\t-vv    -- Enable ERROR, INFO messages\n\
\t-vvv   -- Enable ERROR, INFO, DEBUG messages\n\
\t-vvvv  -- Enable ERROR, INFO, DEBUG, TRACE messages\n\
\t-h     -- print out usage information\n";


   for (int i = 1; i < argc; i++) {
      if (argv[i][0] != '-') {
         IERROR("invalid argument (%i)\n", i)
         return 1;
      }
      switch (argv[i][1]) {
         case 'S' :
            standAlone = true;
            break;
         case 'C' :
            startClient = true;
            break;
         case 'l' :
            if (sscanf(argv[++i], "%i", &sport) != 1) {
               IERROR("invalid argument (%i)\n", i)
               return 1;
            }
            break;
         case 'p' :
            if (sscanf(argv[++i], "%i", &cport) != 1) {
               IERROR("invalid argument (%i)\n", i)
               return 1;
            }
            break;
         case 'r' :
            // check integer value first
            if (sscanf(argv[++i], "%i", &KERN_ROLE) != 1) {
               int found = 0;
               // check string value second
               for (int j=0; j<sizeof(roles)/sizeof(char*); j++) {
                  if (strncmp(roles[j], argv[i], strlen(roles[j])) == 0) {
                     found = 1;
                     KERN_ROLE = j;
                     break;
                  }
               }
               if (!found) {
                  KERN_ROLE = R_MAX;
               }
            }
            // verify valid number last
            if (KERN_ROLE == 0 || KERN_ROLE >= R_MAX) {
               IERROR("invalid argument (%s)\n", argv[i])
               return 1;
            }
            DMSG("Found Role: %s\n", roles[KERN_ROLE]);
            break;
         case 'i' :
            base = ++i; // remember the ip address parameter index for later
            break;
         case 'h' :
            printf(usage, argv[0]);
            return 0;
            break;
         case 'v' :
            C_ERRORS=1; // enable ERROR output
            if (argv[i][2] == 'v') {
               C_INFO=1; // enable INFO output
               if (argv[i][3] == 'v') {
                  C_DEBUG=1; // enable DEBUG output
                  if (argv[i][4] == 'v') {
                     C_TRACE=1; // enable TRACE output
                  }
               }
            }
            break;
         case 'k' :
            C_KERNEL=1; // enable output to kernel
           break;
         case '-' : // for --help
            if (argv[i][2] == 'h') {
               printf(usage, argv[0]);
               return 0;
            } // fall through
         default :
            IERROR("invalid argument (%i)\n", i)
            return 1;
      }
   }

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
   serveraddr.sin_port = htons(sport);
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

   /* set up polling */
   kdpfd = epoll_create(MAX_CONNECTIONS);
   memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN;
   // listen to the listener
   ev.data.ptr = NULL; // indicates listener fd
   if (epoll_ctl(kdpfd, EPOLL_CTL_ADD, listener, &ev) < 0) {
      IERROR("epoll listener insertion error: fd=%d\n", listener)
      return 1;
   }

   /* start the factory */
   if (base != 0) {
      factory = new TCP_Factory(argv[base], cport); // parameter based IP address
   } else {
      factory = new TCP_Factory(defIP, cport); // default IP address
   }
   Connection::Init(factory, kdpfd);

#if REALTIME
   /* set to soft real time scheduling */
   memset(&sp, '\0', sizeof(sp));
   sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
   sched_setscheduler(getpid(), SCHED_FIFO, &sp);
#endif

   // start any clients here
   Kernel_TCP *kern_client = NULL;
   if (startClient) {
      // start client
      kern_client = factory->newConn <Kernel_TCP> ();
   } else if (standAlone) {
      // start stand-alone client
      int fd = open("/dev/null", O_WRONLY);
      kern_client = new Kernel_TCP(fd, factory->findNextTnum());
      DMSG("Created Stand Alone connection %i with tnum %i\n", kern_client->getFD(), kern_client->getTnum());
      
      factory->addToEPoll(kern_client->getFD(), kern_client);
   }

   while(!close_nicely) {
      // epoll_wait() blocks until we timeout or one of the file descriptors is ready
      int msec_t = Timeout::timeoutVal();
DMSG("epoll_wait with %i timeout\n", msec_t);
      nfds = epoll_wait(kdpfd, events, MAX_EVENTS, msec_t);

      // did we timeout?
      switch (Timeout::timedOut()) {
         case 0 :
            if (msec_t != -1) {
               if (nfds == 0) { DMSG("didn't timeout after %i milliseconds\n", msec_t) continue; }
            }
            break;
         case 1 :
            Timeout::handleTimeoutEvents();
            continue;
      }

      for(n = 0; n < nfds; ++n) {
         if(events[n].data.ptr == NULL) { // NULL inidicates the listener fd
            addrlen = sizeof(local);
            client = accept(listener, (struct sockaddr *) &local,
                        &addrlen);
            if(client < 0){
               perror("accept");
               continue;
            }
            setnonblocking(client, true); // socket
            // connect new client to kernel and add to epoll
            Kernel_TCP *kern_tcp = new Kernel_TCP(client);
            ev.events = EPOLLIN;
            ev.data.ptr = (void*)kern_tcp;
            if (epoll_ctl(kdpfd, EPOLL_CTL_ADD, client, &ev) < 0) {
               IERROR("epoll set insertion error: fd=%d\n", client)
               return 1;
            }
            IMSG("Accepted new client %i\n", client)
         } else {
            Connection *conn = (Connection*)events[n].data.ptr;
            int ret = conn->handleReady(&events[n]);
            if (ret == -1) {
               // attempt reconnection
               if (!conn->reconnect()) {
                  delete conn;
               }
            }
         }
      }
   }

   // properly any clients here
   if (kern_client != NULL) {
      delete kern_client;
   }

   return 0;
}
