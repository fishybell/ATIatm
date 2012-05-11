#include <signal.h>
#include "mcp.h"
#include "rf.h"
#include "slaveboss.h"
#include "rf_debug.h"

int verbose = 0;    // so debugging works right in all modules
int added_rf_to_epoll = 0; // whether or not we've connect the RF device
int last_slot = -1; // last slot used starts at no slot used
fasit_connection_t fconns[MAX_CONNECTIONS];

#define MAX_EVENTS 16 /* for epoll */

// global hit logging routines
void log_ResetHits(fasit_connection_t *fc) {
   int i;
   DDCMSG(D_PACKET, RED, "Reset hits for %i", fc->current_event);
   for (i = 0; i < MAX_HITS; i++) {
      if (fc->hit_times[i].event == fc->current_event) {
         D_memset(&fc->hit_times[i], 0, sizeof(hit_event_t));
      }
   }
   fc->hits_per_event[fc->current_event] = 0;
}

void log_NewHits(fasit_connection_t *fc, int new_hits) {
   // got new hits
   struct timespec tv;
   int i=0;
   clock_gettime(CLOCK_MONOTONIC,&tv);
   DDCMSG(D_PACKET, YELLOW, "Remembering %i new hits", new_hits);
   
   // log each hit time individually
   while (new_hits-- > 0) {
      if (++fc->hits_per_event[fc->current_event] <= 127) {
         for (/* don't reset i */; i < MAX_HITS; i++) {
            if (fc->hit_times[i].event == 0 &&
                fc->hit_times[i].time.tv_sec == 0 &&
                fc->hit_times[i].time.tv_nsec == 0l) {
               DCMSG(BLACK, "Setting %5ld.%03ld in fc->hit_times[%i] with event %i", tv.tv_sec, tv.tv_nsec/1000000l, i, fc->current_event);
               fc->hit_times[i].time = tv;
               fc->hit_times[i].event = fc->current_event;
               break; // move on to next hit
            }
         }
      }
   }
}

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
   printf("  -f 14000      set FASIT listen port\n");
   printf("  -r 14004      set RF listen port\n");
   printf("  -v 0          set verbosity bits\n");
   printf("  -n 1          set number of FASIT connections to wait for before parsing RF\n");
   exit(exval);
}

void DieWithError(char *errorMessage){
    char buf[200];
    strerror_r(errno,buf,200);
    DCMSG(RED,"slaveboss %s %s \n", errorMessage,buf);
    exit(1);
}

void setnonblocking(int sock) {
   int opts, yes=1;

   // disable Nagle's algorithm so we send messages as discrete packets
   if (setsockopt(sock, SOL_SOCKET, TCP_NODELAY, &yes, sizeof(int)) == -1) {
      DDCMSG(D_MEGA, RED, "Could not disable Nagle's algorithm\n");
      perror("setsockopt(TCP_NODELAY)");
   }

   if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int)) < 0) { // set keepalive so we disconnect on link failure or timeout
      DieWithError("setsockopt(SO_KEEPALIVE)");
   }

   // generic file descriptor setup
   opts = fcntl(sock, F_GETFL); // grab existing flags
   if (opts < 0) {
      DieWithError("fcntl(F_GETFL)");
   }
   opts = (opts | O_NONBLOCK); // add in nonblock to existing flags
   if (fcntl(sock, F_SETFL, opts) < 0) {
      DieWithError("fcntl(F_SETFL)");
   }
}

// handle the return value from rf2fasit or fasit2rf
int handleRet(int ret, fasit_connection_t *fc, int efd) {
   struct epoll_event ev; // temp event
   int done = 0;
   DDCMSG(D_MEGA, RED, "start...%i[%i]: %08X...", ret, fc->index, fc);
   if (ret == doNothing) { return done; }
   if (ret & mark_rfWrite) {
      DDCMSG(D_MEGA, RED, "mark_rfWrite: %i", fc->rf);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.fd = fc->rf;
      ev.events = EPOLLIN | EPOLLOUT;
      epoll_ctl(efd, EPOLL_CTL_MOD, fc->rf, &ev);
   }
   if (ret & mark_fasitWrite) {
      DDCMSG(D_MEGA, RED, "mark_fasitWrite: %i", fc->fasit);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.ptr = fc;
      ev.events = EPOLLIN | EPOLLOUT;
      epoll_ctl(efd, EPOLL_CTL_MOD, fc->fasit, &ev);
   }
   if (ret & mark_rfRead) { // mark_rfRead overwrites mark_rfWrite
      DDCMSG(D_MEGA, RED, "mark_rfRead: %i", fc->rf);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.fd = fc->rf;
      ev.events = EPOLLIN;
      epoll_ctl(efd, EPOLL_CTL_MOD, fc->rf, &ev);
   }
   if (ret & mark_fasitRead) { // mark_fasitRead overwrites mark_fasitWrite
      DDCMSG(D_MEGA, RED, "mark_fasitRead, %i", fc->fasit);
      D_memset(&ev, 0, sizeof(ev));
      ev.data.ptr = fc;
      ev.events = EPOLLIN;
      epoll_ctl(efd, EPOLL_CTL_MOD, fc->fasit, &ev);
   }
   if (ret & rem_rfEpoll) {
      DDCMSG(D_MEGA, RED, "rem_rfEpoll: %i", fc->rf);
      epoll_ctl(efd, EPOLL_CTL_DEL, fc->rf, NULL);
         perror("errno: ");
      close(fc->rf); // nothing to do if errors...ignore return value from close()
         //exit(1);
      DCMSG(RED, "Client closed down @ line %s:%i", __FILE__, __LINE__);
      done = 1; // TODO -- don't be done, accept again on rfclient
   }
   if (ret & rem_fasitEpoll) {
      DDCMSG(D_MEGA, RED, "rem_fasitEpoll: %i", fc->fasit);
      int index = fc->index;
      epoll_ctl(efd, EPOLL_CTL_DEL, fc->fasit, NULL);
         perror("errno: ");
      close(fc->fasit); // nothing to do if errors...ignore return value from close()
         //exit(1);
      D_memset(fc, 0, sizeof(fasit_connection_t)); // reset to blank
      if (index == last_slot) {
         last_slot--; // move back last slot if we were the last
      }
   }
   if (ret & add_rfEpoll) {
      // add RF client connection to epoll now that we're ready
      D_memset(&ev, 0, sizeof(ev));
      ev.events = EPOLLIN; // only for reading to start
      ev.data.fd = fc->rf;
DDCMSG(D_MEGA, GREEN,"ADDING %i TO EPOLL(rfclient)", fc->rf);
      if (epoll_ctl(efd, EPOLL_CTL_ADD, fc->rf, &ev) < 0) {
         DDCMSG(D_MEGA, RED, "slaveboss epoll add rfclient failed");
         DCMSG(RED, "Client closed down @ line %s:%i", __FILE__, __LINE__);
         done = 1;
      }
   }
   DDCMSG(D_MEGA, RED, "...end");
   return done;
}

int main(int argc, char **argv) {
   int i, opt, lport, n, fnum=1, done=0, yes=1;
   struct sockaddr_in faddr, raddr; // fasit and rf addresses
   struct epoll_event ev, events[MAX_EVENTS]; // temporary event, main event list
   int fasitsock, rfsock, efd, nfds; // file descriptors for sockets
   int flen = sizeof(faddr);

   // install signal handlers
   signal(SIGINT, quitproc);
   signal(SIGQUIT, quitproc);

   // initialize fasit/rf connection array
   D_memset(fconns, 0, sizeof(fconns));

   // initialize addresses
   D_memset(&faddr, 0, sizeof(struct sockaddr_in));
   D_memset(&raddr, 0, sizeof(struct sockaddr_in));
   faddr.sin_family = AF_INET;
   raddr.sin_family = AF_INET;
   faddr.sin_addr.s_addr = htonl(INADDR_ANY);   // Any incoming interface
   raddr.sin_addr.s_addr = htonl(INADDR_ANY);   // Any incoming interface
   faddr.sin_port = htons(14000);               // Listen port
   raddr.sin_port = htons(14004);               // Listen port
   verbose=0;

   // process the arguments
   //  -r 14004  RF listen port number
   //  -f 14000  FASIT listen port number
   //  -v 1     Verbosity level
   while ((opt = getopt(argc, argv, "hv:r:f:n:")) != -1) {
      switch (opt) {
         case 'h':
            print_help(0);
            break;
         case 'v':
            verbose = strtoul(optarg,NULL,16);
            break;
         case 'n':
            fnum = atoi(optarg);
            if (fnum < 1) {
               DieWithError("Invalid value for n: stay positive");
            }
            break;
         case 'r':
            raddr.sin_port = htons(atoi(optarg));
            break;
         case 'f':
            faddr.sin_port = htons(atoi(optarg));
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
   DCMSG(BLACK,"SLAVEBOSS: verbosity is set to 0x%x", verbose);
   DCMSG(BLACK,"SLAVEBOSS: listen FASIT address = %s:%d", inet_ntoa(faddr.sin_addr),htons(faddr.sin_port));
   DCMSG(BLACK,"SLAVEBOSS: listen RF address = %s:%d", inet_ntoa(raddr.sin_addr),htons(raddr.sin_port));
   print_verbosity_bits();

   // setup listening for RF client
   if ((rfsock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	   DieWithError("socket() failed");
   }
   if (bind(rfsock, (struct sockaddr *)&raddr, sizeof(raddr)) < 0) {
      DieWithError("bind failed");
   }
   if (listen(rfsock, 2) < 0) {
	   DieWithError("listen() failed");
   }
   if(setsockopt(rfsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
	   DieWithError("setsockopt(SO_REUSEADDR) failed");
   }

   // setup epoll
   efd = epoll_create(MAX_CONNECTIONS);

   // main loop
   int rfclient;
   while (!done && !close_nicely) {
      // wait for a single RF client
      struct sockaddr_in caddr; // client address
      DDCMSG(D_MEGA, YELLOW, "WAITING FOR RF ACCEPT");
      rfclient = accept(rfsock, (struct sockaddr *) &caddr, &flen);
      DDCMSG(D_MEGA, BLACK,"SLAVEBOSS: client RF address = %s:%d", inet_ntoa(caddr.sin_addr),htons(caddr.sin_port));
      if (rfclient <= 0) {
         DDCMSG(D_MEGA, RED, "slaveboss accept() failed");
         perror("errno: ");
         exit(1);
         continue;
      }
      setnonblocking(rfclient);
      DDCMSG(D_MEGA, YELLOW, "RF ACCEPTED %i", rfclient);

      // setup listening for FASIT client
      if ((fasitsock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
         DieWithError("socket() failed");
      }
      if (bind(fasitsock, (struct sockaddr *)&faddr, sizeof(faddr)) < 0) {
         DieWithError("bind failed");
      }
      if (listen(fasitsock, 2) < 0) {
         DieWithError("listen() failed");
      }

      // add fasit listener to epoll
      D_memset(&ev, 0, sizeof(ev));
      ev.events = EPOLLIN; // only for reading to start
      // listen to the fasit socket always
      ev.data.fd = fasitsock; // remember for later
      DDCMSG(D_MEGA, GREEN,"ADDING %i TO EPOLL(fasitsock)", fasitsock);
      if (epoll_ctl(efd, EPOLL_CTL_ADD, fasitsock, &ev) < 0) {
         DieWithError("epoll listener insertion error");
      }

     
      // after accepting RF client, accept any number of FASIT clients and parse
      //    the messages back and forth
      while (!done && !close_nicely) {
         DDCMSG(D_MEGA, YELLOW, "SLAVEBOSS EPOLL WAITING");
         // wait for something to happen
         nfds = epoll_wait(efd, events, MAX_EVENTS, -1); // infinite timeout
         
         // parse all waiting connections
         for (n = 0; !done && !close_nicely && n < nfds; n++) {
            if (events[n].data.fd == rfclient) { // Read/Write from rfclient
               DDCMSG(D_MEGA, YELLOW, "events[%i].events: %i rfclient", n, events[n].events);

               // closed socket?
               if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP) {
                  // client closed, shutdown -- TODO -- don't shutdown, handle new connection
                  DCMSG(RED, "Client closed down @ line %s:%i", __FILE__, __LINE__);
                  done = 1;
               // writing?
               } else if (events[n].events & EPOLLOUT) {
                  // for each connection, write what's available to the RF client
                  for (i = 0; !done && !close_nicely && i <= last_slot; i++) {
                     // rfWrite will decide whether to handle the packet or not
                     done = handleRet(rfWrite(&fconns[i]),&fconns[i], efd);
                     if (done) { DCMSG(RED, "Client closed down @ line %s:%i", __FILE__, __LINE__); }
                  }
               // reading?
               } else if (events[n].events & EPOLLIN || events[n].events & EPOLLPRI) {
                  char tbuf[RF_BUF_SIZE]; // temporary read buffer
                  int ms; // message size
                  int ret; // do next
                  // read message once into temporary buffer
                  D_memset(tbuf,0,RF_BUF_SIZE);
                  ret = rfRead(rfclient, tbuf, &ms);
                  if (ret != doNothing) {
                     handleRet(ret, &fconns[0], efd); // always use first in fconns as rf is the same in all of them
                  } else {
                     // de-mangle and send to all fasit connections
                     for (i = 0; !done && !close_nicely && i <= last_slot; i++) {
                        addToRFBuffer(&fconns[i], tbuf, ms);
                        // rf2fasit will decide whether to handle the packet or not
                        while (!done && !close_nicely && (ret = rf2fasit(&fconns[i], tbuf, ms)) != doNothing) {
                           done = handleRet(ret, &fconns[i], efd);
                           if (done) { DCMSG(RED, "Client closed down @ line %s:%i", __FILE__, __LINE__); }
                        }
                     }
                  }
               }
            } else if (events[n].data.fd == fasitsock) { // Accept new connection from fasitsock
               // new FASIT socket connection
               int index = -1;
               int newsock = accept(fasitsock, (struct sockaddr *) &caddr, &flen);
               DDCMSG(D_MEGA, BLACK,"SLAVEBOSS: client FASIT address = %s:%d", inet_ntoa(caddr.sin_addr),htons(caddr.sin_port));

               if (newsock <= 0) {
                  DDCMSG(D_MEGA, RED, "slaveboss accept() failed");
                  perror("errno: ");
                  exit(1);
                  continue;
               }
               setnonblocking(newsock);
               DDCMSG(D_MEGA, YELLOW, "FASIT ACCEPTED");

               // find slot to put it in
               for (i = 0; i < MAX_CONNECTIONS && index == -1; i++) {
                  if (fconns[i].rf == 0 && fconns[i].fasit == 0) {
                     // found a slot
                     D_memset(&fconns[i],0,sizeof(fasit_connection_t));
                     index = i;
                     fconns[i].rf = rfclient;
                     fconns[i].fasit = newsock;
                     fconns[i].id = 2047; // not registered yet number
                     fconns[i].index = i; // remember its own index
   DDCMSG(D_MEGA, GREEN,"ADDING rf:%i fasit:%i TO fconns[%i]: %08X", fconns[i].rf, fconns[i].fasit, i, &fconns[i]);
                     fconns[i].target_type = RF_Type_Unknown; // unknown target type

                     // are we the last slot?
                     if ((last_slot + 1) == index) {
                        last_slot++; // we are the last slot
                     }
                  }
               }

               // couldn't add, too many connections
               if (index == -1) {
                  close(newsock);
                  DDCMSG(D_MEGA, RED, "slaveboss fconns add fasit client failed");
                  continue;
               }

               // add this connection to the epoll
               D_memset(&ev, 0, sizeof(ev));
               ev.events = EPOLLIN; // only for reading to start
               ev.data.ptr = &fconns[index];
   DDCMSG(D_MEGA, GREEN,"ADDING %i TO EPOLL(newsock)", newsock);
               if (epoll_ctl(efd, EPOLL_CTL_ADD, newsock, &ev) < 0) {
                  DDCMSG(D_MEGA, RED, "slaveboss epoll add fasit client failed");
                  continue;
               }

               // send 100 message
               done = handleRet(send_100(&fconns[index]), &fconns[index], efd);
               if (done) { DCMSG(RED, "Client closed down @ line %s:%i", __FILE__, __LINE__); }
            } else if (events[n].data.ptr != NULL) {
               // mangle and send to to rf connection
               fasit_connection_t *fc = (fasit_connection_t*) events[n].data.ptr;
               DDCMSG(D_MEGA, YELLOW, "events[%i].events: %i %08X", n, events[n].events, fc);

               // closed socket?
               if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP) {
                  // close by handling like a rem_fasitEpoll
                  done = handleRet(rem_fasitEpoll, fc, efd);
                  if (done) { DCMSG(RED, "Client closed down @ line %s:%i", __FILE__, __LINE__); }
               // writing?
               } else if (events[n].events & EPOLLOUT) {
                  // fasitWrite will write what's available to the fasit client
                  done = handleRet(fasitWrite(fc), fc, efd);
                  if (done) { DCMSG(RED, "Client closed down @ line %s:%i", __FILE__, __LINE__); }
               // reading?
               } else if (events[n].events & EPOLLIN || events[n].events & EPOLLPRI) {
                  char tbuf[FASIT_BUF_SIZE]; // temporary read buffer
                  int ms; // message size
                  int ret; // do next
                  // read message once into temporary buffer
                  D_memset(tbuf,0,FASIT_BUF_SIZE);
                  ret = fasitRead(fc->fasit, tbuf, &ms);
                  addToFASITBuffer(fc, tbuf, ms);
                  if (ret != doNothing) {
                     done = handleRet(ret, fc, efd);
                     if (done) { DCMSG(RED, "Client closed down @ line %s:%i", __FILE__, __LINE__); }
                  } else {
                     // fasit2rf will decide whether to handle the packet or not
                     while (!done && !close_nicely && (ret = fasit2rf(fc, tbuf, ms)) != doNothing) {
                        DDCMSG(D_MEGA, CYAN, "FASIT ret is %i", ret);
                        done = handleRet(ret, fc, efd);
                        if (done) { DCMSG(RED, "Client closed down @ line %s:%i", __FILE__, __LINE__); }
                     }
                     DDCMSG(D_MEGA, BLUE, "FASIT ret is %i", ret);
                  }
               }
            }
         }
      }
   }

   DCMSG(BLACK,"slaveboss says goodbye...");
   // close listeners and clients
   close(fasitsock);
   close(rfclient);
   close(rfsock);

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

