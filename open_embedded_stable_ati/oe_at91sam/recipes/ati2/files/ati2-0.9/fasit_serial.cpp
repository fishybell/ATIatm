using namespace std;

#include "fasit_serial.h"
#include "fasit_tcp.h"
#include "tcp_factory.h"
#include "common.h"
#include <stdio.h>
#include <list>

FASIT_Serial::FASIT_Serial(char *fname) : SerialConnection(fname) {
// TODO -- other initialization
}

FASIT_Serial::~FASIT_Serial() {
// TODO -- other uninitialization
}

void FASIT_Serial::defHeader(int mnum, FASIT_header *fhdr) {
   fhdr->num = mnum;
   fhdr->icd1 = 1;
   fhdr->icd2 = 1;
   fhdr->rsrvd = 0;
   switch (mnum) {
      case 100:
      case 2000:
      case 2004:
      case 2005:
      case 2006:
         fhdr->icd1 = 2;
         break;
   }
}

// macro used in parseData
#define HANDLE_FASIT(FASIT_NUM) case FASIT_NUM : if ( handle_ ## FASIT_NUM (start, end) == -1 ) { return -1; } ; break;

int FASIT_Serial::parseData(int size, char *buf) {
   printf("Serial %i read %i bytes of data\n", fd, size);
   printf("TCP %i read %i bytes of data\n", fd, size);

   addToBuffer(size, buf);

   int start, end, mnum;
   
   // read all available valid messages
   while (validMessage(&start, &end) != 0) {
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
         HANDLE_FASIT (63556)
         HANDLE_FASIT (42966)
         HANDLE_FASIT (43061)
         HANDLE_FASIT (47157)
         HANDLE_FASIT (16000)
         HANDLE_FASIT (16001)
         HANDLE_FASIT (16002)
         HANDLE_FASIT (16003)
         HANDLE_FASIT (16004)
         HANDLE_FASIT (16005)
         default:
            printf("message valid, but not handled: %i\n", mnum);
      }
      clearBuffer(end); // clear out last message
   }
}

// macros used in validMessage function to check just the message length field of the header and call it good
#define END_CHECKS return hdr.num; break;
#define CHECK_LENGTH(FASIT_NUM) case FASIT_NUM : if (hdr.length != hl + sizeof( ATI_ ## FASIT_NUM )) { break; } ; if (hdr.length > (rsize - *start)) { return 0; } ; END_CHECKS
#define CHECK_CRC(CRCL) if (*end <= rsize && crc ## CRCL (rbuf, *start, *end) == 0 ) { END_CHECKS }

// the start and end values may be set even if no valid message is found
int FASIT_Serial::validMessage(int *start, int *end) {
// TODO -- partially valid messages should cause a resend?
   *start = 0;
   // loop through entire buffer, parsing starting at each character
   while (*start < (rsize - sizeof(ATI_header))) { // end when not big enough to contain an entire header
      // unlike TCP messages, all serial messages have the same header
      if (rbuf[*start] & 240 != 240) {
         // first 3 bits of header not 
         *start++;
         continue;
      }
      
      ATI_header hdr;
      int hl = sizeof(ATI_header); // keep for later
      memcpy(&hdr, rbuf + *start, hl);
      if (hdr.length < hl) { *start++; continue; } // invalid message length
      *end = *start + hdr.length;

      // if we're reasonably sure this message might be done, check the parity now
      if (*end < rsize) {
         char buf[15];
         memcpy(buf, rbuf + *start, min(15, (int)hdr.length));
         if (parity(buf, min(15, (int)hdr.length)) != 0) {
            // invalid parity, move on
            *start++;
            continue;
         }
      }

      // do checks for individual messages
      switch (hdr.num) {
         case 2005:
            if (hdr.length != sizeof(ATI_2005s) && hdr.length != sizeof(ATI_2005f)) { break; }
            if (hdr.length > (rsize - *start)) { return 0; }
            END_CHECKS
         case 63556:
            *end = *start + sizeof(ATI_63556);
            CHECK_CRC (8)
            break;
         case 42966:
            *end = *start + sizeof(ATI_42966);
            CHECK_CRC (8)
            break;
         case 2100:
            *end = *start + sizeof(ATI_header) + sizeof(ATI_2100) + sizeof(ATI_2100c) + sizeof(ATI_2100m);
            CHECK_CRC (32)
            *end = *start + sizeof(ATI_header) + sizeof(ATI_2100) + sizeof(ATI_2100c) + sizeof(ATI_2100m) + sizeof(ATI_2100m);
            CHECK_CRC (32)
            *end = *start + sizeof(ATI_header) + sizeof(ATI_2100) + sizeof(ATI_2100c) + sizeof(ATI_2100m) + sizeof(ATI_2100m) + sizeof(ATI_2100m);
            CHECK_CRC (32)
            *end = *start + sizeof(ATI_header) + sizeof(ATI_2100) + sizeof(ATI_2100c) + sizeof(ATI_2100m) + sizeof(ATI_2100m) + sizeof(ATI_2100m) + sizeof(ATI_2100m);
            CHECK_CRC (32)
            break;
         case 2101:
         case 43061:
            if (hdr.length != sizeof(ATI_header)) { break; }
            if (hdr.length > (rsize - *start)) { return 0; }
            END_CHECKS
         case 2111:
            if (hdr.length != sizeof(ATI_2111s) && hdr.length != sizeof(ATI_2111f)) { break; }
            if (hdr.length > (rsize - *start)) { return 0; }
            END_CHECKS
         case 2102:
            if (hdr.length == sizeof(ATI_2102)) {
               if (hdr.length > (rsize - *start)) { return 0; }
               END_CHECKS
            }
            *end = *start + sizeof(ATI_2102) + sizeof(ATI_2102x);
            CHECK_CRC (8)
            break;
         // these ones just look at msg length
         CHECK_LENGTH (100)
         CHECK_LENGTH (2000)
         CHECK_LENGTH (2004)
         CHECK_LENGTH (2006)
         CHECK_LENGTH (47157)
         CHECK_LENGTH (2114)
         CHECK_LENGTH (2115)
         CHECK_LENGTH (2110)
         CHECK_LENGTH (2112)
         CHECK_LENGTH (2113)
         CHECK_LENGTH (16000)
         CHECK_LENGTH (16001)
         CHECK_LENGTH (16002)
         CHECK_LENGTH (16003)
         CHECK_LENGTH (16004)
         CHECK_LENGTH (16005)
      }

      *start++;
   }
   return 0;
}

/***********************************************************
*                     Message Handlers                     *
***********************************************************/

int FASIT_Serial::handle_100(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_100 *msg = (ATI_100*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct destination?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(msg->dest);
   if (tcp == NULL) { return 0; }

   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(100,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header);

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));

   return 0;
}

int FASIT_Serial::handle_2000(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_2000 *msg = (ATI_2000*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct destination?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(msg->dest);
   if (tcp == NULL) { return 0; }

   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(2000,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2000);
   FASIT_2000 fmsg = msg->embed;

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));
   tcp->queueMsg(&fmsg, sizeof(FASIT_2000));

   return 0;
}

int FASIT_Serial::handle_2004(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_2004 *msg = (ATI_2004*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(2004,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2004);
   FASIT_2004 fmsg;
   fmsg.body = msg->embed;
   fmsg.response = tcp->getResponse(2100);

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));
   tcp->queueMsg(&fmsg, sizeof(FASIT_2004));

   return 0;
}

int FASIT_Serial::handle_2005(int start, int end) {
   FASIT_2005b body;

   // map header
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   if (end - start == sizeof(ATI_header) + sizeof(ATI_2005s)) {
      // map small message
      ATI_2005s *msg = (ATI_2005s*)(rbuf + start + sizeof(ATI_header));

      // get device id and flags (small version uses ATI's MAC prefix)
      body.flags = msg->flags;
      body.devid = 0;
      body.devid = ((__uint64_t)ATI_MAC1 << 40) & ((__uint64_t)ATI_MAC2 << 32) & ((__uint64_t)ATI_MAC3 << 24) &
                   ((__uint64_t)msg->mac[0] << 16) & ((__uint64_t)msg->mac[1] << 8) & ((__uint64_t)msg->mac[2]);
   } else {
      // map large message
      ATI_2005f *msg = (ATI_2005f*)(rbuf + start + sizeof(ATI_header));

      // get device id and flags (full version passes entire MAC address)
      body.flags = msg->flags;
      body.devid = 0;
      body.devid = ((__uint64_t)msg->mac[0] << 40) & ((__uint64_t)msg->mac[1] << 32) & ((__uint64_t)msg->mac[2] << 24) &
                   ((__uint64_t)msg->mac[3] << 16) & ((__uint64_t)msg->mac[4] << 8) & ((__uint64_t)msg->mac[5]);
   }

   // are we the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(2005,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2005);
   FASIT_2005 fmsg;
   fmsg.body = body;
   fmsg.response = tcp->getResponse(100);

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));
   tcp->queueMsg(&fmsg, sizeof(FASIT_2005));

   return 0;
}

int FASIT_Serial::handle_2006(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_2006 *msg = (ATI_2006*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // remember zone data for this tcp connection
   tcp->set2006base(msg->embed); // remember zone header data
   tcp->clearZones(); // clear out old zone data to make room for new zone data

   // ask for the next chunk of the message
   ATI_63556 req;
   req.dest = hdr->source;
   for (int i=0; i<8; i++) { // ask for up to the first 8 zones
      if (i>msg->embed.zones) {
         req.zones[i] = 0xffff; // no zone requested for this item
      } else {
         req.zones[i] = i; // these zone numbers have no correlation to the znum field in FASIT_2006z
      }
   }
   ATI_header reqh = createHeader(63556, BASE_STATION, &req, sizeof(ATI_63556));
   req.crc = crc8(&req, 0, sizeof(ATI_63556) - sizeof(req.crc)); // don't crc the crc field

   // send back on all serial devices
   queueMsgAll(&reqh, sizeof(ATI_header));
   queueMsgAll(&req, sizeof(ATI_63556));

   return 0;
}

int FASIT_Serial::handle_2100(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_2100 *msg = (ATI_2100*)(rbuf + start + sizeof(ATI_header));
   ATI_2100m **msgm = (ATI_2100m**)(rbuf + start + sizeof(ATI_header) + sizeof(ATI_2100));
   // we don't grab ATI_2100c, as it's not needed, and its crc member has already checked

   // compute the number of ATI_2100m structures found based on the size of the total message
   int nmsgm = (end - (sizeof(ATI_2100c) + sizeof(ATI_2100) + sizeof(ATI_header))) / sizeof(ATI_2100m);

   // find all destinations that we have (loop over 1200 bit flag group looking)
   list<FASIT_TCP*> tcps;
   int t = 1; // start at 1 as 0 represents unassigned
   for (int i = 149; i>=0; i++) { // the entire group is big endien
      for (int k = 0 ; k < 8; k++) { // 8 bits per int
         if (msg->dest[i] & (1 << k)) {
            FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(t);
            if (tcp != NULL) {
               tcps.push_back(tcp);
            }
         }
         t++;
      }
   }
   if (tcps.empty()) { return 0; }

   // TODO -- evaluate delay field (as set in the ATI_header length field)

   // send each message found to each destination found, the order doesn't really matter:
   //    all the messages will be actually sent in the order the individual TCP sockets
   //    become writeable, and even then each will send all (1-4) messages in a single chunk
   list<FASIT_TCP*>::iterator l;
   while (nmsgm--) {
      l = tcps.begin();
      while (l != tcps.end()) {
         // recombobulate to TCP message
         FASIT_header fhdr;
         FASIT_TCP *tcp = *l;
         defHeader(2100,&fhdr);
         fhdr.seq = tcp->getSequence();
         fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2100);
         FASIT_2100 fmsg = msgm[nmsgm]->embed;

         // send
         tcp->queueMsg(&fhdr, sizeof(FASIT_header));
         tcp->queueMsg(&fmsg, sizeof(FASIT_2100));

         // next
         l++;
      }
   }

   return 0;
}

int FASIT_Serial::handle_2101(int start, int end) {
   // map header (no message body)
   ATI_header *hdr = (ATI_header*)(rbuf + start);

   // do we have the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // handle as a "good" 2101
   FASIT_2101b fmsg;
   fmsg.resp = 'S';
   return handle_as_2101(fmsg, tcp);
}

int FASIT_Serial::handle_43061(int start, int end) {
   // map header (no message body)
   ATI_header *hdr = (ATI_header*)(rbuf + start);

   // do we have the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // handle as a "bad" 2101
   FASIT_2101b fmsg;
   fmsg.resp = 'F';
   return handle_as_2101(fmsg, tcp);
}

int FASIT_Serial::handle_47157(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_47157 *msg = (ATI_47157*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // handle as an "other" 2101
   FASIT_2101b fmsg = msg->embed;
   return handle_as_2101(fmsg, tcp);
}

int FASIT_Serial::handle_as_2101(FASIT_2101b bmsg, FASIT_TCP *tcp) {
   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(2101,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2101);
   FASIT_2101 fmsg;
   fmsg.body = bmsg;
   fmsg.response = tcp->getResponse(2100);

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));
   tcp->queueMsg(&fmsg, sizeof(FASIT_2101));

   return 0;
}

int FASIT_Serial::handle_2111(int start, int end) {
   FASIT_2111b body;

   // map header
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   if (end - start == sizeof(ATI_header) + sizeof(ATI_2111s)) {
      // map small message
      ATI_2111s *msg = (ATI_2111s*)(rbuf + start + sizeof(ATI_header));

      // get device id and flags (small version uses ATI's MAC prefix)
      body.flags = msg->flags;
      body.devid = 0;
      body.devid = ((__uint64_t)ATI_MAC1 << 40) & ((__uint64_t)ATI_MAC2 << 32) & ((__uint64_t)ATI_MAC3 << 24) &
                   ((__uint64_t)msg->mac[0] << 16) & ((__uint64_t)msg->mac[1] << 8) & ((__uint64_t)msg->mac[2]);
   } else {
      // map large message
      ATI_2111f *msg = (ATI_2111f*)(rbuf + start + sizeof(ATI_header));

      // get device id and flags (full version passes entire MAC address)
      body.flags = msg->flags;
      body.devid = 0;
      body.devid = ((__uint64_t)msg->mac[0] << 40) & ((__uint64_t)msg->mac[1] << 32) & ((__uint64_t)msg->mac[2] << 24) &
                   ((__uint64_t)msg->mac[3] << 16) & ((__uint64_t)msg->mac[4] << 8) & ((__uint64_t)msg->mac[5]);
   }

   // are we the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(2111,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2111);
   FASIT_2111 fmsg;
   fmsg.body = body;
   fmsg.response = tcp->getResponse(100);

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));
   tcp->queueMsg(&fmsg, sizeof(FASIT_2111));

   return 0;
}

int FASIT_Serial::handle_2102(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_2102 *msg = (ATI_2102*)(rbuf + start + sizeof(ATI_header));
   ATI_2102x *xmsg = NULL;

   // map extended message?
   if (end - start > sizeof(ATI_header) + sizeof(ATI_2102)) {
      xmsg = (ATI_2102x*)(rbuf + start + sizeof(ATI_header) + sizeof(ATI_2102));
   }

   // do we have the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(2102,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2102);
   FASIT_2102 fmsg;
   if (xmsg == NULL) {
      // extended message not found, retrieve from remembered values and from expanding values given in compact message
      fmsg.body.pstatus = msg->pstatus;
      fmsg.body.fault = 0;
      switch (msg->exp) {
         case 0  : fmsg.body.exp = 0;  break;
         case 1  : fmsg.body.exp = 45; break;
         case 2  : fmsg.body.exp = 90; break;
         default : fmsg.body.exp = 0;  break;
      }
      fmsg.body.asp = 0;
      fmsg.body.dir = 0;
      fmsg.body.move = msg->move ? tcp->getMoveReq() : 0;
      fmsg.body.speed = msg->speed / 100.0;
      fmsg.body.pos = msg->pos;
      fmsg.body.type = msg->type;
      fmsg.body.hit = msg->hit;
      fmsg.body.hit_conf = tcp->getHitReq();
   } else {
      // extended message found, just copy it
      fmsg.body = xmsg->embed;
   }
   fmsg.response = tcp->getResponse(2100);

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));
   tcp->queueMsg(&fmsg, sizeof(FASIT_2102));

   return 0;
}

int FASIT_Serial::handle_2114(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_2114 *msg = (ATI_2114*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct destination?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(msg->dest);
   if (tcp == NULL) { return 0; }

   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(2114,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2114);
   FASIT_2114 fmsg = msg->embed;

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));
   tcp->queueMsg(&fmsg, sizeof(FASIT_2114));

   return 0;
}

int FASIT_Serial::handle_2115(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_2115 *msg = (ATI_2115*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(2115,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2115);
   FASIT_2115 fmsg;
   fmsg.body = msg->embed;
   fmsg.response = tcp->getResponse(2114);

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));
   tcp->queueMsg(&fmsg, sizeof(FASIT_2115));

   return 0;
}

int FASIT_Serial::handle_2110(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_2110 *msg = (ATI_2110*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct destination?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(msg->dest);
   if (tcp == NULL) { return 0; }

   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(2110,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2110);
   FASIT_2110 fmsg = msg->embed;

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));
   tcp->queueMsg(&fmsg, sizeof(FASIT_2110));

   return 0;
}

int FASIT_Serial::handle_2112(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_2112 *msg = (ATI_2112*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(2112,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2112);
   FASIT_2112 fmsg;
   fmsg.body = msg->embed;
   fmsg.response = tcp->getResponse(2110);

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));
   tcp->queueMsg(&fmsg, sizeof(FASIT_2112));

   return 0;
}

int FASIT_Serial::handle_2113(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_2113 *msg = (ATI_2113*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // recombobulate to TCP message
   FASIT_header fhdr;
   defHeader(2113,&fhdr);
   fhdr.seq = tcp->getSequence();
   fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2113);
   FASIT_2113 fmsg;
   fmsg.body = msg->embed;
   fmsg.response = tcp->getResponse(2100);

   // send
   tcp->queueMsg(&fhdr, sizeof(FASIT_header));
   tcp->queueMsg(&fmsg, sizeof(FASIT_2113));

   return 0;
}

int FASIT_Serial::handle_63556(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_63556 *msg = (ATI_63556*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct destination?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(msg->dest);
   if (tcp == NULL) { return 0; }

   // retrieve the zone items remembered for this tcp connection
   vector<struct FASIT_2006z> *zones = tcp->get2006zones();
   ATI_42966 req;
   bool deleteLater = false;
   for (int i=0; i<8; i++) {
      req.zones[i].znum = msg->zones[i]; // these zone numbers have no correlation to the znum field in FASIT_2006z
      if (req.zones[i].znum < zones->size()) { // the 0xffff "not-a-valid-zone" indicator will always be bigger than size
         req.zones[i] = (*zones)[msg->zones[i]]; // copy from list
      }
   }
   ATI_header reqh = createHeader(42966, tcp->getTnum(), &req, sizeof(ATI_42966));
   req.crc = crc8(&req, 0, sizeof(ATI_42966) - sizeof(req.crc)); // don't crc the crc field

   // send back on all serial devices
   queueMsgAll(&reqh, sizeof(ATI_header));
   queueMsgAll(&req, sizeof(ATI_42966));

   return 0;
}

int FASIT_Serial::handle_42966(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_42966 *msg = (ATI_42966*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct source?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(hdr->source);
   if (tcp == NULL) { return 0; }

   // retrieve the zone items and remember for this tcp connection
   vector<struct FASIT_2006z> *zones = tcp->get2006zones();
   FASIT_2006b zhdr = tcp->get2006base();

   // remember each zone sent
   for (int i=0; i<8; i++) {
      if (msg->zones[i].znum != 0xffff) { // check for valid zone
         zones->push_back(msg->zones[i]);
      }
   }

   // ask for the next chunk of the message?
   if (zones->size() < zhdr.zones) {
      ATI_63556 req;
      req.dest = hdr->source;
      for (int i=0; i<8; i++) { // ask for up to the first 8 zones
         if (i>zhdr.zones) {
            req.zones[i] = 0xffff; // no zone requested for this item
         } else {
            req.zones[i] = i + zones->size(); // these zone numbers have no correlation to the znum field in FASIT_2006z
         }
      }
      ATI_header reqh = createHeader(63556, BASE_STATION, &req, sizeof(ATI_63556));
      req.crc = crc8(&req, 0, sizeof(ATI_63556) - sizeof(req.crc)); // don't crc the crc field

      // send back on all serial devices
      queueMsgAll(&reqh, sizeof(ATI_header));
      queueMsgAll(&req, sizeof(ATI_63556));
   } else {
      // last zone retreived, recombobulate to TCP message
      FASIT_header fhdr;
      defHeader(2006,&fhdr);
      fhdr.seq = tcp->getSequence();
      fhdr.length = sizeof(FASIT_header) + sizeof(FASIT_2006);
      FASIT_2006 fmsg;
      fmsg.body = zhdr;
      fmsg.response = tcp->getResponse(2000);

      // send zone header
      tcp->queueMsg(&fhdr, sizeof(FASIT_header));
      tcp->queueMsg(&fmsg, sizeof(FASIT_2006));
      vector<FASIT_2006z>::iterator zIt = zones->begin();

      // send all zones
      while(zIt != zones->end()) {
         FASIT_2006z zone = *zIt;
         tcp->queueMsg(&zone, sizeof(FASIT_2006z));
         zIt++;
      }
   }

   return 0;
}

int FASIT_Serial::handle_16000(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_16000 *msg = (ATI_16000*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct destination?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(msg->dest);
   if (tcp == NULL) { return 0; }

   // TODO -- enable or disable according to msg->enable

   return 0;
}

int FASIT_Serial::handle_16001(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_16001 *msg = (ATI_16001*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct destination?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(msg->dest);
   if (tcp == NULL) { return 0; }

   // TODO -- connect to shutdown switch driver to turn off power to board

   return 0;
}

int FASIT_Serial::handle_16002(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_16002 *msg = (ATI_16002*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct destination?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(msg->dest);
   if (tcp == NULL) { return 0; }

   // TODO -- resend prior message

   return 0;
}

int FASIT_Serial::handle_16003(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_16003 *msg = (ATI_16003*)(rbuf + start + sizeof(ATI_header));

   // the source is unassigned, so this will find the spawning class if we're the base station
   FASIT_TCP_Factory *factory = (FASIT_TCP_Factory*)findByTnum(hdr->source);
   if (factory == NULL) { return 0; }

   // spawn off a new connection to create a new FASIT_TCP
   FASIT_TCP *tcp = factory->newConn();
   if (tcp == NULL) { return 0; } // could not connect

   // send the assign message back
   ATI_16004 rep;
   rep.rand = msg->rand;
   rep.id = tcp->getTnum(); // the factory will create this number
   ATI_header reph = createHeader(16004, BASE_STATION, &rep, sizeof(ATI_16004));

   // send back on all serial devices
   queueMsgAll(&reph, sizeof(ATI_header));
   queueMsgAll(&rep, sizeof(ATI_16004));

   return 0;
}

int FASIT_Serial::handle_16004(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_16004 *msg = (ATI_16004*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct destination?
   FASIT_TCP *tcp = (FASIT_TCP*)findByRnum(msg->rand);
   if (tcp == NULL) { return 0; }

   // apply assignment
   tcp->setTnum(msg->id);

   return 0;
}

int FASIT_Serial::handle_16005(int start, int end) {
   // map header and message
   ATI_header *hdr = (ATI_header*)(rbuf + start);
   ATI_16005 *msg = (ATI_16005*)(rbuf + start + sizeof(ATI_header));

   // do we have the correct destination or is this a broadcast change?
   FASIT_TCP *tcp = (FASIT_TCP*)findByTnum(msg->dest);
   if (tcp == NULL && msg->dest != 0xffff && !msg->broadcast) { return 0; }

   // TODO -- change channel to msg->channel

   return 0;
}


