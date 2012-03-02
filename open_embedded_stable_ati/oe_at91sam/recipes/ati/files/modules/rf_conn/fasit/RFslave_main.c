#include <fcntl.h>
#include <signal.h>
#include "mcp.h"
#include "rf.h"
#include "RFslave.h"
#include "rf_debug.h"

int verbose = 0;    // so debugging works right in all modules
int last_slot = -1; // last slot used starts at no slot used

#define MAX_EVENTS 16
#define BAUDRATE B19200

// kill switch to program
static int close_nicely = 0;
static void quitproc(int sig) {
   switch (sig) {
      case SIGINT:
         DCMSG(red,"Caught signal: SIGINT\n");
         break;
      case SIGQUIT:
         DCMSG(red,"Caught signal: SIGQUIT\n");
         break;
      default:
         DCMSG(red,"Caught signal: %i\n", sig);
         break;
   }
   close_nicely = 1;
}

void print_help(int exval) { printf("slaveboss [-h] [-v num] [-f port] [-r port] [-n num]\n\n");
   printf("  -h            print this help and exit\n");
   printf("  -i 127.0.0.1  set RF ip address\n");
   printf("  -p 14004      set RF tcp port\n");
   printf("  -r /dev/ttyS1 set RF tty port\n");
   printf("  -v 0          set verbosity bits\n");
   exit(exval);
}

void DieWithError(char *errorMessage){
    char buf[200];
    strerror_r(errno,buf,200);
    DCMSG(RED,"slaveboss %s %s \n", errorMessage,buf);
    exit(1);
}

void setnonblocking(int fd, int sock_stuff) {
   int opts, yes=1;

   // socket specific, but only the first time we set up a socket
   if (sock_stuff) {
      // disable Nagle's algorithm so we send messages as discrete packets
      if (setsockopt(fd, SOL_SOCKET, TCP_NODELAY, &yes, sizeof(int)) == -1) {
         DCMSG(RED, "Could not disable Nagle's algorithm\n");
         perror("setsockopt(TCP_NODELAY)");
      }

      if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int)) < 0) { // set keepalive so we disconnect on link failure or timeout
         DieWithError("setsockopt(SO_KEEPALIVE)");
      }
   }

   // generic file descriptor setup
   opts = fcntl(fd, F_GETFL); // grab existing flags
   if (opts < 0) {
      DieWithError("fcntl(F_GETFL)");
   }
   opts = (opts | O_NONBLOCK); // add in nonblock to existing flags
   if (fcntl(fd, F_SETFL, opts) < 0) {
      DieWithError("fcntl(F_SETFL)");
   }
}

void setblocking(int fd) {
   int opts, yes=1;

   // generic file descriptor setup
   opts = fcntl(fd, F_GETFL); // grab existing flags
   if (opts < 0) {
      DieWithError("fcntl(F_GETFL)");
   }
   opts = (opts ^ O_NONBLOCK); // remove nonblock from existing flags
   if (fcntl(fd, F_SETFL, opts) < 0) {
      DieWithError("fcntl(F_SETFL)");
   }
}

// handle the return value from tty2sock or sock2tty
int handleRet(int ret, rf_connection_t *rc, int efd) {
   struct epoll_event ev; // temp event
   int done = 0;
   if (ret == doNothing) { return done; }
   if (ret & mark_ttyWrite) {
      DCMSG(RED, "mark_ttyWrite: %i", rc->tty);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.fd = rc->tty;
      ev.events = EPOLLIN | EPOLLOUT;
      epoll_ctl(efd, EPOLL_CTL_MOD, rc->tty, &ev);
   }
   if (ret & mark_sockWrite) {
      DCMSG(RED, "mark_sockWrite: %i", rc->sock);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.ptr = rc;
      ev.events = EPOLLIN | EPOLLOUT;
      epoll_ctl(efd, EPOLL_CTL_MOD, rc->sock, &ev);
   }
   if (ret & mark_ttyRead) { // mark_ttyRead overwrites mark_ttyWrite
      DCMSG(RED, "mark_ttyRead: %i", rc->tty);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.fd = rc->tty;
      ev.events = EPOLLIN;
      epoll_ctl(efd, EPOLL_CTL_MOD, rc->tty, &ev);
   }
   if (ret & mark_sockRead) { // mark_sockRead overwrites mark_sockWrite
      DCMSG(RED, "mark_sockRead, %i", rc->sock);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.ptr = rc;
      ev.events = EPOLLIN;
      epoll_ctl(efd, EPOLL_CTL_MOD, rc->sock, &ev);
   }
   if (ret & rem_ttyEpoll) {
      DCMSG(RED, "rem_ttyEpoll: %i", rc->tty);
      epoll_ctl(efd, EPOLL_CTL_DEL, rc->tty, NULL);
      close(rc->tty); // nothing to do if errors...ignore return value from close()
      done = 1; // TODO -- don't be done
   }
   if (ret & rem_sockEpoll) {
      DCMSG(RED, "rem_sockEpoll: %i", rc->sock);
      epoll_ctl(efd, EPOLL_CTL_DEL, rc->sock, NULL);
      close(rc->sock); // nothing to do if errors...ignore return value from close()
      done = 1; // TODO -- don't be done
   }
   DCMSG(RED, "...end");
   return done;
}

int main(int argc, char **argv) {
   int i, opt, lport, n, fnum=1, done=0, yes=1;
   struct sockaddr_in raddr; // socket address
   struct epoll_event ev, events[MAX_EVENTS]; // temporary event, main event list
   int efd, nfds; // file descriptors for sockets
   char *ttyport = "/dev/ttyS1";
   struct termios oldtio; // the original state of the serial device
   struct termios newtio; // the new state of the serial device
   rf_connection_t rc;

   // initialize connection structure
   D_memset(&rc, 0, sizeof(rc));
   rc.timeslot_length = 100; // start with 100 millisecond timeslots
   rc.id_index = -1;
   rc.devid_index = -1;
   rc.devid_last_low = -1;
   rc.devid_last_high = -1;

   // initialize addresses
   D_memset(&raddr, 0, sizeof(struct sockaddr_in));
   raddr.sin_family = AF_INET;
   inet_aton("127.0.0.1", &raddr.sin_addr);
   raddr.sin_port = htons(14004);               // Connect port
   verbose=0;

   // process the arguments
   //    -i 127.0.0.1  set RF ip address
   //    -p 14004      set RF tcp port
   //    -r /dev/ttyS1 set RF tty port
   //    -v 0          set verbosity bits
   while ((opt = getopt(argc, argv, "hv:r:f:n:")) != -1) {
      switch (opt) {
         case 'h':
            print_help(0);
            break;
         case 'v':
            verbose = atoi(optarg);
            break;
         case 'i':
            inet_aton(optarg, &raddr.sin_addr);
            break;
         case 'p':
            raddr.sin_port = htons(atoi(optarg));
            break;
         case 't':
            strcpy(ttyport,optarg);
            break;
         case ':':
            fprintf(stderr, "Error - Option `%c' needs a value\n\n", optopt);
            print_help(1);
            break;
         case '?':
            fprintf(stderr, "Error - No such option: `%c'\n\n", optopt);
            print_help(1);
            break;
      }
   }
   DCMSG(BLACK,"RFSLAVE: verbosity is set to 0x%x", verbose);
   DCMSG(BLACK,"RFSLAVE: RF address = %s:%d", inet_ntoa(raddr.sin_addr),htons(raddr.sin_port));
   DCMSG(BLACK,"RFSLAVE: RF tty = %s", ttyport);

   // connect to slaveboss
   if ((rc.sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
      perror("Error creating socket: ");
      DieWithError("socket");
   }
   if (connect(rc.sock, (struct sockaddr *)&raddr, sizeof(raddr)) < 0) {
      perror("Error connecting to slaveboss: ");
      DieWithError("connect");
   }
   setnonblocking(rc.sock, 1); // socket first time

   // open tty and setup the serial device (copy the old settings to oldtio)
   rc.tty = open(ttyport, O_RDWR | O_NOCTTY);
   if (rc.tty < 0) {
      perror("Error opening tty port: ");
      DieWithError("open");
   }
   tcgetattr(rc.tty, &oldtio);
   D_memset(&newtio, 0, sizeof(newtio));
   newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;
   newtio.c_iflag = 0;
   newtio.c_oflag = 0;
   newtio.c_lflag = 0;
   newtio.c_cc[VTIME] = 0;     // inter-character timer unused
   newtio.c_cc[VMIN] = 1;      // read a minimum of 1 character
   tcflush(rc.tty, TCIFLUSH);
   tcsetattr(rc.tty, TCSANOW, &newtio);
   setnonblocking(rc.tty, 0); // not a socket

   // setup epoll
   efd = epoll_create(MAX_CONNECTIONS);

   // add socket to epoll
   D_memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN; // only for reading to start
   ev.data.fd = rc.sock; // remember for later
   if (epoll_ctl(efd, EPOLL_CTL_ADD, rc.sock, &ev) < 0) {
      fprintf(stderr, "epoll socket insertion error");
      close_nicely=1;
   }

   // add tty to epoll
   D_memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN; // only for reading to start
   ev.data.fd = rc.tty; // remember for later
   if (epoll_ctl(efd, EPOLL_CTL_ADD, rc.tty, &ev) < 0) {
      fprintf(stderr, "epoll tty insertion error");
      close_nicely=1;
   }

   // finish initialization of connection structure
   clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&rc.time_start);
   doTimeAfter(&rc, INFINITE);

   // main loop
   while (!done && !close_nicely) {
      DCMSG(YELLOW, "EPOLL WAITING");
      // wait for something to happen
      nfds = epoll_wait(efd, events, MAX_EVENTS, getTimeout(&rc)); // find out timeout and pass it directly in

      // timeout?
      if (nfds == 0) {
         handleRet(mark_ttyWrite, &rc, efd); // we want the tty to be available for writing...
         continue; // ...so come back and wait the rest of the time right before tty writing
      }
      
      // parse all waiting connections
      for (n = 0; !done && !close_nicely && n < nfds; n++) {
         if (events[n].data.fd == rc.tty) { // Read/Write from tty
            DCMSG(YELLOW, "events[%i].events: %i tty", n, events[n].events);

            // closed socket?
            if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP) {
               // client closed, shutdown
               done = 1;
            // writing?
            } else if (events[n].events & EPOLLOUT) {
               // wait the rest of the timeout time
               waitRest(&rc);

               // rcWrite will push the data as quickly as possible
               done = handleRet(rcWrite(&rc, 1),&rc, efd); // 1 for tty
            // reading?
            } else if (events[n].events & EPOLLIN || events[n].events & EPOLLPRI) {
               int ret = rcRead(&rc, 1); // 1 for tty
               if (ret != doNothing) {
                  done = handleRet(ret, &rc, efd); // this 
               } else {
                  // tty2sock will transfer the data from the tty to the socket and handle as necessary
                  done = handleRet(tty2sock(&rc), &rc, efd);
               }
            }
         } else if (events[n].data.fd == rc.sock) { // Read/Write from socket
            DCMSG(YELLOW, "events[%i].events: %i socket", n, events[n].events);

            // closed socket?
            if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP) {
               // close by handling like a rem_sockEpoll
               done = handleRet(rem_sockEpoll, &rc, efd);
            // writing?
            } else if (events[n].events & EPOLLOUT) {
               // rcWrite will push the data as quickly as possible
               done = handleRet(rcWrite(&rc, 0), &rc, efd); // 0 for socket
            // reading?
            } else if (events[n].events & EPOLLIN || events[n].events & EPOLLPRI) {
               int ret = rcRead(&rc, 1); // 1 for tty
               if (ret != doNothing) {
                  done = handleRet(ret, &rc, efd); // this 
               } else {
                  // sock2tty will transfer the data from the tty to the socket and handle as necessary
                  done = handleRet(sock2tty(&rc), &rc, efd);
               }
            }
         }
      }
   }

   // reset serial device
   if (rc.tty > 0) {
      tcsetattr(rc.tty, TCSANOW, &oldtio);
   }

   // close tty and socket
   close(rc.tty);
   close(rc.sock);

   return 0;
}

#ifdef DEBUG_MEM
void *__D_memcpy(void *dest, const void *src, size_t n, char* f, int l) {
   DCMSG(black, "memcpy(%8p, %8p, %i) @ %s:%i", dest, src, n, f, l);
   return memcpy(dest,src,n);
}
void *__D_memset(void *s, int c, size_t n, char* f, int l) {
   DCMSG(black, "memset(%8p, %i, %i) @ %s:%i", s, c, n, f, l);
   return memset(s,c,n);
}
#endif
