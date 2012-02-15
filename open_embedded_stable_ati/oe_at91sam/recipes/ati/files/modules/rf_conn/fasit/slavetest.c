#include "mcp.h"
#include "rf.h"

int verbose;   // so debugging works right in all modules

void print_help(int exval) {
   printf("slavetest [-h] [-v num] [-i ip_address]  [-p port]\n\n");
   printf("  -h            print this help and exit\n");
   printf("  -i 127.0.0.1  set RF connect address\n");
   printf("  -p 14004      set RF connect port\n");
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
   struct sockaddr_in raddr;
   memset(&raddr, 0, sizeof(struct sockaddr_in));
   raddr.sin_family = AF_INET;
   raddr.sin_addr.s_addr = inet_addr("127.0.0.1");    // Any incoming interface
   raddr.sin_port = htons(14000);                     // Listen port
   verbose=0;

   // process the arguments
   //  -i 127.0.0.1  connect addresss
   //  -p 14000      connect port number
   //  -v 2          Verbosity level
   while ((opt = getopt(argc, argv, "hv:i:p:")) != -1) {
      switch (opt) {
         case 'h':
            print_help(0);
            break;
         case 'v':
            verbose = atoi(optarg);
            break;
         case 'i':
            raddr.sin_addr.s_addr = inet_addr(optarg);
            break;
         case 'p':
            raddr.sin_port = htons(atoi(optarg));
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
   DCMSG(BLACK,"SLAVETEST: verbosity is set to 0x%x", verbose);
   DCMSG(BLACK,"SLAVETEST: slaveboss ddress = %s:%d", inet_ntoa(raddr.sin_addr),htons(raddr.sin_port));


   // connect to given address
   

   // run scripted tests
   for (i = optind; i < argc; i++) {
       printf ("Non-option argument %i %s\n", i, argv[i]);
   }
   return 0;
}

