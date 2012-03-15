#include "mcp.h"
#include "slaveboss.h"
#include "rf.h"
#include "eeprom.h"
#include "rf_debug.h"

// we don't worry about clearing the data before a valid message, just up to the end
static void clearBuffer(fasit_connection_t *fc, int end) {
   if (end >= fc->rf_ilen) {
      // clear the entire buffer
      fc->rf_ilen = 0;
   } else {
      // clear out everything up to and including end
      char tbuf[RF_BUF_SIZE];
      D_memcpy(tbuf, fc->rf_ibuf + (sizeof(char) * end), fc->rf_ilen - end);
      D_memcpy(fc->rf_ibuf, tbuf, fc->rf_ilen - end);
      fc->rf_ilen -= end;
   }
}

// add this message to the write buffer
// as this function does not actually write and will not cause the caller function to be
//   preempted, the caller may call this function multiple times to create a complete
//   message and be sure that the entire message is sent
static void queueMsg(fasit_connection_t *fc, void *msg, int size) {

   // check buffer remaining...
   if ((size + fc->rf_olen) > RF_BUF_SIZE) {
      // buffer not big enough
      return;
   }

   // append to existing message
   D_memcpy(fc->rf_obuf + (sizeof(char) *fc->rf_olen), msg, size);
   fc->rf_olen += size;
}

// see if the given packet is for this fasit connection
static int packetForMe(fasit_connection_t *fc, int start) {
   LB_packet_t *hdr = (LB_packet_t*)(fc->rf_ibuf + start);
   int i, j;
   DDCMSG_HEXB(D_RF, RED, "RF packetForMe:", fc->rf_ibuf+start, fc->rf_ilen-start);
   if (hdr->cmd == LBC_REQUEST_NEW) {
      // special case check: check reregister or devid range
      LB_request_new_t *msg = (LB_request_new_t*)hdr;

      if (msg->low_dev <= fc->devid && msg->low_dev+31 >= fc->devid) {
         if (msg->forget_addr&BV(fc->devid - msg->low_dev)){    // checks if our bit forget bit is set
            DDCMSG(D_RF,RED, "RF Packet for my devid range: %x:%x  and forget bit is set for our devid", msg->low_dev, msg->low_dev+31);
            return 1;    // it is for us, (and up to 31 others)  and the forget bit is set so in HANDLE request_new we mustchange our address to 2047
         } else if (fc->id==2047){
            DDCMSG(D_RF,RED, "RF Packet for my devid range: %x:%x  and our address is 2047", msg->low_dev, msg->low_dev+31);
            return 1;    // we have no address so it is for us (and up to 31 other slaves)
         }
      } else if (fc->target_type == RF_Type_Unknown) {
DDCMSG(D_RF,RED, "RF Packet ignored based on not having finished connection: %x", msg->low_dev);
         return 0; // doesn't matter if I'm in the range or not, I've haven't finished connecting
      } else if (fc->id != 2047) {
DDCMSG(D_RF,RED, "RF Packet ignored based on already having registered as %i: %x", fc->id, msg->low_dev);
         return 0; // have already registered
      }
DDCMSG(D_RF,RED, "RF Packet outside my devid range: %x  OR forget bit not set OR our address %d not 2047", msg->low_dev,fc->id);
      return 0;
   } else if (hdr->cmd == LBC_ASSIGN_ADDR) {
      // special case: check devid match (ignore reregister bit if it still exists in packet)
      LB_assign_addr_t *msg = (LB_assign_addr_t*)hdr;
      if (msg->devid == fc->devid) {
DDCMSG(D_RF,RED, "RF Packet for my devid: %x:%x", msg->devid, fc->devid);
         return 1;
      } else if (fc->target_type == RF_Type_Unknown) {
DDCMSG(D_RF,RED, "RF Packet ignored based on not having finished connection: %x", msg->devid);
         return 0;
      } else {
DDCMSG(D_RF,RED, "RF Packet for someone else's devid: %x:%x", msg->devid, fc->devid);
         return 0;
      }
   }
   if (hdr->addr == fc->id) {
DDCMSG(D_RF,RED, "RF Packet for me: %i", hdr->addr);
      return 1; // directly for me
   }

   for (i = 0; i<MAX_GROUPS; i++) {
      if (hdr->addr == fc->groups[i]) {
         for (j = 0; j<MAX_GROUPS; j++) {
            if (hdr->addr == fc->groups_disabled[j]) {
DDCMSG(D_RF,RED, "RF Packet for disabled group %i", hdr->addr);
               return 0; // is one of my groups, but it's disabled
            }
         }
DDCMSG(D_RF,RED, "RF Packet for enabled group %i", hdr->addr);
         return 1; // is one of my groups
      }
   }

   // not for me
DDCMSG(D_RF,RED, "RF Packet for other listener %i:%i", hdr->addr, fc->id);
   return 0;
}

// read a single RF message into given buffer, return do next
int rfRead(int fd, char *dest, int *dests) {
   int err;
   DDCMSG(D_RF,YELLOW, "RF READING");
   // read as much as possible
   *dests = read(fd, dest, RF_BUF_SIZE);
   err = errno; // save errno
   DDCMSG(D_RF,YELLOW, "RF READ %i BYTES", *dests);

   // did we read nothing?
   if (*dests <= 0) {
      if (err == EAGAIN) {
         // try again later
         *dests = 0;
         return doNothing;
      } else {
         // connection dead, remove it
         *dests = 0;
perror("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %i", __LINE__);
         return rem_rfEpoll;
      }
   }

   // data found, let the handler parse it
   return doNothing;
}

// write all RF messages for connection in fconns
int rfWrite(fasit_connection_t *fc) {
   int s, err;
   DDCMSG(D_RF,BLUE, "RF WRITE");

   // have something to write?
   if (fc->rf_olen <= 0) {
      // we only send data, or listen for writability, if we have something to write
      return mark_rfRead;
   }

   // write all the data we can
   s = write(fc->rf, fc->rf_obuf, fc->rf_olen);
   err = errno; // save errno
   DDCMSG(D_RF,BLUE, "RF WROTE %i BYTES", s);
   debugRF(blue, fc->rf_obuf, fc->rf_olen);

   // did it fail?
   if (s <= 0) {
      if (s == 0 || err == EAGAIN) {
         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
perror("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %i", __LINE__);
         return rem_rfEpoll;
      }
   } else if (s < fc->rf_olen) {
      // we can't leave only a partial message going out, finish writing even if we block
      int opts;

      // change to blocking from non-blocking
      opts = fcntl(fc->rf, F_GETFL); // grab existing flags
      if (opts < 0) {
perror("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %i", __LINE__);
         return rem_rfEpoll;
      }
      opts = (opts ^ O_NONBLOCK); // remove nonblock from existing flags
      if (fcntl(fc->rf, F_SETFL, opts) < 0) {
perror("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %i", __LINE__);
         return rem_rfEpoll;
      }

      // loop until written (since we're blocking, it won't loop forever, just until timeout)
      while (s >= 0) {
         int err, ns = write(fc->rf, fc->rf_obuf + (sizeof(char) * s), fc->rf_olen - s);
         err = errno; // save errno
         if (ns < 0 && err != EAGAIN) {
            // connection dead, remove it
perror("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %i", __LINE__);
            return rem_rfEpoll;
         }
         s += ns; // increase total written, possibly by zero
      }

      // change to non-blocking from blocking
      opts = (opts | O_NONBLOCK); // add nonblock back into existing flags
      if (fcntl(fc->rf, F_SETFL, opts) < 0) {
perror("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %i", __LINE__);
         return rem_rfEpoll;
      }

      // don't try writing again
      return mark_rfRead;
   } else {
      // everything was written, clear write buffer
      fc->rf_olen = 0;

      // don't try writing again
      return mark_rfRead;
   }

   // partial success, leave writeable so we try again
   return doNothing;
}

void addToRFBuffer(fasit_connection_t *fc, char *buf, int s) {
   // replace buffer?
   if (fc->rf_ilen <= 0) {
      D_memcpy(fc->rf_ibuf, buf, s);
      fc->rf_ilen = s;
   } else {
      // add to buffer
      D_memcpy(fc->rf_ibuf + (sizeof(char) * fc->rf_ilen), buf, s);
      fc->rf_ilen += s;
   }
}

// the start and end values may be set even if no valid message is found
static int validMessage(fasit_connection_t *fc, int *start, int *end) {
   LB_packet_t *hdr;
   int hl = -1;
   int mnum = -1;
   *start = 0;
   // loop through entire buffer, parsing starting at each character
   while (*start < fc->rf_ilen && mnum == -1) {
      // map the memory, we don't need to manipulate it
      hdr = (LB_packet_t*)(fc->rf_ibuf + *start);
      
      // check for valid message first
      hl = RF_size(hdr->cmd);
      if (((fc->rf_ibuf + fc->rf_ilen) - (fc->rf_ibuf + *start)) < hl) { *start = *start + 1; continue; } // invalid message length, or more likely, don't have all of the message yet

      *end = *start + hl;
      DDCMSG(D_RF,RED, "RF validMessage (cmd %i)", hdr->cmd);
      switch (hdr->cmd) {
         // they all have a crc, check it
         case LBC_EXPOSE:
         case LBC_MOVE:
         case LBC_CONFIGURE_HIT:
         case LBC_AUDIO_CONTROL:
         case LBC_PYRO_FIRE:
         case LBC_DEVICE_REG:
         case LBC_ASSIGN_ADDR:
         case LBC_STATUS_REQ:
         case LBC_STATUS_RESP_LIFTER:
         case LBC_STATUS_RESP_MOVER:
         case LBC_STATUS_RESP_EXT:
         case LBC_STATUS_NO_RESP:
         case LBC_GROUP_CONTROL:
         case LBC_POWER_CONTROL:
         case LBC_QEXPOSE:
         case LBC_QCONCEAL:
         case LBC_REQUEST_NEW:
            if (crc8(hdr) == 0) {
            //   DDCMSG(D_RF,RED, "RF VALID CRC");
               return hdr->cmd;
            } else {
            //   DDCMSG(D_RF,RED, "RF INVALID CRC");
            }
            break;

         // not a valid number, not a valid header
         default:
            break;
      }

      *start = *start + 1;
   }

//   DDCMSG(D_RF,RED, "RF invalid: %08X %i %i", fc->rf_ibuf, *start, fc->rf_ilen);
//   DDCMSG(D_RF,HEXB(RED, "RF invalid:", fc->rf_ibuf+*start, fc->rf_ilen-*start);
   return -1;
}

// macro used in rf2fasit
#define HANDLE_RF(RF_NUM) case LBC_ ## RF_NUM : retval = handle_ ## RF_NUM (fc, start, end) ; break;

// mangle an rf message into 1 or more fasit messages, and potentially respond with rf message
int rf2fasit(fasit_connection_t *fc, char *buf, int s) {
   int start, end, mnum, retval = doNothing;
   DDCMSG(D_RF,RED, "RF 2 FASIT");

   // check client
   if (!fc->rf) {
      return doNothing;
   }
  
   // read all available valid messages
   debugRF(RED, fc->rf_ibuf, fc->rf_ilen);
   while (retval == doNothing && (mnum = validMessage(fc, &start, &end)) != -1) {
      if (!packetForMe(fc, start)) {
         DDCMSG(D_RF,RED,"Ignored RF message %d",mnum);
      } else {
         DDCMSG(D_RF,RED,"Recieved RF message %d",mnum);
         debugRF(RED, fc->rf_ibuf + start, end-start);
         if (fc->sleeping && mnum != LBC_POWER_CONTROL) {
            // ignore message when sleeping
            DDCMSG(D_RF,RED,"Slept through RF message %d",mnum);
         } else {
            switch (mnum) {
               HANDLE_RF (STATUS_REQ);
               HANDLE_RF (EXPOSE);
               HANDLE_RF (MOVE);
               HANDLE_RF (CONFIGURE_HIT);
               HANDLE_RF (GROUP_CONTROL);
               HANDLE_RF (AUDIO_CONTROL);
               HANDLE_RF (POWER_CONTROL);
               HANDLE_RF (PYRO_FIRE);
               HANDLE_RF (QEXPOSE);
               HANDLE_RF (QCONCEAL);
               HANDLE_RF (REQUEST_NEW);
               HANDLE_RF (ASSIGN_ADDR);
               default:
                  break;
            }
         }
      }
      clearBuffer(fc, end); // clear out last message
   }
   return retval;
}

int handle_STATUS_REQ(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_RF|D_VERY,RED, "handle_STATUS_REQ(%8p, %i, %i)", fc, start, end);
   // wait 'til we have the most up-to-date information to send
   fc->waiting_status_resp = 1;
   return send_2100_status_req(fc); // gather latest information
}

int send_STATUS_RESP(fasit_connection_t *fc) {
   // build up current response
   LB_status_resp_ext_t s;
   DDCMSG(D_RF|D_VERY,RED, "send_STATUS_RESP(%08X)", fc);
   D_memset(&s, 0, sizeof(LB_status_resp_ext_t));
   s.hits = min(0,max(fc->hit_hit, 127)); // cap upper/lower bounds
   s.expose = fc->f2102_resp.body.exp == 90 ? 1: 0; // transitions become "down"
   s.speed = min(0,max(htonf(fc->f2102_resp.body.speed) * 100, 2047)); // cap upper/lower bounds
   s.dir = fc->f2102_resp.body.move & 0x3;
   s.location = htons(fc->f2102_resp.body.pos) & 0x7ff;
   s.hitmode = fc->hit_mode;
   s.react = fc->hit_react;
   s.sensitivity = fc->hit_react;
   s.timehits = fc->hit_burst;
   s.fault = htons(fc->f2102_resp.body.fault);

   // check to see if we've never sent any status before
   if (!fc->sent_status) {
      // copy current status to last status and send it
      fc->last_status = s;
      return send_STATUS_RESP_EXT(fc);
   }

   // determine changes send correct status response
   if (s.fault != fc->last_status.fault ||
       s.timehits != fc->last_status.timehits ||
       s.sensitivity != fc->last_status.sensitivity ||
       s.react != fc->last_status.react ||
       s.tokill != fc->last_status.tokill ||
       s.hitmode != fc->last_status.hitmode) {
      // copy current status to last status and send it
      fc->last_status = s;
      return send_STATUS_RESP_EXT(fc);
   } else if (s.location != fc->last_status.location ||
              s.dir != fc->last_status.dir ||
              s.speed != fc->last_status.speed ||
              s.expose != fc->last_status.expose ||
              s.hits != fc->last_status.hits) {
      // copy current status to last status and send it
      fc->last_status = s;
      // send appropriate mover or lifter message
      if (fc->target_type == RF_Type_MIT || fc->target_type == RF_Type_MAT) {
         return send_STATUS_RESP_MOVER(fc);
      } else {
         return send_STATUS_RESP_LIFTER(fc);
      }
   } else {
      // nothing changed, send that
      return send_STATUS_NO_RESP(fc);
   }
   return doNothing; // shouldn't get here
}

int send_STATUS_RESP_LIFTER(fasit_connection_t *fc) {
   // create message from parts of fc->last_status
   LB_status_resp_mover_t bdy;
   DDCMSG(D_RF|D_VERY,RED, "send_STATUS_RESP_LIFTER(%08X)", fc);
   bdy.cmd = LBC_STATUS_RESP_LIFTER;
   bdy.addr = fc->id & 0x7FF; // source address (always to basestation)
   bdy.hits = fc->last_status.hits;
   bdy.expose = fc->last_status.expose;
   fc->sent_status = 1; // have sent a status message before

   // set crc and send
   set_crc8(&bdy);
   queueMsg(fc, &bdy, RF_size(LBC_STATUS_RESP_LIFTER));
   return mark_rfWrite;
}

int send_STATUS_RESP_MOVER(fasit_connection_t *fc) {
   // create message from parts of fc->last_status
   LB_status_resp_mover_t bdy;
   DDCMSG(D_RF|D_VERY,RED, "send_STATUS_RESP_MOVER(%08X)", fc);
   bdy.cmd = LBC_STATUS_RESP_MOVER;
   bdy.addr = fc->id & 0x7FF; // source address (always to basestation)
   bdy.hits = fc->last_status.hits;
   bdy.expose = fc->last_status.expose;
   bdy.speed = fc->last_status.speed;
   bdy.dir = fc->last_status.dir;
   bdy.location = fc->last_status.location;
   fc->sent_status = 1; // have sent a status message before

   // set crc and send
   set_crc8(&bdy);
   queueMsg(fc, &bdy, RF_size(LBC_STATUS_RESP_MOVER));
   return mark_rfWrite;
}

int send_STATUS_RESP_EXT(fasit_connection_t *fc) {
   DDCMSG(D_RF|D_VERY,RED, "send_STATUS_RESP_EXT(%08X)", fc);
   // finish filling in message and send
   fc->last_status.cmd = LBC_STATUS_RESP_EXT;
   fc->last_status.addr = fc->id & 0x7FF; // source address (always to basestation)
   fc->sent_status = 1; // have sent a status message before
   set_crc8(&fc->last_status);
   queueMsg(fc, &fc->last_status, RF_size(LBC_STATUS_RESP_EXT));
   return mark_rfWrite;
}

int send_STATUS_NO_RESP(fasit_connection_t *fc) {
   // create message and send
   LB_status_no_resp_t bdy;
   DDCMSG(D_RF|D_VERY,RED, "send_STATUS_NO_RESP(%08X)", fc);
   bdy.cmd = LBC_STATUS_NO_RESP;
   bdy.addr = fc->id & 0x7FF; // source address (always to basestation)
   set_crc8(&bdy);
   queueMsg(fc, &bdy, RF_size(LBC_STATUS_NO_RESP));
   return mark_rfWrite;
}

int handle_EXPOSE(fasit_connection_t *fc, int start, int end) {
   LB_expose_t *pkt = (LB_expose_t *)(fc->rf_ibuf + start);
   int retval = doNothing;
   static int mfsSDelay = -1;
   static int mfsRDelay = -1;
   DDCMSG(D_RF|D_VERY,RED, "handle_EXPOSE(%8p, %i, %i)", fc, start, end);

   // read, once, the eeprom values for start and repeat delays
   if (mfsSDelay == -1) {
      mfsSDelay = ReadEeprom_int(MFS_START_DELAY_LOC, MFS_START_DELAY_SIZE, MFS_START_DELAY);
      mfsRDelay = ReadEeprom_int(MFS_REPEAT_DELAY_LOC, MFS_REPEAT_DELAY_SIZE, MFS_REPEAT_DELAY);
   }

   // send mfs configuration
   if (fc->has_MFS) {
      switch (pkt->mfs) {
         case 0:
            // off
            retval |= send_2110(fc, 0, 0, 0, 0);
            break;
         case 1:
            // single
            retval |= send_2110(fc, 1, 0, mfsSDelay, mfsRDelay);
            break;
         case 2:
            // burst
            retval |= send_2110(fc, 1, 1, mfsSDelay, mfsRDelay);
            break;
         case 3:
            // simulate single or burst randomly
            if (rand() % 2) {
               retval |= send_2110(fc, 1, 0, mfsSDelay, mfsRDelay);
            } else {
               retval |= send_2110(fc, 1, 1, mfsSDelay, mfsRDelay);
            }
            break;
      }
   }

   // send configure hit sensing
   retval |= send_2100_conf_hit(fc, 4, /* blank on conceal */
                                0, /* reset hit count */
                                fc->hit_react, /* remembered hit reaction */
                                pkt->tokill, /* hits to kill */
                                fc->hit_sens, /* remembered hit sensitivity */
                                pkt->hitmode ? 2 : 1, /* burst / single */
                                fc->hit_burst); /* remembered hit burst seperation */

   
   // send expose command
   retval |= send_2100_exposure(fc, pkt->expose ? 90 : 0);
   
   return retval;
}

int handle_MOVE(fasit_connection_t *fc, int start, int end) {
   LB_move_t *pkt = (LB_move_t *)(fc->rf_ibuf + start);
   // convert speed
   float speed = pkt->speed / 100.0;
   DDCMSG(D_RF|D_VERY,RED, "handle_MOVE(%8p, %i, %i)", fc, start, end);
   if (pkt->speed == 2047) {
      // send e-stop message
      return send_2100_estop(fc);
   }

   // send movement message
   return send_2100_movement(fc, pkt->direction ? 1: 2, /* forward / reverse */
                             speed); /* converted speed value */ 
}

int handle_CONFIGURE_HIT(fasit_connection_t *fc, int start, int end) {
   LB_configure_t *pkt = (LB_configure_t *)(fc->rf_ibuf + start);
   int retval = doNothing;
   DDCMSG(D_RF|D_VERY,RED, "handle_CONFIGURE_HIT(%8p, %i, %i)", fc, start, end);

   // check for thermal control only
   if (pkt->react == 7) {
      // TOOD -- add thermal control
      return doNothing;
   }

   // hit count set
   switch (pkt->hitcountset) {
      case 0:
         // no change
         break;
      case 1:
         // reset to zero
         fc->hit_hit = 0;
         break;
      case 2:
         // incriment by one
         fc->hit_hit++;
         break;
      case 3:
         // set to hits-to-kill value
         fc->hit_hit = pkt->tokill;
         break;
   }

   // reaction settings
   if (pkt->react == 6) {
      retval |= send_14110(fc, 1); // enable PHI
      fc->hit_phi = 1; // remember
      fc->hit_react = 1; // kill
   } else if (fc->hit_phi) {
      retval |= send_14110(fc, 1); // disable previously enabled PHI
      fc->hit_phi = 0; // forget
      fc->hit_react = pkt->react;
   } else {
      fc->hit_react = pkt->react;
   }

   // remember other hit settings
   fc->hit_tokill = pkt->tokill;
   fc->hit_sens = pkt->sensitivity;
   fc->hit_mode = pkt->hitmode ? 2 : 1; // burst / single
   fc->hit_burst = pkt->timehits * 5; // each number is 5 milliseconds
   
   // send configure hit sensing
   retval |= send_2100_conf_hit(fc, 4, /* blank on conceal */
                                fc->hit_hit, /* remembered hit reset value */
                                fc->hit_react, /* remembered hit reaction */
                                fc->hit_tokill, /* remembered hits to kill */
                                fc->hit_sens, /* remembered hit sensitivity */
                                fc->hit_mode, /* remembered hit mode */
                                fc->hit_burst); /* remembered hit burst seperation */

   return retval;
}

int handle_GROUP_CONTROL(fasit_connection_t *fc, int start, int end) {
   LB_group_control_t *pkt = (LB_group_control_t *)(fc->rf_ibuf + start);
   // which group command?
   int i, j;
   DDCMSG(D_RF|D_VERY,RED, "handle_GROUP_CONTROL(%8p, %i, %i)", fc, start, end);
   switch (pkt->gcmd) {
      case 0: // disable command
         for (i = 0; i<MAX_GROUPS; i++) {
            if (pkt->gaddr == fc->groups[i]) {
               // found that I am part of this group
               for (j = 0; j<MAX_GROUPS; j++) {
                  // clear out any existing ones in the disabled list
                  if (fc->groups_disabled[j] == pkt->gaddr) {
                     fc->groups_disabled[j] = 0;
                  }
               }
               for (j = 0; j<MAX_GROUPS; j++) {
                  if (fc->groups_disabled[j] == 0) {
                     // found place in group disable list that's free
                     fc->groups_disabled[j] = pkt->gaddr;
                     return doNothing;
                  }
               }
               return doNothing;
            } 
         }
         return doNothing; break;
      case 1: // enable command
         for (i = 0; i<MAX_GROUPS; i++) {
            // clear out any existing ones in the disabled list
            if (fc->groups_disabled[i] == pkt->gaddr) {
               fc->groups_disabled[i] = 0;
            }
         }
         return doNothing; break;
      case 2: // join command
         for (i = 0; i<MAX_GROUPS; i++) {
            // clear out any existing ones in the group list
            if (fc->groups[i] == pkt->gaddr) {
               fc->groups[i] = 0;
            }
         }
         for (i = 0; i<MAX_GROUPS; i++) {
            // find empty spot in group list
            if (fc->groups[i] == 0) {
               fc->groups[i] = pkt->gaddr;
               return doNothing;
            }
         }
         return doNothing; break;
      case 3: // seperate command
         for (i = 0; i<MAX_GROUPS; i++) {
            // clear out any existing ones in the group list
            if (fc->groups[i] == pkt->gaddr) {
               fc->groups[i] = 0;
            }
         }
         return doNothing; break;
   }
   return doNothing;
}

int handle_AUDIO_CONTROL(fasit_connection_t *fc, int start, int end) {
   LB_packet_t *pkt = (LB_packet_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_AUDIO_CONTROL(%8p, %i, %i)", fc, start, end);
   // TODO -- fill me in
   return doNothing;
}

int handle_POWER_CONTROL(fasit_connection_t *fc, int start, int end) {
   LB_power_control_t *pkt = (LB_power_control_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_POWER_CONTROL(%8p, %i, %i)", fc, start, end);
   // send correct power command to fasit client
   switch (pkt->pcmd) {
      case 0: // ignore command
         return doNothing; break;
      case 1: // sleep command
         fc->sleeping = 1;
         return send_2100_power(fc, CID_Sleep); break;
      case 2: // wake command
         fc->sleeping = 0;
         return send_2100_power(fc, CID_Wake); break;
      case 3: // extended shutdown command
         return send_2100_power(fc, CID_Shutdown); break;
   }
   return doNothing;
}

int handle_PYRO_FIRE(fasit_connection_t *fc, int start, int end) {
   LB_pyro_fire_t *pkt = (LB_pyro_fire_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_PYRO_FIRE(%8p, %i, %i)", fc, start, end);
   // send pyro fire request (will handle "set" and "fire" commands to BES)
   send_2000(fc, pkt->zone);
   return doNothing;
}

int handle_QEXPOSE(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_RF|D_VERY,RED, "handle_QEXPOSE(%8p, %i, %i)", fc, start, end);
   // send exposure request
   return send_2100_exposure(fc, 90); // 90^ = exposed
}

int handle_QCONCEAL(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_RF|D_VERY,RED, "handle_QCONCEAL(%8p, %i, %i)", fc, start, end);
   // send exposure request
   return send_2100_exposure(fc, 0); // 0^ = concealed
}

int send_DEVICE_REG(fasit_connection_t *fc) {
   // create a message packet with most significant bytes of the FASIT device ID
   LB_device_reg_t bdy;
   DDCMSG(D_RF|D_VERY,RED, "send_DEVICE_REG(%08X)", fc);
   D_memset(&bdy, 0, sizeof(LB_device_reg_t));
   bdy.cmd = LBC_DEVICE_REG;
   bdy.dev_type = fc->target_type;
   bdy.devid = fc->devid;
   DDCMSG(D_RF|D_MEGA, BLACK, "Going to pass devid: %02X:%02X:%02X:%02X", (bdy.devid & 0xff000000) >> 24, (bdy.devid & 0xff0000) >> 16, (bdy.devid & 0xff00) >> 8, bdy.devid & 0xff);

   // put in the crc and send
   set_crc8(&bdy);
   queueMsg(fc, &bdy, RF_size(LBC_DEVICE_REG));
   return mark_rfWrite;
}

// a request for new messages has arrived, send back "DEVICE_REG"
int handle_REQUEST_NEW(fasit_connection_t *fc, int start, int end) {
   LB_request_new_t *pkt = (LB_request_new_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_REQUEST_NEW(%8p, %i, %i)", fc, start, end);

   // if we got here, we must forget our old address
   fc->id=2047;
   // only send a registry if I have a fully connected fasit client
   if (fc->target_type == RF_Type_Unknown) {
      DDCMSG(D_RF, RED, "haven't finished connecting yet, can't register");
      return doNothing;
   }

   // I must already have matched 'packetforme' to get here, so I should send the dev registration

   return send_DEVICE_REG(fc); // register now
}


int handle_ASSIGN_ADDR(fasit_connection_t *fc, int start, int end) {
   LB_assign_addr_t *pkt = (LB_assign_addr_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_ASSIGN_ADDR(%8p, %i, %i)", fc, start, end);
   // change device ID to new address
   fc->id = pkt->new_addr;
   DDCMSG(D_RF, RED, "SLAVEBOSS Registered as %i for %08X", fc->id, fc);
   return doNothing;
}



