#include <errno.h>
#include "bcast.h"

#define DELAY 3

int main(int argc, char *argv[]) {
   int sender1, sender2, sender3; // file descriptors
   struct sockaddr_in serveraddr;
   struct sockaddr_in serveraddr_avahi; // address for non-dhcp driven networks 
   struct sockaddr_in serveraddr_wifi;  // address for detachable wifi networks
   int i, yes=1, wifion;
   bcast_packet_t packet_out;

   /* install signal handlers */
   signal(SIGINT, quitproc);
   signal(SIGQUIT, quitproc);


   /* parse argv for command line arguments: */
   int port = PORT;
   
const char *usage = "Usage: %s [options]\n\
\t-p X   -- broadcast on port X rather than the default \n";


   for (i = 1; i < argc; i++) {
      if (argv[i][0] != '-') {
         fprintf(stderr,"Server-main() error: invalid argument (%i)\n", i);
         return 1;
      }
      switch (argv[i][1]) {
         case 'p' :
            if (sscanf(argv[++i], "%i", &port) != 1) {
               fprintf(stderr,"Server-main() error: invalid argument (%i)\n", i);
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
            fprintf(stderr,"Server-main() error: invalid argument (%i)\n", i);
            return 1;
      }
   }

   /* get the sender */
   if ((sender1 = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
      perror("Server-socket(1) error");

      /*just exit  */
      return 1;
   }
   if ((sender2 = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
      perror("Server-socket(2) error");

      /*just exit  */
      return 1;
   }
   if ((sender3 = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
      perror("Server-socket(3) error");

      /*just exit  */
      return 1;
   }

   /* "address already in use" error message */
   if (setsockopt(sender1, SOL_SOCKET, SO_BROADCAST|SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      perror("Server-setsockopt(1) error");
      return 1;
   }
   if (setsockopt(sender2, SOL_SOCKET, SO_BROADCAST|SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      perror("Server-setsockopt(2) error");
      return 1;
   }
   if (setsockopt(sender3, SOL_SOCKET, SO_BROADCAST|SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      perror("Server-setsockopt(3) error");
      return 1;
   }

   /* use only the local ethernet link, not wifi, etc. */
   if (setsockopt(sender1, SOL_SOCKET, SO_BINDTODEVICE, "eth0", 5) == -1) {
      perror("Server-setsockopt(1) BINDTO error");
      return 1;
   }
   if (setsockopt(sender2, SOL_SOCKET, SO_BINDTODEVICE, "eth0", 5) == -1) {
      perror("Server-setsockopt(2) BINDTO error");
      return 1;
   }
   /* attempt connection on wifi, but don't fail if it doesn't work */
   if (setsockopt(sender3, SOL_SOCKET, SO_BINDTODEVICE, "wlan0", 6) == -1) {
      wifion = 0;
   } else {
      wifion = 1;
   }
   

   /* set destination addresses */
   serveraddr.sin_family = AF_INET;
   serveraddr.sin_addr.s_addr = inet_addr("255.255.255.255");
   serveraddr.sin_port = htons(port);
   memset(&(serveraddr.sin_zero), '\0', 8);

   serveraddr_avahi.sin_family = AF_INET;
   serveraddr_avahi.sin_addr.s_addr = inet_addr("169.254.255.255");
   serveraddr_avahi.sin_port = htons(port);
   memset(&(serveraddr_avahi.sin_zero), '\0', 8);

   serveraddr_wifi.sin_family = AF_INET;
   serveraddr_wifi.sin_addr.s_addr = inet_addr("255.255.255.255");
   serveraddr_wifi.sin_port = htons(port);
   memset(&(serveraddr_wifi.sin_zero), '\0', 8);

   /* prepare packet */
   packet_out.magic = MAGIC;

   /* send packet every X seconds */
   while (!close_nicely)
   {
       /* send packet on both address, ignoring failures */
       if (sendto(sender1, &packet_out, sizeof(packet_out), 0,(struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) {
           //perror("1:");
       }
       if (sendto(sender2, &packet_out, sizeof(packet_out), 0,(struct sockaddr *) &serveraddr_avahi, sizeof(serveraddr_avahi)) < 0) {
           //perror("2:");
       }
       if (wifion) {
           if (sendto(sender3, &packet_out, sizeof(packet_out), 0,(struct sockaddr *) &serveraddr_wifi, sizeof(serveraddr_wifi)) < 0) {
               //perror("3:");

               wifion = 0;
           }
       } else {
           /* reconnect wifi */
           if (setsockopt(sender3, SOL_SOCKET, SO_BINDTODEVICE, "wlan0", 6) != -1) {
               wifion = 1;
           }
       }

       /* wait to send again */
       sleep(DELAY);
   }

}
