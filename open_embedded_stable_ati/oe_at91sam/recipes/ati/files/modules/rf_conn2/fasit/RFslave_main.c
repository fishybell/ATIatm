#include <fcntl.h>
#include <signal.h>
#include "mcp.h"
#include "rf.h"
#include "RFslave.h"
#include "rf_debug.h"
#include "smartqueue.c"

const char *__PROGRAM__ = "RFslave.new ";

int verbose = 0;    // so debugging works right in all modules
int last_slot = -1; // last slot used starts at no slot used


struct timespec start_time; // global timers
minion_time_t mt;

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

long getTime() {
   struct timespec tv;
   while (clock_gettime(CLOCK_MONOTONIC,&tv) != 0) {
      usleep(1);
   }
//   DDCMSG(D_TIME, GRAY, "CURRENT TIME: %d from %5i.%03ld", (tv.tv_sec * 1000) + (tv.tv_nsec / 1000000l), tv.tv_sec, tv.tv_nsec / 1000000l);
   return (tv.tv_sec * 1000) + (tv.tv_nsec / 1000000l);
}

void print_help(int exval) { printf("RFslave.new [-h] [-v num] [-f port] [-r port] [-n num]\n\n");
   printf("  -h            print this help and exit\n");
   printf("  -i 127.0.0.1  set RF ip address\n");
   printf("  -p 14004      set RF tcp port\n");
   printf("  -r /dev/ttyS1 set RF tty port\n");
   printf("  -v 0          set verbosity bits\n");
   printf("  -e 0          Run mode number. 0 - normal (default), 1 - with smart queue\n");
   exit(exval);
}

void DieWithError(char *errorMessage){
    char buf[200];
    strerror_r(errno,buf,200);
    DCMSG(RED,"RFslave.new %s %s \n", errorMessage,buf);
    exit(1);
}

void setnonblocking(int fd, int sock_stuff) {
   int opts, yes=1;

   // socket specific, but only the first time we set up a socket
   if (sock_stuff) {
      // disable Nagle's algorithm so we send messages as discrete packets
      if (setsockopt(fd, SOL_SOCKET, TCP_NODELAY, &yes, sizeof(int)) == -1) {
         DCMSG(RED, "Could not disable Nagle's algorithm\n");
         PERROR("setsockopt(TCP_NODELAY)");
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
      DDCMSG(D_MEGA, RED, "mark_ttyWrite: %i", rc->child);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.fd = rc->child;
      ev.events = EPOLLIN | EPOLLOUT;
      epoll_ctl(efd, EPOLL_CTL_MOD, rc->child, &ev);
   }
   if (ret & mark_sockWrite) {
      DDCMSG(D_MEGA, RED, "mark_sockWrite: %i", rc->sock);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.fd = rc->sock;
      ev.events = EPOLLIN | EPOLLOUT;
      epoll_ctl(efd, EPOLL_CTL_MOD, rc->sock, &ev);
   }
   if (ret & mark_ttyRead) { // mark_ttyRead overwrites mark_ttyWrite
      DDCMSG(D_MEGA, RED, "mark_ttyRead: %i", rc->child);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.fd = rc->child;
      ev.events = EPOLLIN;
      epoll_ctl(efd, EPOLL_CTL_MOD, rc->child, &ev);
   }
   if (ret & mark_sockRead) { // mark_sockRead overwrites mark_sockWrite
      DDCMSG(D_MEGA, RED, "mark_sockRead, %i", rc->sock);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.fd = rc->sock;
      ev.events = EPOLLIN;
      epoll_ctl(efd, EPOLL_CTL_MOD, rc->sock, &ev);
   }
   if (ret & rem_ttyEpoll) {
      DDCMSG(D_MEGA, RED, "rem_ttyEpoll: %i", rc->child);
      epoll_ctl(efd, EPOLL_CTL_DEL, rc->child, NULL);
      close(rc->child); // nothing to do if errors...ignore return value from close()
      done = 1; // TODO -- don't be done
   }
   if (ret & rem_sockEpoll) {
      DDCMSG(D_MEGA, RED, "rem_sockEpoll: %i", rc->sock);
      epoll_ctl(efd, EPOLL_CTL_DEL, rc->sock, NULL);
      close(rc->sock); // nothing to do if errors...ignore return value from close()
      done = 1; // TODO -- don't be done
   }
   DDCMSG(D_MEGA, RED, "...end");
   return done;
}

int main(int argc, char **argv) {
   int i, opt, lport, n, fnum=1, done=0, yes=1;
   struct sockaddr_in raddr; // socket address
   struct epoll_event ev, events[MAX_EVENTS]; // temporary event, main event list
   int efd, nfds; // file descriptors for sockets
   char *ttyport = "/dev/ttyS1";
   struct termios newtio; // the new state of the serial device
   rf_connection_t rc;
   int tty_fd_n;
   int sock_fd_n;
   rfpair_t rf_pair;
   int child;
   int smartqueue=0;

   // initialize connection structure
   D_memset(&rc, 0, sizeof(rc));
   rc.timeslot_length = 100; // start with 100 millisecond timeslots
   rc.id_index = -1;
   rc.devid_index = -1;
   rc.devid_last_low = -1;
   rc.devid_last_high = -1;
   rc.packets = -1; // ready for next burst

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
   while ((opt = getopt(argc, argv, "hv:r:f:n:e:")) != -1) {
      switch (opt) {
         case 'h':
            print_help(0);
            break;
         case 'v':
            verbose = strtoul(optarg,NULL,16);
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
         case 'e':
            smartqueue = atoi(optarg);
            break;
         case ':':
            EMSG("Error - Option `%c' needs a value\n\n", optopt);
            print_help(1);
            break;
         case '?':
            EMSG("Error - No such option: `%c'\n\n", optopt);
            print_help(1);
            break;
      }
   }
   DCMSG(BLACK,"RFSLAVE: verbosity is set to 0x%x", verbose);
   DCMSG(BLACK,"RFSLAVE: RF address = %s:%d", inet_ntoa(raddr.sin_addr),htons(raddr.sin_port));
   DCMSG(BLACK,"RFSLAVE: RF tty = %s", ttyport);

   // initialize timers
   clock_gettime(CLOCK_MONOTONIC,&mt.istart_time); // get the intial current time
   timestamp(&mt);   // make sure the delta_time gets set    

   // open a bidirectional pipe for communication with the child
   if (socketpair(AF_UNIX,SOCK_STREAM,0,((int *) &rf_pair.parent))){
      DieWithError("socketpair() failed");
   }

   //   fork a minion
   if ((child = fork()) == -1) {
      DieWithError("fork failed");
   }
   if (child) {
      // install signal handlers
      signal(SIGINT, quitproc);
      signal(SIGQUIT, quitproc);
      // we're the parent
      close(rf_pair.parent);
      rc.child = rf_pair.child;
   } else {
      // install signal handlers
      signal(SIGINT, quitproc);
      signal(SIGQUIT, quitproc);
      // we're the child
      close(rf_pair.child);
      // do child thread here...
      __PROGRAM__ = "RFslave.new:child ";

      //   Okay,   set up the RF modem link here
      rc.tty=open_port(ttyport, 4|2|8);   // ignore brk/cn, blocking and read-only

      if (rc.tty<0) {
         DCMSG(RED,"RFslave.new: comm port could not be opened. Shutting down");
         exit(-1);
      }

      while (!close_nicely) {
         char rbuf[1];
         int gotrf;
         if ((gotrf = read(rc.tty,rbuf,1)) > 0) {
            DDCMSG(D_NEW, GRAY, "Childing pushing to parent %i bytes", gotrf);
            write(rf_pair.parent, rbuf, gotrf);
         } else {
            DDCMSG(D_NEW, RED, "Failed to gather_rf"); 
            perror("on rf read");
            sleep(1);
         }
      }
      exit(0);
   }

	rfpair_t queue_pair;

	if(smartqueue==1) {
		//RFslave forks a child process containing the target-side smartqueue program.
		//For compatibility, rc.sock will attach to the child process
		//and no protocol changes are required. 

		// open a bidirectional pipe for communication with the child
		if (socketpair(AF_UNIX,SOCK_STREAM,0,((int *) &queue_pair.parent))){
			DieWithError("socketpair() failed");
		}

		if ((child = fork()) == -1) DieWithError("fork failed.");

		if (child) {
			/* This is the parent. */
			//install signal handlers
			signal(SIGINT, quitproc);
			signal(SIGQUIT, quitproc);
			// we're the parent
			close(queue_pair.parent);
			rc.sock=queue_pair.child;
		} else {
			/* This is the child. */
			// install signal handlers
			signal(SIGINT, quitproc);
			signal(SIGQUIT, quitproc);
			// we're the child
			close(queue_pair.child);

			//create socket to connect to slaveboss
			if ((rc.sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
				PERROR("Error creating socket: ");
				DieWithError("socket");
			}

			// use the RF_addr structure that was set by default or option arguments
			if (connect(rc.sock, (struct sockaddr *)&raddr, sizeof(raddr)) < 0) {
				PERROR("Error connecting to slaveboss: ");
				DieWithError("connect");
			}

			// do child thread here...
			smartqueue_trg_thread(queue_pair.parent, rc.sock);

			exit(0); // thread should not return
		}
	} else {
		//legacy approach as of 12/2012
		//RFslave.new.arm owns the socket to slaveboss.arm

		//create socket to connect to slaveboss
		if ((rc.sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
			PERROR("Error creating socket: ");
			DieWithError("socket");
		}

		if (connect(rc.sock, (struct sockaddr *)&raddr, sizeof(raddr)) < 0) {
			PERROR("Error connecting to slaveboss: ");
			DieWithError("connect");
		}
	}

	setnonblocking(rc.sock, 1); // socket first time
	DDCMSG(D_MEGA, BLACK,"RFSLAVE: RF socket fd: %i", rc.sock);

    // turn power to high for the radio A/B pin
    rc.tty=open("/sys/class/gpio/export",O_WRONLY,"w");
    write(rc.tty,"39",1);
    close(rc.tty);
    rc.tty=open("/sys/class/gpio/gpio39/direction",O_WRONLY,"w");
    write(rc.tty,"out",3);
    close(rc.tty);
    rc.tty=open("/sys/class/gpio/gpio39/value",O_WRONLY,"w");
    write(rc.tty,"1",1);		// a "0" here would turn on low power
    close(rc.tty);
    rc.tty=open("/sys/class/gpio/unexport",O_WRONLY,"w");
    write(rc.tty,"39",1);		// this lets any kernel modules use the pin from now on
    close(rc.tty);
    
    DCMSG(YELLOW,"A/B set for High power.\n");
    // open tty and setup the serial device
   rc.tty=open_port(ttyport, 4|2|0x10); // bits 4 (IGNBRK | IGNCR), 2 (blocking), and 9 (write-only) on

   if (rc.tty < 0) {
      DieWithError("Is unhappy tty");
   }
   DDCMSG(D_MEGA, BLACK,"RFSLAVE: RF tty fd: %i", rc.tty);

   // setup epoll
   efd = epoll_create(MAX_CONNECTIONS);

   // add socket to epoll
   D_memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN; // only for reading to start
   ev.data.fd = rc.sock; // remember for later
   if (epoll_ctl(efd, EPOLL_CTL_ADD, rc.sock, &ev) < 0) {
      EMSG("epoll socket insertion error");
      close_nicely=1;
   }

   // add child to epoll
   D_memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN; // only for reading to start
   ev.data.fd = rc.child; // remember for later
   if (epoll_ctl(efd, EPOLL_CTL_ADD, rc.child, &ev) < 0) {
      EMSG("epoll child insertion error");
      close_nicely=1;
   }

   // finish initialization of connection structure
   rc.time_start = getTime();
   doTimeAfter(&rc, INFINITE);

   // main loop
   while (!done && !close_nicely) {
      tty_fd_n = -1;
      sock_fd_n = -1;
      int timeout = getTimeout(&rc); // find out timeout...
      DDCMSG(D_MEGA, YELLOW, "RFSLAVE EPOLL WAITING %i msecs", timeout);
      // wait for something to happen
      nfds = epoll_wait(efd, events, MAX_EVENTS, timeout); // ...and pass it directly in

      // timeout?
      if (nfds == 0) {
         // timed out, either wait for a while, or do everything now
         timeout = getTimeout(&rc);
         if (timeout >= 10) { // long timeout (10 milliseconds) still exists
            DDCMSG(D_MEGA, YELLOW, "Epoll timed out, going back...");
         
            continue; // ...so come back and wait the rest of the time right before tty writing
         } else { // no significant timeout exists
            // reset timer now
            handleRet(mark_ttyWrite, &rc, efd); // we want the tty to be available for writing...
            rc.time_start = rc.timeout_end; // reset to latest time
            rc.timeout_start = rc.timeout_end; // reset to latest time
            continue; // ...so come back and wait the rest of the time right before tty writing
         }
      }
      
      // parse all waiting connections
      for (n = 0; !done && !close_nicely && n < nfds; n++) {
         DDCMSG(D_MEGA, YELLOW, "Looking at %i in events...", n);
         if (events[n].data.fd == rc.child) { // Read/Write from child
            tty_fd_n = n;
         } else if (events[n].data.fd == rc.sock) { // Read/Write from socket
            sock_fd_n = n;
         } else {
            DDCMSG(D_MEGA, YELLOW, "events[%i].events: %i unknown: %i", n, events[n].events, events[n].data.fd);
            done = 1; // exit
         }
      }

      if (tty_fd_n != -1) { // Read/Write from child/tty (always, even if socket is ready)
         n = tty_fd_n;
         DDCMSG(D_MEGA, YELLOW, "events[%i].events: %i child", n, events[n].events);

         // closed socket?
         if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP) {
            // client closed, shutdown
            done = 1;
         // writing?
         } else if (events[n].events & EPOLLOUT) {
            // rcWrite will push the data as quickly as possible
            done = handleRet(rcWrite(&rc, 1),&rc, efd); // 1 for tty
      //DCMSG(RED, "clearTxQ @ %s:%i", __FILE__, __LINE__);
            clearTxQ(&rc);
         // reading?
         } else if (events[n].events & EPOLLIN || events[n].events & EPOLLPRI) {
            //DDCMSG(D_POINTER, MAGENTA, "\n---------------------------\nTTY READ\n---------------------------");
            if (rc.packets < 0) { // is this the start of a fresh burst?
               DDCMSG(D_TIME, YELLOW, "---------------------------START OF BURST!!!");
               // grab now time
               rc.nowt = getTime();
               rc.packets = 0; // in burst now
            }

            int ret = rcRead(&rc, 1); // 1 for child
            if (ret != doNothing) {
               done = handleRet(ret, &rc, efd); // this 
            } else {
               // tty2sock will transfer the data from the child to the socket and handle as necessary
               done = handleRet(tty2sock(&rc), &rc, efd);
               
               // handle socket write immediately
               if (done & mark_sockWrite) {
                  // move to socket
                  done ^- mark_sockWrite; // un-mark socket write
                  done |= mark_sockRead; // continue un-marking
                  done |= handleRet(sock2tty(&rc), &rc, efd);
               }

               if (rc.packets <= 0) {
                  DDCMSG(D_TIME, YELLOW, "---------------------------END OF BURST!!!");
                  rc.packets = -1; // signal ready for next burst
               }
            }
         }
      } else if (sock_fd_n != -1) { // Read/Write from socket (only if child/tty isn't ready)
         n = sock_fd_n;
         DDCMSG(D_MEGA, YELLOW, "events[%i].events: %i socket", n, events[n].events);

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
            int ret = rcRead(&rc, 0); // 0 for socket
            if (ret != doNothing) {
               done = handleRet(ret, &rc, efd); // this 
            } else {
               // sock2tty will transfer the data from the tty to the socket and handle as necessary
               done = handleRet(sock2tty(&rc), &rc, efd);
            }
         }
      }
   }

   DCMSG(BLACK,"RFslave.new says goodbye...");

   // close child, tty and socket
   close(rc.child);
   close(rc.tty);
   close(rc.sock);

   return 0;
}

#ifdef DEBUG_MEM
void *__D_memcpy(void *dest, const void *src, size_t n, char* f, int l) {
   DDCMSG(D_MEGA, black, "memcpy(%8p, %8p, %i) @ %s:%i", dest, src, n, f, l);
   return memcpy(dest,src,n);
}
void *__D_memset(void *s, int c, size_t n, char* f, int l) {
   DDCMSG(D_MEGA, black, "memset(%8p, %i, %i) @ %s:%i", s, c, n, f, l);
   return memset(s,c,n);
}
#endif

