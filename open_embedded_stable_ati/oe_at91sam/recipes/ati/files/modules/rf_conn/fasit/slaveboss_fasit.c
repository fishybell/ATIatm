#include <bits/types.h>

#include "mcp.h"
#include "slaveboss.h"

// for some reason we have a ntohs/htons, but no ntohf/htonf
float ntohf(float f) {
   __uint32_t holder = *(__uint32_t*)(&f), after;
   // byte starts as 1, 2, 3, 4, ends as 4, 3, 2, 1
   after = ((holder & 0x000000ff) << 24) | \
           ((holder & 0x0000ff00) << 8) | \
           ((holder & 0x00ff0000) >> 8) | \
           ((holder & 0xff000000) >> 24);

   return *(float*)(&after);
}

// we don't worry about clearing the data before a valid message, just up to the end
static void clearBuffer(fasit_connection_t *fc, int end) {
   if (end >= fc->fasit_ilen) {
      // clear the entire buffer
      fc->fasit_ilen = 0;
   } else {
      // clear out everything up to and including end
      char tbuf[FASIT_BUF_SIZE];
      memcpy(tbuf, fc->fasit_ibuf + (sizeof(char) * end), fc->fasit_ilen - end);
      memcpy(fc->fasit_ibuf, tbuf, fc->fasit_ilen - end);
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
   memcpy(fc->fasit_obuf + (sizeof(char) *fc->fasit_olen), msg, size);
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
      memcpy(&hdr, fc->fasit_ibuf + *start, hl);

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
         CHECK_LENGTH (14401);
         default:      // not a valid number, not a valid header
            break;
      }

      *start = *start + 1;
   }
   return 0;
}

// read a single FASIT message into given buffer, return do next
int fasitRead(int fd, char **dest, int *dests) {
   // read as much as possible
   *dests = read(fd, *dest, RF_BUF_SIZE);

   // did we read nothing?
   if (*dests <= 0) {
      *dests = 0;
      if (*dests == 0 || errno == EAGAIN) {
         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
         return rem_fasitEpoll;
      }
   }

   // data found, let the handler parse it
   return doNothing;
}

// write all FASIT messages for connection in fconns
int fasitWrite(fasit_connection_t *fc) {
   int s;

   // have something to write?
   if (fc->fasit_olen <= 0) {
      // we only send data, or listen for writability, if we have something to write
      return mark_fasitRead;
   }

   // write all the data we can
   s = write(fc->fasit, fc->fasit_obuf, fc->fasit_olen);

   // did it fail?
   if (s <= 0) {
      if (s == 0 || errno == EAGAIN) {
         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
         return rem_fasitEpoll;
      }
   } else if (s < fc->fasit_olen) {
      // we can't leave only a partial message going out, finish writing even if we block
      int opts;

      // change to blocking from non-blocking
      opts = fcntl(fc->fasit, F_GETFL); // grab existing flags
      if (opts < 0) {
         return rem_fasitEpoll;
      }
      opts = (opts ^ O_NONBLOCK); // remove nonblock from existing flags
      if (fcntl(fc->fasit, F_SETFL, opts) < 0) {
         return rem_fasitEpoll;
      }

      // loop until written (since we're blocking, it won't loop forever, just until timeout)
      while (s >= 0) {
         int ns = write(fc->fasit, fc->fasit_obuf + (sizeof(char) * s), fc->fasit_olen - s);
         if (ns < 0 && errno != EAGAIN) {
            // connection dead, remove it
            return rem_fasitEpoll;
         }
         s += ns; // increase total written, possibly by zero
      }

      // change to non-blocking from blocking
      opts = (opts | O_NONBLOCK); // add nonblock back into existing flags
      if (fcntl(fc->fasit, F_SETFL, opts) < 0) {
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

void addToBuffer(fasit_connection_t *fc, char *buf, int s) {
   // replace buffer?
   if (fc->fasit_ilen <= 0) {
      memcpy(fc->fasit_ibuf, buf, s);
      fc->fasit_ilen = s;
   } else {
      // add to buffer
      memcpy(fc->fasit_ibuf + (sizeof(char) * fc->fasit_ilen), buf, s);
      fc->fasit_ilen += s;
   }
}

// macro used in fasit2rf
#define HANDLE_FASIT(FASIT_NUM) case FASIT_NUM : return handle_ ## FASIT_NUM (fc, start, end) ; break;

// mangle one or more fasit messages into 1 rf message (often just caches information until needed by rf2fasit)
int fasit2rf(fasit_connection_t *fc, char *buf, int s) {
   int start, end, mnum;

   // check client
   if (!fc->rf) {
      return doNothing;
   }
  
   // read all available valid messages
   while ((mnum = validMessage(fc, &start, &end)) != 0) {
      DCMSG(RED,"Recieved FASIT message %d",mnum);
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
   return doNothing;
}

int send_100(fasit_connection_t *fc) {
   FASIT_header hdr;
   defHeader(fc, 100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header));
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   return mark_fasitWrite;
}

int send_2000(fasit_connection_t *fc, int zone) {
   FASIT_header hdr;
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
   bdy.zone = zone+1;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2000));
   return mark_fasitWrite;
}

int handle_2004(fasit_connection_t *fc, int start, int end) {
   // do nothing with this information
   return doNothing;
}

int handle_2005(fasit_connection_t *fc, int start, int end) {
   // copy until for later potential sending over RF
   memcpy(&fc->f2005_resp, fc->fasit_ibuf, end-start);
   // remember target type
   fc->target_type = RF_Type_BES;
   return doNothing;
}

int handle_2006(fasit_connection_t *fc, int start, int end) {
   // do nothing with this information
   return doNothing;
}

int send_2100_status_req(fasit_connection_t *fc) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = CID_Status_Request;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2100));
   return mark_fasitWrite;
}

int send_2100_power(fasit_connection_t *fc, int cmd) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = cmd;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2100));
   return mark_fasitWrite;
}

int send_2100_exposure(fasit_connection_t *fc, int exp) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = CID_Expose_Request;
   bdy.exp = exp;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2100));
   return mark_fasitWrite;
}

int send_2100_movement(fasit_connection_t *fc, int move, float speed) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = CID_Move_Request;
   bdy.move = move;
   bdy.speed = htonf(speed);

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_2100));
   return mark_fasitWrite;
}

int send_2100_conf_hit(fasit_connection_t *fc, int on, int hit, int react, int tokill, int sens, int mode, int burst) {
   FASIT_header hdr;
   FASIT_2100 bdy;
   defHeader(fc, 2100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2100));

   // fill in body
   memset(&bdy, 0, sizeof(FASIT_2100));
   bdy.cid = CID_Config_Hit_Sensor;
   bdy.on = on;
   bdy.hit = hit;
   bdy.react = react;
   bdy.tokill = tokill;
   bdy.sens = sens;
   bdy.mode = mode;
   bdy.burst = burst;

   // remember for later
   fc->hit_on = on;
   fc->hit_hit = hit;
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
   // copy until for later potential sending over RF
   memcpy(&fc->f2101_resp, fc->fasit_ibuf, end-start);
   return doNothing;
}

int handle_2102(fasit_connection_t *fc, int start, int end) {
   // copy until for later potential sending over RF
   memcpy(&fc->f2102_resp, fc->fasit_ibuf, end-start);
   // remember target type
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
   // remember hit sensing settings
   fc->hit_on = fc->f2102_resp.body.hit_conf.on;
   fc->hit_hit = fc->f2102_resp.body.hit;
   fc->hit_react = fc->f2102_resp.body.hit_conf.react;
   fc->hit_tokill = fc->f2102_resp.body.hit_conf.tokill;
   fc->hit_sens = fc->f2102_resp.body.hit_conf.sens;
   fc->hit_mode = fc->f2102_resp.body.hit_conf.mode;
   fc->hit_burst = fc->f2102_resp.body.hit_conf.burst;
   return doNothing;
}

int send_2110(fasit_connection_t *fc, int on, int mode, int idelay, int rdelay) {
   FASIT_header hdr;
   FASIT_2110 bdy;
   defHeader(fc, 2110, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2110));

   // fill in body
   memset(&bdy, 0, sizeof(FASIT_2110));
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
   // copy until for later potential sending over RF
   memcpy(&fc->f2111_resp, fc->fasit_ibuf, end-start);
   // remember if I have an MFS or not
   if (fc->f2111_resp.body.flags & PD_NES) {
      fc->has_MFS = 1;
   }
   // send out a status request to get target type
   send_2100_status_req(fc);
   return doNothing;
}

int handle_2112(fasit_connection_t *fc, int start, int end) {
   // copy until for later potential sending over RF
   memcpy(&fc->f2112_resp, fc->fasit_ibuf, end-start);
   return doNothing;
}

int handle_2113(fasit_connection_t *fc, int start, int end) {
   // copy until for later potential sending over RF
   memcpy(&fc->f2113_resp, fc->fasit_ibuf, end-start);
   return doNothing;
}

int send_2114(fasit_connection_t *fc) {
   // MILES not supported over RF
   return doNothing;
}

int handle_2115(fasit_connection_t *fc, int start, int end) {
   // do nothing with this information
   return doNothing;
}

int send_13110(fasit_connection_t *fc) {
   // Moon Glow not supported over RF
   return doNothing;
}

int handle_13112(fasit_connection_t *fc, int start, int end) {
   // do nothing with this information
   return doNothing;
}

int send_14110(fasit_connection_t *fc, int on) {
   FASIT_header hdr;
   FASIT_14110 bdy;
   defHeader(fc, 14110, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_14110));

   // fill in body
   memset(&bdy, 0, sizeof(FASIT_14110));
   bdy.on = on;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_14110));
   return mark_fasitWrite;
}

int handle_14112(fasit_connection_t *fc, int start, int end) {
   // do nothing with this information
   return doNothing;
}

int send_14200(fasit_connection_t *fc, int blank) {
   FASIT_header hdr;
   FASIT_14200 bdy;
   defHeader(fc, 14200, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_14200));

   // fill in body
   memset(&bdy, 0, sizeof(FASIT_14200));
   bdy.blank = blank;

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_14200));
   return mark_fasitWrite;
}

int send_14400(fasit_connection_t *fc, int cid, int length, char *data) {
   FASIT_header hdr;
   FASIT_14400 bdy;
   defHeader(fc, 14400, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_14400));

   // fill in body
   memset(&bdy, 0, sizeof(FASIT_14400));
   bdy.cid = cid;
   bdy.length = length;
   memcpy(bdy.data, data, length);

   // send
   queueMsg(fc, &hdr, sizeof(FASIT_header));
   queueMsg(fc, &bdy, sizeof(FASIT_14400));
   return mark_fasitWrite;
}

int handle_14401(fasit_connection_t *fc, int start, int end) {
   // do nothing with this information
   return doNothing;
}


