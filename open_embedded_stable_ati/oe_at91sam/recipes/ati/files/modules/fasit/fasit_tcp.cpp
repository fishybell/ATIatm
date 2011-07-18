#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

using namespace std;

#include "fasit_tcp.h"
#include "common.h"
#include "timers.h"
#include "tcp_factory.h"

FASIT_TCP::FASIT_TCP(int fd) : Connection(fd) {
FUNCTION_START("::FASIT_TCP(int fd) : Connection(fd)");
   seq = 0;

   // initialize the client connection later
   client = NULL;
   new ConnTimer(this, CONNECTCLIENT);

FUNCTION_END("::FASIT_TCP(int fd) : Connection(fd)");
}

// special case when initializing as a client
FASIT_TCP::FASIT_TCP(int fd, int tnum) : Connection(fd) {
FUNCTION_START("::FASIT_TCP(int fd) : Connection(fd)");
   seq = 0;
   setTnum(tnum);

FUNCTION_END("::FASIT_TCP(int fd) : Connection(fd)");
}

FASIT_TCP::~FASIT_TCP() {
FUNCTION_START("::~FASIT_TCP()");
   // free client and close its connection
   if (client) {
      client->server = NULL;
      delete client;
      client = NULL;
   }

FUNCTION_END("::~FASIT_TCP()");
}

// fill out default header information
void FASIT_TCP::defHeader(int mnum, FASIT_header *fhdr) {
FUNCTION_START("::defHeader(int mnum, FASIT_header *fhdr)");
   fhdr->num = htons(mnum);
   fhdr->icd1 = htons(1);
   fhdr->icd2 = htons(1);
   fhdr->rsrvd = htonl(0);
   fhdr->seq = htonl(getSequence());
   switch (mnum) {
      case 100:
      case 2000:
      case 2004:
      case 2005:
      case 2006:
         fhdr->icd1 = htons(2);
         break;
   }
FUNCTION_END("::defHeader(int mnum, FASIT_header *fhdr)");
}

// if none is found, FASIT_RESPONSE will be blank. this is the valid response to give
struct FASIT_RESPONSE FASIT_TCP::getResponse(int mnum) {
FUNCTION_START("::getResponse(int mnum)");
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

FUNCTION_END("::getResponse(int mnum)");
   return resp;
}

void FASIT_TCP::seqForResp(int mnum, int seq) {
FUNCTION_START("::seqForResp(int mnum, int seq)");
   respMap[mnum] = seq;
FUNCTION_END("::seqForResp(int mnum, int seq)");
}

// connect a new client
void FASIT_TCP::newClient() {
FUNCTION_START("::newClient()");
   client = factory->newConn <TCP_Client> ();

DMSG("Client added: 0x%08X to server 0x%08X\n", client, this);
   // if we failed to connect, mark this server instance as dead
   if (client == NULL) {
      deleteLater();
   } else {
      // success, let the client know the server instance
      client->server = this;
   }
FUNCTION_END("::newClient()");
}

// macro used in parseData
#define HANDLE_FASIT(FASIT_NUM) case FASIT_NUM : if ( handle_ ## FASIT_NUM (start, end) == -1 ) { return -1; } ; break;

int FASIT_TCP::parseData(int size, const char *buf) {
FUNCTION_START("::parseData(int size, char *buf)");
   IMSG("TCP %i read %i bytes of data\n", fd, size);

   addToBuffer(size, buf);

   // check client
   if (!hasPair()) {
FUNCTION_INT("::parseData(int size, char *buf)", 0);
      return 0;
   }

   int start, end, mnum;
   
   // read all available valid messages
   while ((mnum = validMessage(&start, &end)) != 0) {
      DCMSG(RED,"Recieved FASIT message %d",mnum);
      switch (mnum) {
         HANDLE_FASIT (100);
         HANDLE_FASIT (2000);
         HANDLE_FASIT (2004);
         HANDLE_FASIT (2005);
         HANDLE_FASIT (2006);
         HANDLE_FASIT (2100);
	 HANDLE_FASIT (2101);
	 HANDLE_FASIT (2102);
	 HANDLE_FASIT (2110);
         HANDLE_FASIT (2111);
	 HANDLE_FASIT (2112);
	 HANDLE_FASIT (2113);
         HANDLE_FASIT (2114);
         HANDLE_FASIT (2115);
	 HANDLE_FASIT (13110);
	 HANDLE_FASIT (13112);
	 HANDLE_FASIT (14110);
	 HANDLE_FASIT (14112);
	 HANDLE_FASIT (14200);
	 HANDLE_FASIT (14400);
	 HANDLE_FASIT (14401);
         default:
            IMSG("message valid, but not handled: %i\n", mnum);
            break;
      }
      clearBuffer(end); // clear out last message
   }
FUNCTION_INT("::parseData(int size, char *buf)", 0);
   return 0;
}

// macro used in validMessage function to check just the message length field of the header and call it good
#define END_CHECKS DMSG("%i from %i to %i\n", hdr.num, *start, *end) return hdr.num; break;
#define CHECK_LENGTH(FASIT_NUM) case FASIT_NUM : if (hdr.length != hl + sizeof( FASIT_ ## FASIT_NUM )) { break; }; if (hdr.length > (rsize - *start)) { break; }; END_CHECKS; break;

// the start and end values may be set even if no valid message is found
int FASIT_TCP::validMessage(int *start, int *end) {
FUNCTION_START("::validMessage(int *start, int *end)");
   *start = 0;
   // loop through entire buffer, parsing starting at each character
   while (*start < rsize) {
      /* look for FASIT message */
      if (*start > (rsize - sizeof(FASIT_header))) {
         // if not big enough to hold a full message header don't look for a valid FASIT message
TMSG("too short: %i > (%i - %i)\n", *start, rsize, sizeof(FASIT_header));
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
         CHECK_LENGTH (2000);
         CHECK_LENGTH (2004);
         CHECK_LENGTH (2005);
         CHECK_LENGTH (2100);
         CHECK_LENGTH (2101);
         CHECK_LENGTH (2102);
         CHECK_LENGTH (2110);
	 CHECK_LENGTH (2111);
         CHECK_LENGTH (2112);
         CHECK_LENGTH (2113);
	 CHECK_LENGTH (2114);
	 CHECK_LENGTH (2115);
	 CHECK_LENGTH (13110);
	 CHECK_LENGTH (13112);
	 CHECK_LENGTH (14110);
	 CHECK_LENGTH (14112);
	 CHECK_LENGTH (14200);
	 CHECK_LENGTH (14400);
	 CHECK_LENGTH (14401);
         default:      // not a valid number, not a valid header
            break;
      }

      *start = *start + 1;
   }
FUNCTION_INT("::validMessage(int *start, int *end)", 0);
   return 0;
}

// clears out all tcp connections on non-base station units
void FASIT_TCP::clearSubscribers() {
FUNCTION_START("::clearSubscribers()");
   // the base station shouldn't get this message anyway, but best to be sure
   Connection *tcp = findByTnum(UNASSIGNED);
   if (tcp != NULL) {
      return;
   }

   // delete all tcp connetions
   tcp = Connection::getFirst();
   while (tcp != NULL) {
      tcp->deleteLater();
      tcp = tcp->getNext();
   }
FUNCTION_END("::clearSubscribers()");
}

/***********************************************************
*                     Message Handlers                     *
***********************************************************/

int FASIT_TCP::handle_100(int start, int end) {
FUNCTION_START("::handle_100(int start, int end)");
   // map header (no body for 100)
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_100(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2000(int start, int end) {
FUNCTION_START("::handle_2000(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2000 *msg = (FASIT_2000*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2000));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2000(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2004(int start, int end) {
FUNCTION_START("::handle_2004(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2004 *msg = (FASIT_2004*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2004));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2004(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2005(int start, int end) {
FUNCTION_START("::handle_2005(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2005 *msg = (FASIT_2005*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2005));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2005(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2006(int start, int end) {
FUNCTION_START("::handle_2006(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2006 *msg = (FASIT_2006*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2006));

      // send zone data
      int znum = ntohs(msg->body.zones);
      FASIT_2006z *zone = (FASIT_2006z*)(rbuf + start + sizeof(FASIT_header) + sizeof(FASIT_2006));
      while (znum--) {
         pair()->queueMsg(zone++, sizeof(FASIT_2006z));
      }
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2006(int start, int end)", 0);
   return 0;
}


int FASIT_TCP::handle_2100(int start, int end) {
FUNCTION_START("::handle_2100(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2100 *msg = (FASIT_2100*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2100));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2100(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2101(int start, int end) {
FUNCTION_START("::handle_2101(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2101 *msg = (FASIT_2101*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2101));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2101(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2111(int start, int end) {
FUNCTION_START("::handle_2111(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2111 *msg = (FASIT_2111*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2111));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2111(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2102(int start, int end) {
FUNCTION_START("::handle_2102(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2102 *msg = (FASIT_2102*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2102));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2102(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2114(int start, int end) {
FUNCTION_START("::handle_2114(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2114 *msg = (FASIT_2114*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2114));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2114(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2115(int start, int end) {
FUNCTION_START("::handle_2115(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2115 *msg = (FASIT_2115*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2115));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2115(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2110(int start, int end) {
FUNCTION_START("::handle_2110(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2110 *msg = (FASIT_2110*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2110));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2110(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2112(int start, int end) {
FUNCTION_START("::handle_2112(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2112 *msg = (FASIT_2112*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2112));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2112(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_2113(int start, int end) {
FUNCTION_START("::handle_2113(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2113 *msg = (FASIT_2113*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_2113));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_2113(int start, int end)", 0);
   return 0;
}

int FASIT_TCP::handle_13110(int start, int end) {
	FUNCTION_START("::handle_13110(int start, int end)");
   // map header and message
	FASIT_header *hdr = (FASIT_header*)(rbuf + start);
	FASIT_13110 *msg = (FASIT_13110*)(rbuf + start + sizeof(FASIT_header));

   // send via client
	if (hasPair()) {
		pair()->queueMsg(hdr, sizeof(FASIT_header));
		pair()->queueMsg(msg, sizeof(FASIT_13110));
		pair()->finishMsg();
	}

	FUNCTION_INT("::handle_13110(int start, int end)", 0);
	return 0;
}

int FASIT_TCP::handle_13112(int start, int end) {
	FUNCTION_START("::handle_13112(int start, int end)");
   // map header and message
	FASIT_header *hdr = (FASIT_header*)(rbuf + start);
	FASIT_13112 *msg = (FASIT_13112*)(rbuf + start + sizeof(FASIT_header));

   // send via client
	if (hasPair()) {
		pair()->queueMsg(hdr, sizeof(FASIT_header));
		pair()->queueMsg(msg, sizeof(FASIT_13112));
		pair()->finishMsg();
	}

	FUNCTION_INT("::handle_13112(int start, int end)", 0);
	return 0;
}

int FASIT_TCP::handle_14110(int start, int end) {
	FUNCTION_START("::handle_14110(int start, int end)");
   // map header and message
	FASIT_header *hdr = (FASIT_header*)(rbuf + start);
	FASIT_14110 *msg = (FASIT_14110*)(rbuf + start + sizeof(FASIT_header));

   // send via client
	if (hasPair()) {
		pair()->queueMsg(hdr, sizeof(FASIT_header));
		pair()->queueMsg(msg, sizeof(FASIT_14110));
		pair()->finishMsg();
	}

	FUNCTION_INT("::handle_14110(int start, int end)", 0);
	return 0;
}

int FASIT_TCP::handle_14112(int start, int end) {
	FUNCTION_START("::handle_14112(int start, int end)");
   // map header and message
	FASIT_header *hdr = (FASIT_header*)(rbuf + start);
	FASIT_14112 *msg = (FASIT_14112*)(rbuf + start + sizeof(FASIT_header));

   // send via client
	if (hasPair()) {
		pair()->queueMsg(hdr, sizeof(FASIT_header));
		pair()->queueMsg(msg, sizeof(FASIT_14112));
		pair()->finishMsg();
	}

	FUNCTION_INT("::handle_14112(int start, int end)", 0);
	return 0;
}

int FASIT_TCP::handle_14200(int start, int end) {
	FUNCTION_START("::handle_14200(int start, int end)");
   // map header and message
	FASIT_header *hdr = (FASIT_header*)(rbuf + start);
	FASIT_14200 *msg = (FASIT_14200*)(rbuf + start + sizeof(FASIT_header));

   // send via client
	if (hasPair()) {
		pair()->queueMsg(hdr, sizeof(FASIT_header));
		pair()->queueMsg(msg, sizeof(FASIT_14200));
		pair()->finishMsg();
	}

	FUNCTION_INT("::handle_14200(int start, int end)", 0);
	return 0;
}


int FASIT_TCP::handle_14400(int start, int end) {
	FUNCTION_START("::handle_14400(int start, int end)");
   // map header and message
	FASIT_header *hdr = (FASIT_header*)(rbuf + start);
	FASIT_14400 *msg = (FASIT_14400*)(rbuf + start + sizeof(FASIT_header));

   // send via client
	if (hasPair()) {
		pair()->queueMsg(hdr, sizeof(FASIT_header));
		pair()->queueMsg(msg, sizeof(FASIT_14400));
		pair()->finishMsg();
	}

	FUNCTION_INT("::handle_14400(int start, int end)", 0);
	return 0;
}

int FASIT_TCP::handle_14401(int start, int end) {
FUNCTION_START("::handle_14401(int start, int end)");
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_14401 *msg = (FASIT_14401*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      pair()->queueMsg(hdr, sizeof(FASIT_header));
      pair()->queueMsg(msg, sizeof(FASIT_14401));
      pair()->finishMsg();
   }

FUNCTION_INT("::handle_14401(int start, int end)", 0);
   return 0;
}

//
//   Command Acknowledge
//
//   Since we seem to ack from a bunch of places, better to have a funciton
//
int FASIT_TCP::send_2101_ACK(FASIT_header *hdr,int response) {
   FUNCTION_START("::send_2101_ACK(FASIT_header *hdr,int response)");

   // do handling of message

   DCMSG( MAGENTA,"sending 2101 ACK\n");
   FASIT_header rhdr;
   FASIT_2101 rmsg;
   // build the response - some CID's just reply 2101 with 'S' for received and complied 
   // and 'F' for Received and Cannot comply
   // other Command ID's send other messages

   defHeader(2101, &rhdr); // sets the sequence number and other data
   rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2101));

   // set response
   rmsg.response.rnum = hdr->num;	//  pulls the message number from the header  (htons was wrong here)
   rmsg.response.rseq = hdr->seq;		

   rmsg.body.resp = response;	// The actual response code 'S'=can do, 'F'=Can't do
   DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htons(rhdr.seq),htons(rhdr.rsrvd),htons(rhdr.length));
   DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nR-NUM | R-Seq | Response\n%5d  %6d  '%c'",
	 htons(rmsg.response.rnum),htons(rmsg.response.rseq),rmsg.body.resp);
   queueMsg(&rhdr, sizeof(FASIT_header));	// send the response
   queueMsg(&rmsg, sizeof(FASIT_2101));
   finishMsg();
   
   DCMSG( MAGENTA,"2101 ACK  all queued up - someplace to go? \n");
   FUNCTION_INT("::send_2101_ACK(FASIT_header *hdr,int response)",0);
   return 0;
}


