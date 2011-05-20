#include <errno.h>
#include "bcast.h"

#define DELAY 3

int main(int argc, char *argv[]) {
   int sender; // file descriptor
   struct sockaddr_in serveraddr;
   struct sockaddr_in serveraddr_avahi; // address for non-dhcp driven networks 
   int i, yes=1;
   bcast_packet_t packet_out;

   /* install signal handlers */
   signal(SIGINT, quitproc);
   signal(SIGQUIT, quitproc);


   /* parse argv for command line arguments: */
   int port = PORT;
   
const char *usage = "Usage: %s [options]\n\
\t-l X   -- listen on port X rather than the default \n";


   for (i = 1; i < argc; i++) {
      if (argv[i][0] != '-') {
         fprintf(stderr,"Server-main() error: invalid argument (%i)\n", i);
         return 1;
      }
      switch (argv[i][1]) {
         case 'l' :
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
   if ((sender = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
      perror("Server-socket() error");

      /*just exit  */
      return 1;
   }

   /* "address already in use" error message */
   if (setsockopt(sender, SOL_SOCKET, SO_BROADCAST|SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      perror("Server-setsockopt() error");
      return 1;
   }

   /* set destination address */
   serveraddr.sin_family = AF_INET;
   serveraddr.sin_addr.s_addr = inet_addr("255.255.255.255");
   serveraddr.sin_port = htons(port);
   memset(&(serveraddr.sin_zero), '\0', 8);

   serveraddr_avahi.sin_family = AF_INET;
   serveraddr_avahi.sin_addr.s_addr = inet_addr("169.254.255.255");
   serveraddr_avahi.sin_port = htons(port);
   memset(&(serveraddr_avahi.sin_zero), '\0', 8);

   /* prepare packet */
   packet_out.magic = MAGIC;

   /* send packet every X seconds */
   while (!close_nicely)
   {
       /* send packet on avahi address first (it's the most likely to fail) */
       if (sendto(sender, &packet_out, sizeof(packet_out), 0,(struct sockaddr *) &serveraddr_avahi, sizeof(serveraddr_avahi)) == -1)
       {
           /* try to send on normal address (both exist on a wifi MIT/MAT) */
           if (errno != ENETUNREACH || sendto(sender, &packet_out, sizeof(packet_out), 0,(struct sockaddr *) &serveraddr, sizeof(serveraddr)) == -1) {
              perror("Server-sendto() error");
           }
       }
       /* wait to send again */
       sleep(DELAY);
   }

   
}
