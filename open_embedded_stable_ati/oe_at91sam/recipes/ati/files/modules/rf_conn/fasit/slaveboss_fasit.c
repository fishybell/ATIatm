#include <bits/types.h>

#include "mcp.h"
#include "slaveboss.h"
#include "fasit_debug.h"

// we don't worry about clearing the data before a valid message, just up to the end
static void clearBuffer(fasit_connection_t *fc, int end) {
   if (end >= fc->fasit_ilen) {
      // clear the entire buffer
      fc->fasit_ilen = 0;
   } else {
      // clear out everything up to and including end
      char tbuf[FASIT_BUF_SIZE];
      D_memcpy(tbuf, fc->fasit_ibuf + (sizeof(char) * end), fc->fasit_ilen - end);
      D_memcpy(fc->fasit_ibuf, tbuf, fc->fasit_ilen - end);
      fc->fasit_ilen -= end;
   }
}

// add this message to the write buffer
// as this function does not actually write and will not cause the caller function to be
//   preempted, the caller may call this function multiple times to create a complete
//   message and be sure that the entire message is sent
static void queueMsg(fasit_connection_t *fc, void *msg, int size) {

   // check buffer remaining...
   if ((size + fc->fasit_olen) > FASIT_BUF_SIZE) {
      // buffer not big enough
      return;
   }

   // append to existing message
   D_memcpy(fc->fasit_obuf + (sizeof(char) *fc->fasit_olen), msg, size);
   fc->fasit_olen += size;
}

// get this connections next sequence number
static int getSequence(fasit_connection_t *fc) { return fc->seq++; }

// fill out default header information
static void defHeader(fasit_connection_t *fc, int mnum, FASIT_header *fhdr) {
   fhdr->num = htons(mnum);
   fhdr->icd1 = htons(1);
   fhdr->icd2 = htons(1);
   fhdr->rsrvd = htonl(0);
   fhdr->seq = htonl(getSequence(fc));
   switch (mnum) {
      case 100:
      case 2000:
      case 2004:
      case 2005:
      case 2006:
         fhdr->icd1 = htons(2);
         break;
   }
}

// macro used in validMessage function to check just the message length field of the header and call it good
#define END_CHECKS return hdr.num; break;
#define CHECK_LENGTH(FASIT_NUM) case FASIT_NUM : if (hdr.length != hl + sizeof( FASIT_ ## FASIT_NUM )) { break; }; if (hdr.length > (fc->fasit_ilen - *start)) { break; }; END_CHECKS; break;

// the start and end values may be set even if no valid message is found
static int validMessage(fasit_connection_t *fc, int *start, int *end) {
   FASIT_header hdr;
   int hl = sizeof(FASIT_header); // keep for later
   *start = 0;
   // loop through entire buffer, parsing starting at each character
   while (*start < fc->fasit_ilen) {
      /* look for FASIT message */
      if (*start > (fc->fasit_ilen - sizeof(FASIT_header))) {
         // if not big enough to hold a full message header don't look for a valid FASIT message
         *start = *start + 1;
         continue;
      }
      // use copy so I can manipulate it
      D_memcpy(&hdr, fc->fasit_ibuf + *start, hl);

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
      DDCMSG(D_PACKET,CYAN, "FASIT validMessage for %i", hdr.num);
      switch (hdr.num) {
         case 2006:         // don't check against length of FASIT_2006 struct
            if (hdr.length > (fc->fasit_ilen - *start)) { break; } // have the beginning of a valid message
            END_CHECKS;
         // these ones just look at msg length
         CHECK_LENGTH (2004);
         CHECK_LENGTH (2005);
         CHECK_LENGTH (2101);
         CHECK_LENGTH (2102);
         CHECK_LENGTH (2111);
         CHECK_LENGTH (2112);
         CHECK_LENGTH (2113);
         CHECK_LENGTH (2115);
         CHECK_LENGTH (13112);
         CHECK_LENGTH (14112);
         CHECK_LENGTH (15112);
         CHECK_LENGTH (14401);
         default:      // not a valid number, not a valid header
            break;
      }

      *start = *start + 1;
   }
//   DDCMSG(D_PACKET,CYAN, "FASIT invalid: %08X %i %i", fc->fasit_ibuf, *start, fc->fasit_ilen);
//   DDCMSG_HEXB(D_PACKET,CYAN, "FASIT invalid:", fc->fasit_ibuf+*start, fc->fasit_ilen-*start);
   return 0;
}

// read a single FASIT message into given buffer, return do next
int fasitRead(int fd, char *dest, int *dests) {
   int err;
   DDCMSG(D_PACKET,YELLOW, "FASIT READING");
   // read as much as possible
   *dests = read(fd, dest, RF_BUF_SIZE);
   err = errno; // save errno

   DDCMSG(D_PACKET,YELLOW, "FASIT READ %i BYTES", *dests);

   // did we read nothing?
   if (*dests <= 0) {
      DDCMSG(D_PACKET,CYAN, "FASIT ERROR: %i", err);
      PERROR("Error: ");
      if (err == EAGAIN) {
         // try again later
         *dests = 0;
         return doNothing;
      } else {
         // connection dead, remove it
         *dests = 0;
PERROR("FASIT Died because ");
DDCMSG(D_PACKET,CYAN, "FASIT Dead at %s:%i", __FILE__, __LINE__);
         return rem_fasitEpoll;
      }
   }

   // data found, let the handler parse it
   return doNothing;
}

// write all FASIT messages for connection in fconns
int fasitWrite(fasit_connection_t *fc) {
   int s, err;
   DDCMSG(D_PACKET,BLUE, "FASIT WRITE");

   // have something to write?
   if (fc->fasit_olen <= 0) {
      // we only send data, or listen for writability, if we have something to write
      return mark_fasitRead;
   }

   // write all the data we can
   s = write(fc->fasit, fc->fasit_obuf, fc->fasit_olen);
   err = errno; // save errno
   DDCMSG(D_PACKET,BLUE, "FASIT WROTE %i BYTES", s);
   debugFASIT(blue, fc->fasit_obuf, fc->fasit_olen);

   // did it fail?
   if (s <= 0) {
      if (s == 0 || err == EAGAIN) {
         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
PERROR("FASIT Died because ");
DDCMSG(D_PACKET,CYAN, "FASIT Dead at %s:%i", __FILE__, __LINE__);
         return rem_fasitEpoll;
      }
   } else if (s < fc->fasit_olen) {
      // we can't leave only a partial message going out, finish writing even if we block
      int opts;

      // change to blocking from non-blocking
      opts = fcntl(fc->fasit, F_GETFL); // grab existing flags
      if (opts < 0) {
PERROR("FASIT Died because ");
DDCMSG(D_PACKET,CYAN, "FASIT Dead at %s:%i", __FILE__, __LINE__);
         return rem_fasitEpoll;
      }
      opts = (opts ^ O_NONBLOCK); // remove nonblock from existing flags
      if (fcntl(fc->fasit, F_SETFL, opts) < 0) {
PERROR("FASIT Died because ");
DDCMSG(D_PACKET,CYAN, "FASIT Dead at %s:%i", __FILE__, __LINE__);
         return rem_fasitEpoll;
      }

      // loop until written (since we're blocking, it won't loop forever, just until timeout)
      while (s >= 0) {
         int ns = write(fc->fasit, fc->fasit_obuf + (sizeof(char) * s), fc->fasit_olen - s);
         if (ns < 0 && err != EAGAIN) {
            // connection dead, remove it
PERROR("FASIT Died because ");
DDCMSG(D_PACKET,CYAN, "FASIT Dead at %s:%i", __FILE__, __LINE__);
            return rem_fasitEpoll;
         }
         s += ns; // increase total written, possibly by zero
      }

      // change to non-blocking from blocking
      opts = (opts | O_NONBLOCK); // add nonblock back into existing flags
      if (fcntl(fc->fasit, F_SETFL, opts) < 0) {
PERROR("FASIT Died because ");
DDCMSG(D_PACKET,CYAN, "FASIT Dead at %s:%i", __FILE__, __LINE__);
         return rem_fasitEpoll;
      }

      // don't try writing again
      return mark_fasitRead;
   } else {
      // everything was written, clear write buffer
      fc->fasit_olen = 0;

      // don't try writing again
      return mark_fasitRead;
   }

   // partial success, leave writeable so we try again
   return doNothing;
}

void addToFASITBuffer(fasit_connection_t *fc, char *buf, int s) {
   // replace buffer?
   if (fc->fasit_ilen <= 0) {
      D_memcpy(fc->fasit_ibuf, buf, s);
      fc->fasit_ilen = s;
   } else {
      // add to buffer
      D_memcpy(fc->fasit_ibuf + (sizeof(char) * fc->fasit_ilen), buf, s);
      fc->fasit_ilen += s;
   }
}

// macro used in fasit2rf
#define HANDLE_FASIT(FASIT_NUM) case FASIT_NUM : retval = handle_ ## FASIT_NUM (fc, start, end) ; break;

// mangle one or more fasit messages into 1 rf message (often just caches information until needed by rf2fasit)
int fasit2rf(fasit_connection_t *fc, char *buf, int s) {
   int start, end, mnum;
   int retval = doNothing;
   DDCMSG(D_PACKET,CYAN, "FASIT 2 RF");

   // check client
   if (!fc->rf) {
      return doNothing;
   }
  
   // read all available valid messages
   debugFASIT(YELLOW, fc->fasit_ibuf, fc->fasit_ilen);
   while (retval == doNothing && (mnum = validMessage(fc, &start, &end)) != 0) {
      DDCMSG(D_PACKET,CYAN,"Recieved FASIT message %d",mnum);
      switch (mnum) {
         HANDLE_FASIT (2004);
         HANDLE_FASIT (2005);
         HANDLE_FASIT (2006);
         HANDLE_FASIT (2101);
         HANDLE_FASIT (2102);
         HANDLE_FASIT (2111);
         HANDLE_FASIT (2112);
         HANDLE_FASIT (2113);
         HANDLE_FASIT (2115);
         HANDLE_FASIT (13112);
         HANDLE_FASIT (14112);
         HANDLE_FASIT (14401);
         default:
            break;
      }
      clearBuffer(fc, end); // clear out last message
   }
   return retval;
}

int send_100(fasit_connection_t *fc) {
   FASIT_header hdr;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_100(%08X)", fc);
   defHeader(fc, 100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header));
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   return mark_fasitWrite;
}

int send_2000(fasit_connection_t *fc, int zone) {
   FASIT_header hdr;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_2000(%08X)", fc);
   defHeader(fc, 2000, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2000));
   FASIT_2000 bdy;

   // first time through?
   if (fc->p_fire == 0) {
      // send "set" fire command
      bdy.cid = 3;
      fc->p_fire = 1; // next time send "fire" fire command
   } else {
      // second time through...
      bdy.cid = 4;
      fc->p_fire = 0; // next time send "set" fire command again
   }

   // change zone from 0-7 to 1-x
   bdy.zone = htons(zone+1);

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2000));
   return mark_fasitWrite;
}

int handle_2004(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_2004(%8p, %i, %i)", fc, start, end);
   // do nothing with this information
   return doNothing;
}

int handle_2005(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_2005(%8p, %i, %i)", fc, start, end);
   // copy until for later potential sending over RF
   D_memcpy(&fc->f2005_resp, fc->fasit_ibuf+sizeof(FASIT_header)+start, sizeof(FASIT_2005));
   // remember target type
   fc->target_type = RF_Type_BES;
   fc->devid = (fc->f2005_resp.body.devid & 0xffll << (8*7)) >> (8*7) |
               (fc->f2005_resp.body.devid & 0xffll << (8*6)) >> (8*5) |
               (fc->f2005_resp.body.devid & 0xffll << (8*5)) >> (8*3); // 3 most significant bytes and reversed from original (reversed) order
//   fc->devid = (fc->f2005_resp.body.devid & 0xffll << (8*7)) >> (8*5) |
//               (fc->f2005_resp.body.devid & 0xffll << (8*6)) >> (8*5) |
//               (fc->f2005_resp.body.devid & 0xffll << (8*5)) >> (8*5); // 3 most significant bytes in original (reversed) order
   DDCMSG(D_RF,BLACK, "Looking at: %08x", fc->devid);
   DDCMSG(D_RF,BLACK, "From pieces of 2005: %08llx", fc->f2005_resp.body.devid);
   return add_rfEpoll; // the RF is ready to work now that I have a type
}

int handle_2006(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_2006(%8p, %i, %i)", fc, start, end);
   // do nothing with this information
   return doNothing;
}

int send_2100_status_req(fasit_connection_t *fc) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_2100_status_req(%08X)", fc);
   DCMSG(RED, "send_2100_status_req(%08X)", fc);
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = CID_Status_Request;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2100));
   return mark_fasitWrite;
}

int send_2100_power(fasit_connection_t *fc, int cmd) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_2100_power(%8p, %i)", fc, cmd);
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = cmd;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2100));
   return mark_fasitWrite;
}

int send_2100_exposure(fasit_connection_t *fc, int exp) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_2100_exposure(%8p, %i)", fc, exp);
   DCMSG(CYAN, "send_2100_exposure(%8p, %i)", fc, exp);
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = CID_Expose_Request;
   bdy.exp = exp;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2100));
   return mark_fasitWrite;
}

int send_2100_reset(fasit_connection_t *fc) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_2100_reset(%8p)", fc);
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = CID_Reset_Device;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2100));
   return mark_fasitWrite;
}

int send_2100_movement(fasit_connection_t *fc, int move, float speed) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_2100_movement(%8p, %i, %f)", fc, move, speed);
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = CID_Move_Request;
   bdy.speed = htonf(speed);
   bdy.move = 0; // no movement if values below don't fit
   switch (move) {
      case 0:
      case 1:
      case 2:
         bdy.move = move;
         break;
      case 3:
         bdy.cid = CID_Continuous_Move_Request;
         break;
      case 4:
         bdy.cid = CID_Dock;
         break;
      case 5:
         bdy.cid = CID_Gohome;
         break;
      case 6:
         bdy.cid = CID_MoveAway;
         break;
   }

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2100));
   return mark_fasitWrite;
}

int send_2100_estop(fasit_connection_t *fc) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_2100_estop(%8p)", fc);
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = CID_Stop;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2100));
   return mark_fasitWrite;
}

int send_2100_conf_hit(fasit_connection_t *fc, int on, int hit, int react, int tokill, int sens, int mode, int burst) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_2100_conf_hit(%8p, %i, %i, %i, %i, %i, %i, %i)", fc, on, hit, react, tokill, sens, mode, burst);
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = CID_Config_Hit_Sensor;
   bdy.on = on;
   bdy.hit = htons(hit);
   bdy.react = react;
   bdy.tokill = htons(tokill);
   bdy.sens = htons(sens);
   bdy.mode = mode;
   bdy.burst = htons(burst);

   // remember for later
   fc->hit_on = on;
   DDCMSG(D_PACKET, RED, "Setting Hits to %i", hit);
   if (hit > 0) {
      // these fake hits will get logged when the 2102 message comes back -- log_NewHits(fc, hit);
   } else if (hit == 0) {
      // only reset when we get the event ack for the given event -- log_ResetHits(fc);
   }
   fc->hit_react = react;
   fc->hit_tokill = tokill;
   fc->hit_sens = sens;
   fc->hit_mode = mode;
   fc->hit_burst = burst;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2100));
   return mark_fasitWrite;
   
}

int handle_2101(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_2101(%8p, %i, %i)", fc, start, end);
   // copy until for later potential sending over RF
   D_memcpy(&fc->f2101_resp, fc->fasit_ibuf+sizeof(FASIT_header)+start, sizeof(FASIT_2101));
   // doing reset? if so, disconnect
   if (fc->doing_reset) {
      fc->doing_reset = 0;
DCMSG(RED, "Going to disconnect fasit connection...");
      return rem_fasitEpoll;
   } else {
      return doNothing;
   }
}

int handle_2102(fasit_connection_t *fc, int start, int end) {
   int retval = doNothing;
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_2102(%8p, %i, %i)", fc, start, end);
   // copy until for later potential sending over RF
   D_memcpy(&fc->f2102_resp, fc->fasit_ibuf+sizeof(FASIT_header)+start, sizeof(FASIT_2102));
   // remember target type
   //DCMSG(GRAY, "Got response from target");
   switch (fc->f2102_resp.body.type) {
      case Type_SIT:
         if (fc->has_MFS) {
            fc->target_type = RF_Type_SIT_W_MFS;
         } else {
            fc->target_type = RF_Type_SIT;
         }
         break;
      case Type_MIT:
         fc->target_type = RF_Type_MIT;
         break;
      case Type_SAT:
         fc->target_type = RF_Type_SAT;
         break;
      case Type_HSAT:
         fc->target_type = RF_Type_HSAT;
         break;
      case Type_MAT:
         fc->target_type = RF_Type_MAT;
         break;
      case Type_SES:
         fc->target_type = RF_Type_SES;
         break;
   }
   //DCMSG(GRAY, "Found 2102 expose: fc->f2102_resp.body.exp=%i", fc->f2102_resp.body.exp);
   // remember hit sensing settings
   fc->hit_on = fc->f2102_resp.body.hit_conf.on;
   fc->hit_react = fc->f2102_resp.body.hit_conf.react;
   fc->hit_tokill = htons(fc->f2102_resp.body.hit_conf.tokill);
   fc->hit_sens = htons(fc->f2102_resp.body.hit_conf.sens);
   fc->hit_mode = fc->f2102_resp.body.hit_conf.mode;
   fc->hit_burst = htons(fc->f2102_resp.body.hit_conf.burst);
   // check for new hits
   if (htons(fc->f2102_resp.body.hit) > 0  && htons(fc->f2102_resp.body.hit) != fc->last_2102_hit) { // only for greater than 0 and not the same as last time
      // log...
      log_NewHits(fc, htons(fc->f2102_resp.body.hit));
      // ...then reset back to fasit client
      retval |= send_2100_conf_hit(fc, 4, /* blank on conceal */
                                   0, /* reset hit value */
                                   fc->hit_react, /* remembered hit reaction */
                                   fc->hit_tokill, /* remembered hits to kill */
                                   fc->hit_sens, /* remembered hit sensitivity */
                                   fc->hit_mode, /* remembered hit mode */
                                   fc->hit_burst); /* remembered hit burst seperation */
   }
   fc->last_2102_hit = htons(fc->f2102_resp.body.hit);

   // remember fault status, but don't clear out existing faults unless there is a new one
   if (htons(fc->f2102_resp.body.fault)) {
      fc->last_fault = htons(fc->f2102_resp.body.fault);
   }

   // check to see if we have accomplished our exposure task and then switched back, all without a new command
   if (fc->did_exp_cmd == 0 && fc->f2102_resp.body.exp == 45) {
      // first step
      DDCMSG(D_NEW, MAGENTA, "MISSION 1/2 ACCOMPLISHED: %i", fc->future_exp);
      fc->did_exp_cmd = 1; // task half-way there, next 2102 should be the final part if everything goes well
   } else if (fc->did_exp_cmd == 1  && fc->f2102_resp.body.exp == fc->future_exp) {
      // second step
      DDCMSG(D_NEW, MAGENTA, "MISSION ACCOMPLISHED: %i", fc->future_exp);
      fc->did_exp_cmd = 2; // task accomplished, now look to see if we go back to the other way
   } else if (fc->did_exp_cmd == 2  && ((fc->f2102_resp.body.exp == 0 && fc->future_exp == 90) ||
                                 (fc->f2102_resp.body.exp == 90 && fc->future_exp == 0))) {
      // third, and final, step
      DDCMSG(D_NEW, MAGENTA, "MISSION OVER-ACCOMPLISHED: %i", fc->future_exp);
      fc->did_exp_cmd = 3; // task over-accomplished, subsequent changes in exposure will not effect this until a new command is issued
   }

   // check to see if we're waiting to send the information back
   if (fc->waiting_status_resp) {
      fc->waiting_status_resp = 0; // not waiting anymore
      //DCMSG(GRAY, "Going to send STATUS_RESP: %x", retval);
      return retval|send_STATUS_RESP(fc); // return appropriate information
   } else if (!added_rf_to_epoll) {
      added_rf_to_epoll = 1;
      return retval|add_rfEpoll; // the RF is ready to work now that I have a type
   } else {
      return retval; // just remember the status for later
   }
}

int send_2110(fasit_connection_t *fc, int on, int mode, int idelay, int rdelay) {
   FASIT_header hdr;
   FASIT_2110 bdy;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_2110(%8p, %i, %i, %i, %i)", fc, on, mode, idelay, rdelay);
   defHeader(fc, 2110, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2110));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_2110));
   bdy.on = on;
   bdy.mode = mode;
   bdy.idelay = idelay;
   bdy.rdelay = rdelay;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2110));
   return mark_fasitWrite;
}

int handle_2111(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_2111(%8p, %i, %i)", fc, start, end);
   // copy until for later potential sending over RF
   D_memcpy(&fc->f2111_resp, fc->fasit_ibuf+sizeof(FASIT_header)+start, sizeof(FASIT_2111));
   // remember if I have an MFS or not
   if (fc->f2111_resp.body.flags & PD_NES) {
      fc->has_MFS = 1;
   }
   fc->devid = (fc->f2111_resp.body.devid & 0xffll << (8*7)) >> (8*7) |
               (fc->f2111_resp.body.devid & 0xffll << (8*6)) >> (8*5) |
               (fc->f2111_resp.body.devid & 0xffll << (8*5)) >> (8*3); // 3 most significant bytes and reversed from original (reversed) order
//   fc->devid = (fc->f2111_resp.body.devid & 0xffll << (8*7)) >> (8*5) |
//               (fc->f2111_resp.body.devid & 0xffll << (8*6)) >> (8*5) |
//               (fc->f2111_resp.body.devid & 0xffll << (8*5)) >> (8*5); // 3 most significant bytes in original (reversed) order
   DDCMSG(D_RF,BLACK, "Looking at: %08x", fc->devid);
   DDCMSG(D_RF,BLACK, "From pieces of 2111: %08llx", fc->f2111_resp.body.devid);
   // send out a status request to get target type
   return send_2100_status_req(fc);
}

int handle_2112(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_2112(%8p, %i, %i)", fc, start, end);
   // copy until for later potential sending over RF
   D_memcpy(&fc->f2112_resp, fc->fasit_ibuf+sizeof(FASIT_header)+start, sizeof(FASIT_2112));
   return doNothing;
}

int handle_2113(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_2113(%8p, %i, %i)", fc, start, end);
   // copy until for later potential sending over RF
   D_memcpy(&fc->f2113_resp, fc->fasit_ibuf+sizeof(FASIT_header)+start, sizeof(FASIT_2113));
   return doNothing;
}

int send_2114(fasit_connection_t *fc, int on, int delay) {
   FASIT_header hdr;
   FASIT_2114 bdy;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_2114(%8p, %i)", fc, on);
   defHeader(fc, 2114, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2114));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_2114));
   bdy.code = 0;
   bdy.ammo = 0;
   bdy.player = 0;
   if (on) {
      bdy.delay = delay;
   } else {
      bdy.delay = 101; // more than 60 seconds = off
   }

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2114));
   return mark_fasitWrite;
}

int handle_2115(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_2115(%8p, %i, %i)", fc, start, end);
   // do nothing with this information
   return doNothing;
}

int send_13110(fasit_connection_t *fc, int on) {
   FASIT_header hdr;
   FASIT_13110 bdy;
   DDCMSG(D_NEW,CYAN, "send_13110(%8p, %i)", fc, on);
   defHeader(fc, 13110, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_13110));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_13110));
   bdy.on = on;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_13110));
   return mark_fasitWrite;
}

int handle_13112(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_13112(%8p, %i, %i)", fc, start, end);
   // do nothing with this information
   return doNothing;
}

int send_14110(fasit_connection_t *fc, int on) {
   FASIT_header hdr;
   FASIT_14110 bdy;
   DDCMSG(D_NEW,CYAN, "send_14110(%8p, %i)", fc, on);
   defHeader(fc, 14110, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_14110));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_14110));
   bdy.on = on;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_14110));
   return mark_fasitWrite;
}

int send_15110(fasit_connection_t *fc, int on) {
   FASIT_header hdr;
   FASIT_15110 bdy;
   DDCMSG(D_NEW,CYAN, "send_15110(%8p, %i)", fc, on);
   defHeader(fc, 15110, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_15110));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_15110));
   bdy.on = on;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_15110));
   return mark_fasitWrite;
}

int handle_14112(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_14112(%8p, %i, %i)", fc, start, end);
   // do nothing with this information
   return doNothing;
}

int handle_15112(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_15112(%8p, %i, %i)", fc, start, end);
   // do nothing with this information
   return doNothing;
}

int send_14200(fasit_connection_t *fc, int blank) {
   FASIT_header hdr;
   FASIT_14200 bdy;
   DDCMSG(D_NEW,CYAN, "send_14200(%8p, %i)", fc, blank);
   defHeader(fc, 14200, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_14200));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_14200));
   bdy.blank = htons(blank);

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_14200));
   return mark_fasitWrite;
}

int send_14400(fasit_connection_t *fc, int cid, int length, char *data) {
   FASIT_header hdr;
   FASIT_14400 bdy;
   DDCMSG(D_PACKET|D_VERY,CYAN, "send_14200(%8p, %i, %i, %08X)", fc, cid, length, data);
   defHeader(fc, 14400, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_14400));

   // fill in body
   D_memset(&bdy, 0, sizeof(FASIT_14400));
   bdy.cid = cid;
   bdy.length = htons(length);
   D_memcpy(bdy.data, data, length);

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_14400));
   return mark_fasitWrite;
}

int handle_14401(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_PACKET|D_VERY,CYAN, "handle_14401(%8p, %i, %i)", fc, start, end);
   // do nothing with this information
   return doNothing;
}


