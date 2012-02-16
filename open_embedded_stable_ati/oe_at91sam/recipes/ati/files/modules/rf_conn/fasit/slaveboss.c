#include "mcp.h"
#include "rf.h"
#include "slaveboss.h"

int verbose = 0;    // so debugging works right in all modules
int last_slot = -1; // last slot used starts at no slot used
fasit_connection_t fconns[MAX_SLOTS];

#define MAX_EVENTS 16

void print_help(int exval) {
   printf("slaveboss [-h] [-v num] [-f port] [-r port] [-n num]\n\n");
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
      DCMSG(RED, "Could not disable Nagle's algorithm\n");
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
   if (ret == doNothing) { return done; }
   if (ret & mark_rfWrite) {
      memset(&ev, 0, sizeof(ev));
      ev.data.fd = fc->rf;
      ev.events = EPOLLIN | EPOLLOUT;
      epoll_ctl(efd, EPOLL_CTL_MOD, fc->rf, &ev);
   }
   if (ret & mark_fasitWrite) {
      memset(&ev, 0, sizeof(ev));
      ev.data.ptr = fc;
      ev.events = EPOLLIN | EPOLLOUT;
      epoll_ctl(efd, EPOLL_CTL_MOD, fc->fasit, &ev);
   }
   if (ret & mark_rfRead) { // mark_rfRead overwrites mark_rfWrite
      memset(&ev, 0, sizeof(ev));
      ev.data.fd = fc->rf;
      ev.events = EPOLLIN;
      epoll_ctl(efd, EPOLL_CTL_MOD, fc->rf, &ev);
   }
   if (ret & mark_fasitRead) { // mark_fasitRead overwrites mark_fasitWrite
      memset(&ev, 0, sizeof(ev));
      ev.data.ptr = fc;
      ev.events = EPOLLIN;
      epoll_ctl(efd, EPOLL_CTL_MOD, fc->fasit, &ev);
   }
   if (ret & rem_rfEpoll) {
      epoll_ctl(efd, EPOLL_CTL_DEL, fc->rf, NULL);
      close(fc->rf); // nothing to do if errors...ignore return value from close()
      done = 1; // TODO -- don't be done, accept again on rfclient
   }
   if (ret & rem_fasitEpoll) {
      int index = fc->index;
      epoll_ctl(efd, EPOLL_CTL_DEL, fc->fasit, NULL);
      close(fc->fasit); // nothing to do if errors...ignore return value from close()
      memset(fc, 0, sizeof(fasit_connection_t)); // reset to blank
      if (index == last_slot) {
         last_slot--; // move back last slot if we were the last
      }
   }
   return done;
}

int main(int argc, char **argv) {
   int i, opt, lport, n, fnum=1, done=0, yes=1;
   struct sockaddr_in faddr, raddr; // fasit and rf addresses
   struct epoll_event ev, events[MAX_EVENTS]; // temporary event, main event list
   int fasitsock, rfsock, efd, nfds; // file descriptors for sockets
   int flen = sizeof(faddr);
   // initialize fasit/rf connection array
   memset(fconns, 0, sizeof(fconns));

   // initialize addresses
   memset(&faddr, 0, sizeof(struct sockaddr_in));
   memset(&raddr, 0, sizeof(struct sockaddr_in));
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
            verbose = atoi(optarg);
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

   // setup epoll
   efd = epoll_create(MAX_CONNECTIONS);
   memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN; // only for reading to start
   // listen to the fasit socket always
   ev.data.fd = fasitsock; // remember for later
   if (epoll_ctl(efd, EPOLL_CTL_ADD, fasitsock, &ev) < 0) {
      DieWithError("epoll listener insertion error");
   }


   // main loop
   while (!done) {
      // wait for a single RF client
      struct sockaddr_in caddr; // client address
      int rfclient = accept(rfsock, (struct sockaddr *) &caddr, &flen);
      if (rfclient < 0) {
         DCMSG(RED, "slaveboss accept() failed");
         continue;
      }

     
      // after accepting RF client, accept any number of FASIT clients and parse
      //    the messages back and forth
      while (!done) {
         // wait for something to happen
         nfds = epoll_wait(efd, events, MAX_EVENTS, -1); // infinite timeout
         
         // parse all waiting connections
         for (n = 0; !done && n < nfds; n++) {
            if (events[n].data.fd == rfclient) { // Read/Write from rfclient
               // writing?
               if (events[n].events & EPOLLOUT) {
                  // for each connection, write what's available to the RF client
                  for (i = 0; !done && i < last_slot; i++) {
                     // rfWrite will decide whether to handle the packet or not
                     done = handleRet(rfWrite(&fconns[i]),&fconns[i], efd);
                  }
               }

               // reading?
               if (events[n].events & EPOLLIN || events[n].events & EPOLLPRI) {
                  char tbuf[RF_BUF_SIZE]; // temporary read buffer
                  int mnum; // message number
                  int ms; // message size
                  // read message once into temporary buffer
                  memset(tbuf,0,RF_BUF_SIZE);
                  mnum = rfRead(rfclient, (char**)&tbuf, &ms);
               
                  // de-mangle and send to all fasit connections
                  for (i = 0; !done && i < last_slot; i++) {
                     // rf2fasit will decide whether to handle the packet or not
                     done = handleRet(rf2fasit(&fconns[i], tbuf, ms, mnum), &fconns[i], efd);
                  }
               }

               // closed socket?
               if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP) {
                  // client closed, shutdown -- TODO -- don't shutdown, handle new connection
                  done = 1;
               }
            } else if (events[n].data.fd == fasitsock) { // Accept new connection from fasitsock
               // new FASIT socket connection
               int index = -1;
               int newsock = accept(fasitsock, (struct sockaddr *) &caddr, &flen);
               if (newsock < 0) {
                  DCMSG(RED, "slaveboss accept() failed");
                  continue;
               }

               // check to see if we got all our FASIT connections yet
               if (fnum > 0) {
                  if (--fnum == 0) { // count backwards and re-check
                     // add RF client connection to epoll now that we're ready
                     memset(&ev, 0, sizeof(ev));
                     ev.events = EPOLLIN; // only for reading to start
                     ev.data.fd = rfclient;
                     if (epoll_ctl(efd, EPOLL_CTL_ADD, rfclient, &ev) < 0) {
                        DCMSG(RED, "slaveboss epoll add rfclient failed");
                        continue;
                     }
                  }
               }

               // find slot to put it in
               for (i = 0; i < MAX_SLOTS && index == -1; i++) {
                  if (fconns[i].rf == 0 && fconns[i].fasit == 0) {
                     // found a slot
                     index = i;
                     fconns[i].rf = newsock;
                     fconns[i].fasit = fasitsock;
                     fconns[i].id = 2047; // TODO -- random number
                     fconns[i].index = i; // remember its own index

                     // are we the last slot?
                     if ((last_slot - 1) == index) {
                        last_slot++; // we are the last slot
                     }
                  }
               }

               // couldn't add, too many connections
               if (index == -1) {
                  close(newsock);
                  DCMSG(RED, "slaveboss fconns add fasit client failed");
                  continue;
               }

               // add this connection to the epoll
               memset(&ev, 0, sizeof(ev));
               ev.events = EPOLLIN; // only for reading to start
               ev.data.ptr = &fconns[index];
               if (epoll_ctl(efd, EPOLL_CTL_ADD, newsock, &ev) < 0) {
                  DCMSG(RED, "slaveboss epoll add fasit client failed");
                  continue;
               }
            } else if (ev.data.ptr != NULL) {
               // mangle and send to to rf connection
               fasit_connection_t *fc = (fasit_connection_t*) ev.data.ptr;
               
               // writing?
               if (events[n].events & EPOLLOUT) {
                  // fasitWrite will write what's available to the fasit client
                  done = handleRet(fasitWrite(fc), fc, efd);
               }

               // reading?
               if (events[n].events & EPOLLIN || events[n].events & EPOLLPRI) {
                  char tbuf[FASIT_BUF_SIZE]; // temporary read buffer
                  int mnum; // message number
                  int ms; // message size
                  // read message once into temporary buffer
                  memset(tbuf,0,FASIT_BUF_SIZE);
                  mnum = fasitRead(fc->fasit, (char**)&tbuf, &ms);
               
                  // de-mangle and send to all fasit connections
                  for (i = 0; !done && i < last_slot; i++) {
                     // rf2fasit will decide whether to handle the packet or not
                     done = handleRet(fasit2rf(fc, tbuf, ms, mnum), fc, efd);
                  }
               }

               // closed socket?
               if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP) {
                  // close by handling like a rem_fasitEpoll
                  done = handleRet(rem_fasitEpoll, fc, efd);
               }
            }
         }
      }
   }

   // listen for RF clients
   return 0;
}

