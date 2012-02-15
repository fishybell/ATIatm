#include "mcp.h"
#include "rf.h"

int verbose;   // so debugging works right in all modules

void print_help(int exval) {
   printf("slaveboss [-h] [-v num] [-f port] [-r port]\n\n");
   printf("  -h            print this help and exit\n");
   printf("  -f 14000      set FASIT listen port\n");
   printf("  -r 14004      set RF listen port\n");
   printf("  -v 0          set verbosity bits\n");
   exit(exval);
}

void DieWithError(char *errorMessage){
    char buf[200];
    strerror_r(errno,buf,200);
    DCMSG(RED,"slaveboss %s %s \n", errorMessage,buf);
    exit(1);
}

int main(int argc, char **argv) {
   int i, opt, lport;
   struct sockaddr_in faddr, raddr;
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
   while ((opt = getopt(argc, argv, "hv:r:f:")) != -1) {
      switch (opt) {
         case 'h':
            print_help(0);
            break;
         case 'v':
            verbose = atoi(optarg);
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

   // listen for FASIT clients

   // listen for RF client
   return 0;
}

