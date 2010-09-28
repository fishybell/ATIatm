using namespace std;

#include "fasit_tcp.h"
#include "serial.h"
#include "common.h"
#include "timeout.h"
#include <sys/time.h>
#include <arpa/inet.h>

// initialize static members
list<ATI_2100m> FASIT_TCP::commandList;
multimap<ATI_2100m, int, struct_comp<ATI_2100m> > FASIT_TCP::commandMap;

FASIT_TCP::FASIT_TCP(int fd) : Connection(fd) {
FUNCTION_START("::FASIT_TCP(int fd) : Connection(fd)")
   seq = needDelete = 0;

   // new connection, send new connection message over serial
   ATI_16003 msg;
   msg.rand = rnum;
   ATI_header hdr = createHeader(16003, UNASSIGNED, &msg, sizeof(ATI_16003)); // source unassigned for 16003
   SerialConnection::queueMsgAll(&hdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&msg, sizeof(ATI_16003));
   // TODO -- no response from server after 10 seconds? disconnect
   
FUNCTION_END("::FASIT_TCP(int fd) : Connection(fd)")
}

// special case constructor where we already know the target number
FASIT_TCP::FASIT_TCP(int fd, int tnum) : Connection(fd) {
FUNCTION_START("::FASIT_TCP(int fd, int tnum) : Connection(fd)")
   seq = needDelete = 0;

   setTnum(tnum);
FUNCTION_END("::FASIT_TCP(int fd, int tnum) : Connection(fd)")
}

// called when either ready to read or write; returns -1 if needs to be deleted afterwards
int FASIT_TCP::handleEvent(epoll_event *ev) {
HERE
   return handleReady(ev);
}

FASIT_TCP::~FASIT_TCP() {
FUNCTION_START("::~FASIT_TCP()")

   // disconnecting, tell the other side of the radio connection
   ATI_16006 msg;
   msg.dest = tnum; // send to myself
   ATI_header hdr = createHeader(16006, tnum, &msg, sizeof(ATI_16006));
   SerialConnection::queueMsgAll(&hdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&msg, sizeof(ATI_16006));

FUNCTION_END("::~FASIT_TCP()")
}

// macro used in parseData
#define HANDLE_FASIT(FASIT_NUM) case FASIT_NUM : if ( handle_ ## FASIT_NUM (start, end) == -1 ) { return -1; } ; break;

int FASIT_TCP::parseData(int size, char *buf) {
FUNCTION_START("::parseData(int size, char *buf)")
   IMSG("TCP %i read %i bytes of data\n", fd, size)

   if (needDelete) { // have we been marked for deletion?
      return -1; // -1 represents a "delete me" flag
   }

   addToBuffer(size, buf);

   int start, end, mnum;
   
   // read all available valid messages
HERE
   while ((mnum = validMessage(&start, &end)) != 0) {
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
      /* look for non-FASIT message */
      if (rbuf[*start] == '@') {
         // start character found, check each valid string
         if (strncmp("@Disable~", rbuf + *start, 9) == 0) {
            *end = *start + 9;
            return 16000;
         }
         if (strncmp("@Enable~", rbuf + *start, 8) == 0) {
            *end = *start + 8;
            return 16000;
         }
         if (strncmp("@Shutdown~", rbuf + *start, 10) == 0) {
            *end = *start + 10;
            return 16001;
         }
         if (strncmp("@Channel", rbuf + *start, 8) == 0) {
            *end = *start + 9;
            // look for end character
            while (*end < rsize) {
               if (rbuf[*end] == '~') {
                  return 16005;
               }
            }
         }
          if (strncmp("@AllChannel", rbuf + *start, 11) == 0) {
            *end = *start + 12;
            // look for end character
            while (*end < rsize) {
               if (rbuf[*end] == '~') {
                  return 16005;
               }
            }
         }
      }

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

FUNCTION_INT("::getResponse(int mnum)", resp)
   return resp;
}

void FASIT_TCP::seqForResp(int mnum, int seq) {
FUNCTION_START("::seqForResp(int mnum, int seq)")
   respMap[mnum] = seq;
FUNCTION_END("::seqForResp(int mnum, int seq)")
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

   // discombobulate to radio message
   ATI_100 rmsg;
   rmsg.dest = tnum; // destination is this connection
   ATI_header rhdr = createHeader(100, BASE_STATION, &rmsg, sizeof(ATI_100));

   // send message on all serial devices
   SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_100));

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

   // discombobulate to radio message
   ATI_2000 rmsg;
   rmsg.dest = tnum; // destination is this connection
   rmsg.embed = *msg;
   ATI_header rhdr = createHeader(2000, BASE_STATION, &rmsg, sizeof(ATI_2000));

   // send message on all serial devices
   SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2000));

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

   // discombobulate to radio message
   ATI_2004 rmsg;
   rmsg.embed = msg->body;
   ATI_header rhdr = createHeader(2004, tnum, &rmsg, sizeof(ATI_2004)); // source is this connection

   // send message on all serial devices
   SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2004));

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

   // determine if we only need to send the last 3 bytes of the device id
   bool isShort = false;
   __uint64_t devid = swap64(msg->body.devid);
   devid = (devid >> 24) & 0xffffff;
   isShort = (devid == (ATI_MAC1 << 16) | (ATI_MAC2 << 8) | (ATI_MAC3));
   devid = swap64(msg->body.devid);
   
   // discombobulate to radio message
   if (isShort) {
      ATI_2005s rmsg;
      rmsg.flags = msg->body.flags;
      rmsg.mac[0] = ((devid & ((__uint64_t)0xff << 16)) >> 16);
      rmsg.mac[1] = ((devid & ((__uint64_t)0xff << 8)) >> 8);
      rmsg.mac[2] = (devid & ((__uint64_t)0xff));
      ATI_header rhdr = createHeader(2005, tnum, &rmsg, sizeof(ATI_2005s)); // source is this connection

      // send message on all serial devices
      SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
      SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2005s));
   } else {
      ATI_2005f rmsg;
      rmsg.flags = msg->body.flags;
      rmsg.mac[0] = ((devid & ((__uint64_t)0xff << 40)) >> 40);
      rmsg.mac[1] = ((devid & ((__uint64_t)0xff << 32)) >> 32);
      rmsg.mac[2] = ((devid & ((__uint64_t)0xff << 24)) >> 24);
      rmsg.mac[3] = ((devid & ((__uint64_t)0xff << 16)) >> 16);
      rmsg.mac[4] = ((devid & ((__uint64_t)0xff << 8)) >> 8);
      rmsg.mac[5] = (devid & ((__uint64_t)0xff));
      ATI_header rhdr = createHeader(2005, tnum, &rmsg, sizeof(ATI_2005f)); // source is this connection

      // send message on all serial devices
      SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
      SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2005f));
   }

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

   // remember zone data
   int znum = ntohs(msg->body.zones);
   FASIT_2006z *zone = (FASIT_2006z*)(rbuf + start + sizeof(FASIT_header) + sizeof(FASIT_2006));
   while (znum--) {
      zones.push_back(*zone);
      zone++;
   }

   // discombobulate to radio message
   ATI_2006 rmsg;
   rmsg.embed = msg->body;
   ATI_header rhdr = createHeader(2006, tnum, &rmsg, sizeof(ATI_2006)); // source is this connection

   // send message on all serial devices
   SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2006));

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

   // make sure we map this destination
   ATI_2100m cmd;
   cmd.embed = *msg;
   cmd.reserved = 0x0000;
   if (commandMap.count(cmd) == 0) {
      // add command to the static list
      commandList.push_back(cmd);
   }
   commandMap.insert(pair<ATI_2100m,int>(cmd,this->tnum - 1)); // destination is this connection (convert tnum to zero based)
DMSG("sending hit configuration: %i %i %i %i %i\n", cmd.embed.on, cmd.embed.react, cmd.embed.sens, cmd.embed.mode, cmd.embed.burst)

   // check timeout timer
   if (Timeout::timedOut() == -1) {
      // timer not set, set to starting now
      Timeout::setStartTimeNow();
   }
   // make sure that we're an event to watch
   Timeout::setMaxWait(125); // wait up to 125 milliseconds between 2100 commands
   Timeout::addTimeoutEvent(BAKE_2100); // when we timeout, handle the BAKE_2100 event, which will combine all found 2100 messages and send them

   // check if we've waited for a total of 250 milliseconds already
   timeval sTime = Timeout::getStartTime(), eTime;
   gettimeofday(&eTime, NULL);
   if (eTime.tv_sec > sTime.tv_sec) {
      // move second difference to tv_usec field
      eTime.tv_usec += (eTime.tv_sec - sTime.tv_sec) * 1000000;
   }
   if (eTime.tv_usec - sTime.tv_usec > 250000) {
      // 250 milliseconds has passed since the first message arrived, bake now
      Bake_2100();
   }

FUNCTION_INT("::handle_2100(int start, int end)", 0)
   return 0;
}

void FASIT_TCP::Bake_2100() {
FUNCTION_START("::Bake_2100()")
   ATI_2100 destinations;
   memset(&destinations, 0, sizeof(ATI_2100));
   ATI_2100m messages[4];
   memset(&messages, 0, sizeof(ATI_2100m) * 4);

   // add up to 4 messages to the command
   int mnum = 0;
   list<ATI_2100m>::iterator cIt = commandList.begin();
   while (mnum < 4) {
      // does this command already exist in the list?
      bool valid = false;
      while (!valid) {
         // we're at the end?
         if (cIt == commandList.end()) { break; }

         for (int i = 0; i < 4 ; i++) {
            if (memcmp(&messages[i], &(*cIt), sizeof(ATI_2100m)) != 0) {
               valid = true;
               break;
            }
         }

         if (!valid) { cIt++; }
      }
      if (!valid) { break; } // no further viable commands were found

      // prepare iterator pair marking the beginging and ending in the multimap for the current message
      pair<multimap<ATI_2100m, int, struct_comp<ATI_2100m> >::iterator, multimap<ATI_2100m, int, struct_comp<ATI_2100m> >::iterator> ppp;
      ppp = commandMap.equal_range(*cIt);

      // find all destinations for this command
      if (mnum == 0) {
DMSG("adding first message:\n")
PRINT_HEX(*cIt)
         // first message, fill in main destination list
         for (multimap<ATI_2100m, int, struct_comp<ATI_2100m> >::iterator mIt = ppp.first; mIt != ppp.second; mIt++) {
            int index = 149 - ((*mIt).second/8); // inverse index in list to maintain destinations as a 1200-bit big endian bit field
            destinations.dest[index] |= (1 << ((*mIt).second % 8)); // set individual bit
DMSG("adding bit for tnum %i at index %i bit %i: %02x\n", (*mIt).second, index, ((*mIt).second % 8), destinations.dest[index])
         }

         // add to message list
         messages[mnum] = *cIt;
      } else {
DMSG("adding secondary message\n")
PRINT_HEX(*cIt)
         // not first message, fill in a secondary destination list
         ATI_2100 sdests;
         for (multimap<ATI_2100m, int, struct_comp<ATI_2100m> >::iterator mIt = ppp.first; mIt != ppp.second; mIt++) {
            int index = 149 - ((*mIt).second/8); // inverse index in list to maintain destinations as a 1200-bit big endian bit field
            sdests.dest[index] |= (1 << ((*mIt).second % 8)); // set individual bit
DMSG("adding bit for tnum %i at index %i bit %i: %02x\n", (*mIt).second, index, ((*mIt).second % 8), destinations.dest[index])
         }
 
         // compare the two lists
         int subset = true; // assume the main list is a subset until proven otherwise
         for (int i = 0; i < 150 && subset; i++) {
            for (int k = 0; k < 8 && subset; k++) {
               if (destinations.dest[i] & (1 << k)) {
                  // main list has this particular destination, does secondary list?
                  if (!(sdests.dest[i] & (1 << k))) {
                     // no, not a subset
                     subset = false;
                  }
               }
            }
         }

         // add to message list if the main destination list is just a subset of the secondary list
         if (subset) {
            messages[mnum] = *cIt;
         } else {
            mnum--; // try to fill this message slot with a different message
         }
      }

      // next message
      cIt++;
      mnum++;
   }

   // we're not sending any commands?
   if (mnum == 0) { return; }

   // clear out all mapped message/dest pairs being actually sent
   for (int m = 0; m < mnum; m++) {
      // for each command sent...
      pair<multimap<ATI_2100m, int, struct_comp<ATI_2100m> >::iterator, multimap<ATI_2100m, int, struct_comp<ATI_2100m> >::iterator> ppp;
      ppp = commandMap.equal_range(messages[m]);
     // ...find each destination...
      for (multimap<ATI_2100m, int, struct_comp<ATI_2100m> >::iterator mIt = ppp.first; mIt != ppp.second; mIt++) {
         // ...and see if we actually sent to it...
         int index = 150 - ((*mIt).second/8); // inverse index in list to maintain destinations as a 1200-bit big endian bit field
         if (destinations.dest[index] & (1 << ((*mIt).second % 8))) {
            // ...and delete it from the map if we do
            commandMap.erase(mIt);
         }
      }
   }

   // clear out the list of commands without destinations
   for (cIt == commandList.begin(); cIt != commandList.end(); cIt++) {
      if (commandMap.count(*cIt) == 0) {
         commandList.erase(cIt);
      }
   }

   // create header with the correct delay set (read the "internal fasit to radio protocol.odt" document for explanation of below)
   ATI_header hdr = createHeader(2100, BASE_STATION, &destinations, 15); // source is the base station
   int delay = 0;
   int command_three = 0;
   for (int i = 0; i < mnum; i++) {
      int accum_delay = 0;
      int command_two = 0;
      command_three &= 0x01;
      switch (messages[i].embed.cid) {
         default:
         case 1:
            delay += 5;
            break;
         case 2:
            command_two |= 0x01;
            // fall through
         case 4:
         case 6:
            if (accum_delay == 5) {
               accum_delay = 10;
            } else {
               accum_delay = 5;
            }
            break;
         case 5:
            if (accum_delay == 5) {
               accum_delay = 10;
            } else {
               accum_delay = 5;
            }
            command_three |= 0x11;
            break;
         case 3:
            command_three |= 0x11;
            delay += 11;
            break;
         case 7:
            command_two |= 0x10;
            break;
         case 8:
            delay += 20;
            break;
      }
      switch (command_two) {
         case 0x01:
            if (accum_delay == 5) {
               accum_delay = 10;
            } else {
               accum_delay = 5;
            }
            break;
         case 0x10:
            delay += 11;
            break;
         case 0x11:
            accum_delay = 11;
            break;
      }
      delay += accum_delay;
      if (command_three & 0x10) {
         delay += 11;
      }
   }
   if (command_three & 0x01) {
      delay += 22;
   }
   if (delay <= 5) {
      hdr.length = 0;
   } else if (delay <= 10) {
      hdr.length = 1;
   } else if (delay <= 18) {
      hdr.length = 2;
   } else if (delay <= 36) {
      hdr.length = 3;
   } else if (delay <= 72) {
      hdr.length = 4;
   } else if (delay <= 144) {
      hdr.length = 5;
   } else if (delay <= 288) {
      hdr.length = 6;
   } else {
      hdr.length = 7;
   }
   hdr.length |= 0x08;

   // recalculate parity for header
   hdr.parity = 0;
   hdr.parity = parity(&hdr, sizeof(ATI_header)) ^ parity(&destinations, 10);

   // combine and crc
   int size = sizeof(ATI_header) + sizeof(ATI_2100) + sizeof(ATI_2100c) + (sizeof(ATI_2100m) * mnum);
   char *buf = new char[size];
   memcpy(buf, &hdr, sizeof(ATI_header)); // copy header
   memcpy(buf + sizeof(ATI_header), &destinations, sizeof(ATI_2100)); // copy destiniation bitfield
   for (int i = 0; i < mnum; i++) {
      memcpy(buf + sizeof(ATI_header) + sizeof(ATI_2100) + (sizeof(ATI_2100m) * i), &messages[i], sizeof(ATI_2100m)); // copy messages
   }
   ATI_2100c crc;
   crc.crc = crc32(buf, 0, size - sizeof(ATI_2100c)); // don't crc the crc field
   memcpy(buf + size - sizeof(ATI_2100c), &crc, sizeof(ATI_2100c)); // copy crc

   // send all at once
   SerialConnection::queueMsgAll(buf, size);
FUNCTION_END("::Bake_2100()")
}


int FASIT_TCP::handle_2101(int start, int end) {
FUNCTION_START("::handle_2101(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2101 *msg = (FASIT_2101*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2101, ntohl(hdr->seq));

   // discombobulate to radio message
   ATI_header rhdr;
   ATI_47157 rmsg;
   switch (msg->body.resp) {
      case 'S' :
      case 's' :
         // no message body
         rhdr = createHeader(2101, tnum, NULL, 0); // source is this connection

         // send message on all serial devices
         SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
         break;
      case 'F' :
      case 'f' :
         // no message body
         rhdr = createHeader(43060, tnum, NULL, 0); // source is this connection

         // send message on all serial devices
         SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
         break;
      default :
         rmsg.embed = msg->body;
         ATI_header rhdr = createHeader(47157, tnum, &rmsg, sizeof(ATI_47157)); // source is this connection

         // send message on all serial devices
         SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
         SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_47157));
         break;
   }
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

   // determine if we only need to send the last 3 bytes of the device id
   bool isShort = false;
   __uint64_t devid = swap64(msg->body.devid);
   devid = (devid >> 24) & 0xffffff;
   isShort = (devid == (ATI_MAC1 << 16) | (ATI_MAC2 << 8) | (ATI_MAC3));
   devid = swap64(msg->body.devid);
   
   // discombobulate to radio message
DMSG("sending %s version of 2111\n", isShort ? "short" : "long")
   if (isShort) {
      ATI_2111s rmsg;
      rmsg.flags = msg->body.flags;
      rmsg.mac[0] = ((devid & (0xff << 16)) >> 16);
      rmsg.mac[1] = ((devid & (0xff << 8)) >> 8);
      rmsg.mac[2] = (devid & (0xff));
      ATI_header rhdr = createHeader(2111, tnum, &rmsg, sizeof(ATI_2111s)); // source is this connection

      // send message on all serial devices
      SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
      SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2111s));
   } else {
      ATI_2111f rmsg;
      rmsg.flags = msg->body.flags;
      rmsg.mac[0] = ((devid & ((__uint64_t)0xff << 40)) >> 40);
      rmsg.mac[1] = ((devid & ((__uint64_t)0xff << 32)) >> 32);
      rmsg.mac[2] = ((devid & ((__uint64_t)0xff << 24)) >> 24);
      rmsg.mac[3] = ((devid & ((__uint64_t)0xff << 16)) >> 16);
      rmsg.mac[4] = ((devid & ((__uint64_t)0xff << 8)) >> 8);
      rmsg.mac[5] = (devid & ((__uint64_t)0xff));
      ATI_header rhdr = createHeader(2111, tnum, &rmsg, sizeof(ATI_2111f)); // source is this connection

      // send message on all serial devices
      SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
      SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2111f));
   }

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

   // do we need to create the extended message as well?
   bool shortOnly = true;

   // discombobulate to radio message
   ATI_2102 rmsg;
   rmsg.pstatus = msg->body.pstatus;
   rmsg.type = msg->body.type;
   if (rmsg.type != msg->body.type) { shortOnly = false; } // check that the 3 bits allocated were enough
   rmsg.pos = ntohs(msg->body.pos);
   if (rmsg.pos != ntohs(msg->body.pos)) { shortOnly = false; } // check that the 10 bits allocated were enough
   switch (msg->body.exp) {
      case 0 : rmsg.exp = 0; break;
      case 45 : rmsg.exp = 1; break;
      case 90 : rmsg.exp = 2; break;
      default : shortOnly = false; break;
   }
   if (msg->body.asp != 0) { shortOnly = false; }
   if (msg->body.dir != 0) { shortOnly = false; }
   if (msg->body.move == getMoveReq()) {
      rmsg.move = 1;
   } else if (msg->body.move == 0) {
      rmsg.move = 0;
   } else {
      shortOnly = false;
   }
   rmsg.speed = static_cast<__uint16_t> (msg->body.speed * 100);
   if (rmsg.speed != static_cast<__uint16_t> (msg->body.speed * 100)) { shortOnly = false; } // check that the 12 bits allocated were enough
   rmsg.hit = ntohs(msg->body.hit);
   if (rmsg.hit != ntohs(msg->body.hit)) { shortOnly = false; } // check that the 12 bits allocated were enough
   if (memcmp(&msg->body.hit_conf, &hitReq, sizeof(hitReq)) != 0) {
      shortOnly = false;
DMSG("orig: %i %i %i %i %i\n", hitReq.on, hitReq.react, hitReq.sens, hitReq.mode, hitReq.burst)
DMSG("new: %i %i %i %i %i\n", msg->body.hit_conf.on, msg->body.hit_conf.react, msg->body.hit_conf.sens, msg->body.hit_conf.mode, msg->body.hit_conf.burst)
   }
DMSG("short 2102? %s\n", shortOnly ? "yes" : "no")
   ATI_header rhdr = createHeader(2102, tnum, &rmsg, sizeof(ATI_2102)); // source is this connection

   // doe we need to send extended message?
   if (!shortOnly) {
      ATI_2102x xmsg;
      xmsg.embed = msg->body;
      int size = sizeof(ATI_header) + sizeof(ATI_2102) + sizeof(ATI_2102x);
      char *buf = new char[size];
      memcpy(buf + sizeof(ATI_header), &rmsg, sizeof(ATI_2102)); // copy to correct place
      memcpy(buf + sizeof(ATI_header) + sizeof(ATI_2102), &xmsg, sizeof(ATI_2102x)); // copy to correct place
      rhdr = createHeader(2102, tnum, buf + sizeof(ATI_header), size- sizeof(ATI_header)); // recreate header for larger sized message
      memcpy(buf, &rhdr, sizeof(ATI_header)); // copy to correct place
      xmsg.crc = crc8(buf, 0, size - sizeof(xmsg.crc)); // don't crc the crc field

      // send full message on all serial devices
      SerialConnection::queueMsgAll(buf, size);
      delete [] buf;
   } else {
      // send small message on all serial devices
      SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
      SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2102));
   }

FUNCTION_INT("::handle_2102(int start, int end)", 0)
   return 0;
}

FASIT_2102h FASIT_TCP::getHitReq() {
FUNCTION_START("::getHitReq()")
   // when this is used, it needs to be in network byte order
   FASIT_2102h hitr;
   hitr.on = hitReq.on;
   hitr.react = hitReq.react;
   hitr.tokill = htons(hitReq.tokill);
   hitr.sens = htons(hitReq.sens);
   hitr.mode = hitReq.mode;
   hitr.burst = htons(hitReq.burst);
FUNCTION_HEX("::getHitReq()", &hitr)
   return hitr;
}

int FASIT_TCP::handle_2114(int start, int end) {
FUNCTION_START("::handle_2114(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2114 *msg = (FASIT_2114*)(rbuf + start + sizeof(FASIT_header));

   // remember sequence for this connection
   seqForResp(2114, ntohl(hdr->seq));

   // discombobulate to radio message
   ATI_2114 rmsg;
   rmsg.dest = tnum; // destination is this connection
   rmsg.embed = *msg;
   ATI_header rhdr = createHeader(2114, BASE_STATION, &rmsg, sizeof(ATI_2114));

   // send message on all serial devices
   SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2114));

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

   // discombobulate to radio message
   ATI_2115 rmsg;
   rmsg.embed = msg->body;
   ATI_header rhdr = createHeader(2115, tnum, &rmsg, sizeof(ATI_2115)); // source is this connection

   // send message on all serial devices
   SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2115));

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

   // discombobulate to radio message
   ATI_2110 rmsg;
   rmsg.dest = tnum; // destination is this connection
   rmsg.embed = *msg;
   ATI_header rhdr = createHeader(2110, BASE_STATION, &rmsg, sizeof(ATI_2110));

   // send message on all serial devices
   SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2110));

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

   // discombobulate to radio message
   ATI_2112 rmsg;
   rmsg.embed = msg->body;
   ATI_header rhdr = createHeader(2112, tnum, &rmsg, sizeof(ATI_2112)); // source is this connection

   // send message on all serial devices
   SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2112));

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

   // discombobulate to radio message
   ATI_2113 rmsg;
   rmsg.embed = msg->body;
   ATI_header rhdr = createHeader(2113, tnum, &rmsg, sizeof(ATI_2113)); // source is this connection
   rmsg.crc = crc8(&rmsg, 0, sizeof(ATI_2113) - sizeof(rmsg.crc)); // don't crc the crc field

   // send message on all serial devices
   SerialConnection::queueMsgAll(&rhdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&rmsg, sizeof(ATI_2113));

FUNCTION_INT("::handle_2113(int start, int end)", 0)
   return 0;
}


