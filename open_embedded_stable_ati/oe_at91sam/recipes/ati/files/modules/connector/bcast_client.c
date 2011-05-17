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

   /* install signal handlers */
   signal(SIGINT, quitproc);
   signal(SIGQUIT, quitproc);


   /* parse argv for command line arguments: */
   int port = PORT;
   
const char *usage = "Usage: %s [options]\n\
\t-l X   -- listen on port X rather than the default \n";


   for (i = 1; i < argc; i++) {
      if (argv[i][0] != '-') {
         fprintf(stderr,"Client-main() error: invalid argument (%i)\n", i);
         return 1;
      }
      switch (argv[i][1]) {
         case 'l' :
            if (sscanf(argv[++i], "%i", &port) != 1) {
               fprintf(stderr,"Client-main() error: invalid argument (%i)\n", i);
               return 1;
            }
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
   if (bind(listener1, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1 && 
       bind(listener2, (struct sockaddr *)&serveraddr_avahi, sizeof(serveraddr_avahi)) == -1) {
      perror("Client-bind() error");
      return 1;
   }

   /* ready sockets for polling (either one) */
   if (setnonblocking(listener1) == -1 && setnonblocking(listener2) == -1) {
      return 1;
   }

   /* set up polling */
   kdpfd = epoll_create(MAX_CONNECTIONS);
   // listen to the listeners
   memset(&ev1, 0, sizeof(ev1));
   memset(&ev2, 0, sizeof(ev2));
   ev1.events = EPOLLIN;
   ev2.events = EPOLLIN;
   ev1.data.fd = listener1;
   ev2.data.fd = listener2;
   if (epoll_ctl(kdpfd, EPOLL_CTL_ADD, listener1, &ev1) < 0 &&
       epoll_ctl(kdpfd, EPOLL_CTL_ADD, listener2, &ev2) < 0) {
      perror("Client-epoll_ctl() error");
      return 1;
   }

   /* poll once */
   while(!close_nicely) {
      nfds = epoll_wait(kdpfd, events, MAX_EVENTS, -1);
       
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
            } else {
                printf("error");
            }

            /* break out of loop */
            close_nicely = 1;
         }
      }
   }
}
