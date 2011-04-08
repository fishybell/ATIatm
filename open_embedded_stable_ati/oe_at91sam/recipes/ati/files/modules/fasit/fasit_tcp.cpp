#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

using namespace std;

#include "fasit_tcp.h"
#include "common.h"
#include "timers.h"
#include "tcp_factory.h"

// initialize static members
FASIT_TCP *FASIT_TCP::flink = NULL;

FASIT_TCP::FASIT_TCP(int fd) : Connection(fd) {
FUNCTION_START("::FASIT_TCP(int fd) : Connection(fd)")
   seq = 0;
   initChain();

   // initialize the client connection later
   client = NULL;
   doclose = false;
   new ConnTimer(this, CONNECTCLIENT);

FUNCTION_END("::FASIT_TCP(int fd) : Connection(fd)")
}

// special case when initializing as a client
FASIT_TCP::FASIT_TCP(int fd, int tnum) : Connection(fd) {
FUNCTION_START("::FASIT_TCP(int fd) : Connection(fd)")
   seq = 0;
   setTnum(tnum);

FUNCTION_END("::FASIT_TCP(int fd) : Connection(fd)")
}

// initialize place in linked list
void FASIT_TCP::initChain() {
FUNCTION_START("::initChain()")
   link = NULL;
   if (flink == NULL) {
      // we're first
DMSG("first tcp link in chain 0x%08x\n", this);
      flink = this;
   } else {
      // we're last (find old last and link from there)
      FASIT_TCP *tlink = flink;
      while(tlink->link != NULL) {
         tlink = tlink->link;
      }
      tlink->link = this;
DMSG("last tcp link in chain 0x%08x\n", this);
   }
FUNCTION_END("::initChain()")
}

FASIT_TCP::~FASIT_TCP() {
FUNCTION_START("::~FASIT_TCP()")
   // free client and close its connection
   if (client) {
      client->server = NULL;
      delete client;
      client = NULL;
   }

   // am I the sole FASIT_TCP?
   if (link == NULL && flink == this) {
      flink = NULL;
   }

   // remove from linked list
   FASIT_TCP *tlink = flink;
   while (tlink != NULL) {
      if (tlink->link == this) {
         tlink->link = this->link; // connect to next link in chain (if last, this drops this link off chain)
         break;
      }
   }

FUNCTION_END("::~FASIT_TCP()")
}

// connect a new client
void FASIT_TCP::newClient() {
   client = factory->newConn();

   // if we failed to connect, mark this server instance as dead
   if (client == NULL) {
      doclose = true;
   } else {
      // success, let the client know the server instance
      client->server = this;
   }
}

// macro used in parseData
#define HANDLE_FASIT(FASIT_NUM) case FASIT_NUM : if ( handle_ ## FASIT_NUM (start, end) == -1 ) { return -1; } ; break;

int FASIT_TCP::parseData(int size, const char *buf) {
FUNCTION_START("::parseData(int size, char *buf)")
   IMSG("TCP %i read %i bytes of data\n", fd, size)

   addToBuffer(size, buf);

   // check client
   if (client == NULL) {
      if (doclose) {
         // we failed to acquire a client connection, delete this connection
         return -1;
      }
      // ignore all data until we have a client connection
      return 0;
   }

   int start, end, mnum;
   
   // read all available valid messages
HERE
   while ((mnum = validMessage(&start, &end)) != 0) {
      IMSG("TCP Message : %i\n", mnum)
      switch (mnum) {
         HANDLE_FASIT (100)
         HANDLE_FASIT (2000)
         HANDLE_FASIT (2004)
         HANDLE_FASIT (2005)
         HANDLE_FASIT (2006)
         HANDLE_FASIT (2100)
         HANDLE_FASIT (2101)
         HANDLE_FASIT (2111)
         HANDLE_FASIT (2102)
         HANDLE_FASIT (2114)
         HANDLE_FASIT (2115)
         HANDLE_FASIT (2110)
         HANDLE_FASIT (2112)
         HANDLE_FASIT (2113)
         default:
            IMSG("message valid, but not handled: %i\n", mnum)
            break;
      }
      clearBuffer(end); // clear out last message
   }
FUNCTION_INT("::parseData(int size, char *buf)", 0)
   return 0;
}

// macro used in validMessage function to check just the message length field of the header and call it good
#define END_CHECKS DMSG("%i from %i to %i\n", hdr.num, *start, *end) return hdr.num; break;
#define CHECK_LENGTH(FASIT_NUM) case FASIT_NUM : if (hdr.length != hl + sizeof( FASIT_ ## FASIT_NUM )) { break; }; if (hdr.length > (rsize - *start)) { break; }; END_CHECKS; break;

// the start and end values may be set even if no valid message is found
int FASIT_TCP::validMessage(int *start, int *end) {
FUNCTION_START("::validMessage(int *start, int *end)")
   *start = 0;
   // loop through entire buffer, parsing starting at each character
   while (*start < rsize) {
      /* look for FASIT message */
      if (*start > (rsize - sizeof(FASIT_header))) {
         // if not big enough to hold a full message header don't look for a valid FASIT message
TMSG("too short: %i > (%i - %i)\n", *start, rsize, sizeof(FASIT_header))
         *start = *start + 1;
         continue;
      }
      FASIT_header hdr;
      int hl = sizeof(FASIT_header); // keep for later
      memcpy(&hdr, rbuf + *start, hl);
      // convert header from network byte order to host byte order
      hdr.num = ntohs(hdr.num);
      hdr.icd1 = ntohs(hdr.icd1);
      hdr.icd2 = ntohs(hdr.icd2);
      hdr.seq = ntohl(hdr.seq);
      hdr.rsrvd = ntohl(hdr.rsrvd);
      hdr.length = ntohs(hdr.length);
      if (hdr.icd1 > 2) { *start = *start + 1; continue; }    // invalid icd major version number
      if (hdr.icd2 > 1) { *start = *start + 1; continue; }    // invalid icd minor version number
      if (hdr.rsrvd != 0) { *start = *start + 1; continue; }   // invalid reserved field
      if (hdr.length < hl) { *start = *start + 1; continue; } // invalid message length
      *end = *start + hdr.length;
      switch (hdr.num) {
         case 100:          // just look at header length
            if (hdr.length != hl) { break; } // invalid message length
            END_CHECKS;
         case 2006:         // don't check against length of FASIT_2006 struct
            if (hdr.length > (rsize - *start)) { break; } // have the beginning of a valid message
            END_CHECKS;
         // these ones just look at msg length
         CHECK_LENGTH (2000)
         CHECK_LENGTH (2004)
         CHECK_LENGTH (2005)
         CHECK_LENGTH (2100)
         CHECK_LENGTH (2101)
         CHECK_LENGTH (2111)
         CHECK_LENGTH (2102)
         CHECK_LENGTH (2114)
         CHECK_LENGTH (2115)
         CHECK_LENGTH (2110)
         CHECK_LENGTH (2112)
         CHECK_LENGTH (2113)
         default:      // not a valid number, not a valid header
            break;
      }

      *start = *start + 1;
   }
FUNCTION_INT("::validMessage(int *start, int *end)", 0)
   return 0;
}

// if none is found, FASIT_RESPONSE will be blank. this is the valid response to give
struct FASIT_RESPONSE FASIT_TCP::getResponse(int mnum) {
FUNCTION_START("::getResponse(int mnum)")
   FASIT_RESPONSE resp;
   resp.rnum = 0;
   resp.rseq = 0;

   map<int,int>::iterator rIt;
   rIt = respMap.find(mnum);
   if (rIt != respMap.end()) {
      // use last remembered response for this message number
      resp.rnum = htons(mnum);
      resp.rseq = htonl(rIt->second);

      // this is a one shot deal, delete it now
      respMap.erase(rIt);
   }

FUNCTION_END("::getResponse(int mnum)")
   return resp;
}

void FASIT_TCP::seqForResp(int mnum, int seq) {
FUNCTION_START("::seqForResp(int mnum, int seq)")
   respMap[mnum] = seq;
FUNCTION_END("::seqForResp(int mnum, int seq)")
}

// clears out all tcp connections on non-base station units
void FASIT_TCP::clearSubscribers() {
FUNCTION_START("::clearSubscribers()")
   // the base station shouldn't get this message anyway, but best to be sure
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(UNASSIGNED);
   if (tcp != NULL) {
      return;
   }

   // delete all tcp connetions
   tcp = FASIT_TCP::getFirst();
   while (tcp != NULL) {
      tcp->deleteLater();
      tcp = tcp->getNext();
   }
FUNCTION_END("::clearSubscribers()")
}

/***********************************************************
*                     Message Handlers                     *
***********************************************************/

int FASIT_TCP::handle_100(int start, int end) {
FUNCTION_START("::handle_100(int start, int end)")
   // map header (no body for 100)
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);

   // remember sequence for this connection
   seqForResp(100, ntohl(hdr->seq));

FUNCTION_INT("::handle_100(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2000(int start, int end) {
FUNCTION_START("::handle_2000(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2000 *msg = (FASIT_2000*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2000, ntohl(hdr->seq));

FUNCTION_INT("::handle_2000(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2004(int start, int end) {
FUNCTION_START("::handle_2004(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2004 *msg = (FASIT_2004*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2004, ntohl(hdr->seq));

FUNCTION_INT("::handle_2004(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2005(int start, int end) {
FUNCTION_START("::handle_2005(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2005 *msg = (FASIT_2005*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2005, ntohl(hdr->seq));

FUNCTION_INT("::handle_2005(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2006(int start, int end) {
FUNCTION_START("::handle_2006(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2006 *msg = (FASIT_2006*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2006, ntohl(hdr->seq));

FUNCTION_INT("::handle_2006(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2100(int start, int end) {
FUNCTION_START("::handle_2100(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2100 *msg = (FASIT_2100*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2100, ntohl(hdr->seq));

FUNCTION_INT("::handle_2100(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2101(int start, int end) {
FUNCTION_START("::handle_2101(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2101 *msg = (FASIT_2101*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2101, ntohl(hdr->seq));

FUNCTION_INT("::handle_2101(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2111(int start, int end) {
FUNCTION_START("::handle_2111(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2111 *msg = (FASIT_2111*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2111, ntohl(hdr->seq));

FUNCTION_INT("::handle_2111(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2102(int start, int end) {
FUNCTION_START("::handle_2102(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2102 *msg = (FASIT_2102*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2102, ntohl(hdr->seq));

FUNCTION_INT("::handle_2102(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2114(int start, int end) {
FUNCTION_START("::handle_2114(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2114 *msg = (FASIT_2114*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2114, ntohl(hdr->seq));

FUNCTION_INT("::handle_2114(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2115(int start, int end) {
FUNCTION_START("::handle_2115(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2115 *msg = (FASIT_2115*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2115, ntohl(hdr->seq));

FUNCTION_INT("::handle_2115(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2110(int start, int end) {
FUNCTION_START("::handle_2110(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2110 *msg = (FASIT_2110*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2110, ntohl(hdr->seq));

FUNCTION_INT("::handle_2110(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2112(int start, int end) {
FUNCTION_START("::handle_2112(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2112 *msg = (FASIT_2112*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2112, ntohl(hdr->seq));

FUNCTION_INT("::handle_2112(int start, int end)", 0)
   return 0;
}

int FASIT_TCP::handle_2113(int start, int end) {
FUNCTION_START("::handle_2113(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2113 *msg = (FASIT_2113*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2113, ntohl(hdr->seq));

FUNCTION_INT("::handle_2113(int start, int end)", 0)
   return 0;
}

