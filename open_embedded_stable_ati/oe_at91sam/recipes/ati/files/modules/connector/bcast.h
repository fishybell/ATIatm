#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

// udp port we'll use for broadcast packet
#define PORT 4246

// kill switch to program
int close_nicely = 0;
void quitproc(int sig) {
   switch (sig) {
      case SIGINT:
         fprintf(stderr,"Caught signal: SIGINT\n");
         break;
      case SIGQUIT:
         fprintf(stderr,"Caught signal: SIGQUIT\n");
         break;
      default:
         fprintf(stderr,"Caught signal: %i\n", sig);
         break;
   }
   close_nicely = 1;
}

// packet sent/received
#define MAGIC 0xBEADFEAD
typedef struct bcast_packet {
   int magic;
} bcast_packet_t;

