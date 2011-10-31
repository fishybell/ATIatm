#include <sys/epoll.h>
#include <fcntl.h>
#include "bcast.h"

#define MAX_CONNECTIONS 2
#define MAX_EVENTS 2

// utility function to properly make a socket non-blocking
int setnonblocking(int sock) {
   int opts;

   opts = fcntl(sock, F_GETFL);
   if (opts < 0) {
      perror("fcntl(F_GETFL)");
      return -1;
   }
   opts = (opts | O_NONBLOCK);
   if (fcntl(sock, F_SETFL, opts) < 0) {
      perror("fcntl(F_SETFL)");
      return -1;
   }
   return 0;
}

int main(int argc, char *argv[]) {
   struct epoll_event ev1, ev2, events[MAX_EVENTS];
   int listener1, listener2, kdpfd; // file descriptors
   socklen_t addrlen;
   struct sockaddr_in serveraddr;
   struct sockaddr_in serveraddr_avahi;
   int i, nfds, yes=1;

   // packet receipt data
   bcast_packet_t packet_in;
   struct msghdr msg_in;
   struct iovec entry;
   struct sockaddr_in from_addr;
   struct {
      struct cmsghdr cm;
      char control[512];
   } control;
   int res;
   int timeout = -1;


   int port = PORT;
   int number = 1;
   int multiple = 0;

   /* install signal handlers */
   signal(SIGINT, quitproc);
   signal(SIGQUIT, quitproc);

   
   /* parse argv for command line arguments: */
const char *usage = "Usage: %s [options]\n\
\t-p X   -- listen on port X rather than the default \n\
\t-n X   -- wait for X broadcasts rather than one \n";


   for (i = 1; i < argc; i++) {
      if (argv[i][0] != '-') {
         fprintf(stderr,"Client-main() error: invalid argument (%i)\n", i);
         return 1;
      }
      switch (argv[i][1]) {
         case 'p' :
            if (sscanf(argv[++i], "%i", &port) != 1) {
               fprintf(stderr,"Client-main() error: invalid argument (%i)\n", i);
               return 1;
            }
            break;
         case 'n' :
            if (sscanf(argv[++i], "%i", &number) != 1) {
               fprintf(stderr,"Client-main() error: invalid argument (%i)\n", i);
               return 1;
            }
            multiple = 1;
            timeout = 4000 * number; // wait 4 seconds for each client
            break;
         case 'h' :
            printf(usage, argv[0]);
            return 0;
            break;
         case '-' : // for --help
            if (argv[i][2] == 'h') {
               printf(usage, argv[0]);
               return 0;
            } // fall through
         default :
            fprintf(stderr,"Client-main() error: invalid argument (%i)\n", i);
            return 1;
      }
   }

   /* get the listeners */
   if((listener1 = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
      perror("Client-socket(1) error");

      /*just exit  */
      return 1;
   }
   if((listener2 = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
      perror("Client-socket(2) error");

      /*just exit  */
      return 1;
   }

   /* "address already in use" error message */
   if(setsockopt(listener1, SOL_SOCKET, SO_BROADCAST|SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      perror("Client-setsockopt(1) error");
      return 1;
   }
   if(setsockopt(listener2, SOL_SOCKET, SO_BROADCAST|SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      perror("Client-setsockopt(2) error");
      return 1;
   }

   /* setup addresses for both avahi and dhcp driven networks */
   serveraddr.sin_family = AF_INET;
   serveraddr.sin_addr.s_addr = INADDR_BROADCAST;
   serveraddr.sin_port = htons(port);
   memset(&(serveraddr.sin_zero), '\0', 8);

   serveraddr_avahi.sin_family = AF_INET;
   serveraddr_avahi.sin_addr.s_addr = inet_addr("169.254.0.0");
   serveraddr_avahi.sin_port = htons(port);
   memset(&(serveraddr_avahi.sin_zero), '\0', 8);

   /* bind either connection */
   bind(listener1, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
   bind(listener2, (struct sockaddr *)&serveraddr_avahi, sizeof(serveraddr_avahi));

   /* ready sockets for polling (either one) */
   setnonblocking(listener1);
   setnonblocking(listener2);

   /* set up polling */
   kdpfd = epoll_create(MAX_CONNECTIONS);
   // listen to the listeners
   memset(&ev1, 0, sizeof(ev1));
   memset(&ev2, 0, sizeof(ev2));
   ev1.events = EPOLLIN;
   ev2.events = EPOLLIN;
   ev1.data.fd = listener1;
   ev2.data.fd = listener2;
   epoll_ctl(kdpfd, EPOLL_CTL_ADD, listener1, &ev1);
   epoll_ctl(kdpfd, EPOLL_CTL_ADD, listener2, &ev2);

   /* poll once */
   while(!close_nicely) {
      nfds = epoll_wait(kdpfd, events, MAX_EVENTS, timeout);

      /* timed out? */
      if (nfds == 0) {
         /* break out of loop */
         close_nicely = 1;
      }
       
      /* check all ready sockets */
      for(i = 0; i < nfds && !close_nicely; ++i) {
         /* was it one of my listeners? */
         if(events[i].data.fd == listener1 || events[i].data.fd == listener2) {
            /* prepare packet receipt data */
            memset(&msg_in, 0, sizeof(msg_in));
            msg_in.msg_iov = &entry;
            msg_in.msg_iovlen = 1;
            entry.iov_base = &packet_in;
            entry.iov_len = sizeof(packet_in);
            msg_in.msg_name = (caddr_t)&from_addr;
            msg_in.msg_namelen = sizeof(from_addr);
            msg_in.msg_control = &control;
            msg_in.msg_controllen = sizeof(control);

            /* receive packet */
            if((res = recvmsg(events[i].data.fd, &msg_in, MSG_WAITALL)) == -1) {
               perror("Client-recvmsg() error");
               return 1;
            }

            /* check magic, print IP of sender */
            if (res == sizeof(packet_in) && packet_in.magic == MAGIC) {
                printf("%s", inet_ntoa(from_addr.sin_addr));
                /* if we're doing multiple, subdivide by lines */
                if (multiple) {
                   printf("\n");
                   fflush(stdout); // print out what we've got now...
                }
            } else {
                printf("error");
            }

            /* check number to see if we should look for another */
            if (--number == 0) {
               /* break out of loop */
               close_nicely = 1;
            } else if (multiple) {
               timeout -= 4000; // one less to worry about, wait 4 seconds less
            }
         }
      }
   }
}
