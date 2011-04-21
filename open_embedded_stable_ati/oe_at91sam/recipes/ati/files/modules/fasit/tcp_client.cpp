#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

using namespace std;

#include "tcp_client.h"
#include "common.h"
#include "timers.h"

TCP_Client::TCP_Client(int fd, int tnum) : FASIT_TCP(fd, tnum) {
FUNCTION_START("::TCP_Client(int fd, int tnum) : Connection(fd)")
   server = NULL;
FUNCTION_END("::TCP_Client(int fd, int tnum) : Connection(fd)")
}

TCP_Client::~TCP_Client() {
FUNCTION_START("::~TCP_Client()")
   if (server != NULL) {
      // if we are deleted outside of the server being deleted, kill the server too
      server->clientLost();
   }
FUNCTION_END("::~TCP_Client()")
}

/***********************************************************
*    Message Handlers (just forward to connection pair)    *
***********************************************************/
#if 0
int TCP_Client::handle_100(int start, int end) {
FUNCTION_START("::handle_100(int start, int end)")

   // do handling of message
   IMSG("Handling 100 in client\n");

   // do forwarding as normal
   int ret = FASIT_TCP::handle_100(start, end);

FUNCTION_INT("::handle_100(int start, int end)", ret)
   return ret;
}

int TCP_Client::handle_2000(int start, int end) {
FUNCTION_START("::handle_2000(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2000 *msg = (FASIT_2000*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2000));
   }

FUNCTION_INT("::handle_2000(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2004(int start, int end) {
FUNCTION_START("::handle_2004(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2004 *msg = (FASIT_2004*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2004));
   }

FUNCTION_INT("::handle_2004(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2005(int start, int end) {
FUNCTION_START("::handle_2005(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2005 *msg = (FASIT_2005*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2005));
   }

FUNCTION_INT("::handle_2005(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2006(int start, int end) {
FUNCTION_START("::handle_2006(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2006 *msg = (FASIT_2006*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2006));

      // send zone data
      int znum = ntohs(msg->body.zones);
      FASIT_2006z *zone = (FASIT_2006z*)(rbuf + start + sizeof(FASIT_header) + sizeof(FASIT_2006));
      while (znum--) {
         pair()->queueMsg(zone++, sizeof(FASIT_2006z));
      }
   }

FUNCTION_INT("::handle_2006(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2100(int start, int end) {
FUNCTION_START("::handle_2100(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2100 *msg = (FASIT_2100*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2100));
   }

FUNCTION_INT("::handle_2100(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2101(int start, int end) {
FUNCTION_START("::handle_2101(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2101 *msg = (FASIT_2101*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2101));
   }

FUNCTION_INT("::handle_2101(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2111(int start, int end) {
FUNCTION_START("::handle_2111(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2111 *msg = (FASIT_2111*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2111));
   }

FUNCTION_INT("::handle_2111(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2102(int start, int end) {
FUNCTION_START("::handle_2102(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2102 *msg = (FASIT_2102*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2102));
   }

FUNCTION_INT("::handle_2102(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2114(int start, int end) {
FUNCTION_START("::handle_2114(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2114 *msg = (FASIT_2114*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2114));
   }

FUNCTION_INT("::handle_2114(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2115(int start, int end) {
FUNCTION_START("::handle_2115(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2115 *msg = (FASIT_2115*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2115));
   }

FUNCTION_INT("::handle_2115(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2110(int start, int end) {
FUNCTION_START("::handle_2110(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2110 *msg = (FASIT_2110*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2110));
   }

FUNCTION_INT("::handle_2110(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2112(int start, int end) {
FUNCTION_START("::handle_2112(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2112 *msg = (FASIT_2112*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2112));
   }

FUNCTION_INT("::handle_2112(int start, int end)", 0)
   return 0;
}

int TCP_Client::handle_2113(int start, int end) {
FUNCTION_START("::handle_2113(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2113 *msg = (FASIT_2113*)(rbuf + start + sizeof(FASIT_header));

   // send via server
   if (server != NULL) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2113));
   }

FUNCTION_INT("::handle_2113(int start, int end)", 0)
   return 0;
}

#endif /* if 0 block */


