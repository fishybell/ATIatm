#include "mcp.h"
#include "slaveboss.h"
#include "rf.h"
#include "eeprom.h"

// we don't worry about clearing the data before a valid message, just up to the end
static void clearBuffer(fasit_connection_t *fc, int end) {
   if (end >= fc->rf_ilen) {
      // clear the entire buffer
      fc->rf_ilen = 0;
   } else {
      // clear out everything up to and including end
      char tbuf[RF_BUF_SIZE];
      memcpy(tbuf, fc->rf_ibuf + (sizeof(char) * end), fc->rf_ilen - end);
      memcpy(fc->rf_ibuf, tbuf, fc->rf_ilen - end);
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
   memcpy(fc->rf_obuf + (sizeof(char) *fc->rf_olen), msg, size);
   fc->rf_olen += size;
}

// see if the given packet is for this fasit connection
static int packetForMe(fasit_connection_t *fc, int start) {
   LB_packet_t *hdr = (LB_packet_t*)(fc->rf_ibuf + start);
   int i, j;
   if (hdr->addr == fc->id) { return 1; } // directly for me

   for (i = 0; i<MAX_GROUPS; i++) {
      if (hdr->addr == fc->groups[i]) {
         for (j = 0; j<MAX_GROUPS; j++) {
            if (hdr->addr == fc->groups_disabled[j]) {
               return 0; // is one of my groups, but it's disabled
            }
         }
         return 1; // is one of my groups
      }
   }

   // not for me
   return 0;
}

// read a single RF message into given buffer, return do next
int rfRead(int fd, char **dest, int *dests) {
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
         return rem_rfEpoll;
      }
   }

   // data found, let the handler parse it
   return doNothing;
}

// write all RF messages for connection in fconns
int rfWrite(fasit_connection_t *fc) {
   int s;

   // have something to write?
   if (fc->rf_olen <= 0) {
      // we only send data, or listen for writability, if we have something to write
      return mark_rfRead;
   }

   // write all the data we can
   s = write(fc->rf, fc->rf_obuf, fc->rf_olen);

   // did it fail?
   if (s <= 0) {
      if (s == 0 || errno == EAGAIN) {
         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
         return rem_rfEpoll;
      }
   } else if (s < fc->rf_olen) {
      // we can't leave only a partial message going out, finish writing even if we block
      int opts;

      // change to blocking from non-blocking
      opts = fcntl(fc->rf, F_GETFL); // grab existing flags
      if (opts < 0) {
         return rem_rfEpoll;
      }
      opts = (opts ^ O_NONBLOCK); // remove nonblock from existing flags
      if (fcntl(fc->rf, F_SETFL, opts) < 0) {
         return rem_rfEpoll;
      }

      // loop until written (since we're blocking, it won't loop forever, just until timeout)
      while (s >= 0) {
         int ns = write(fc->rf, fc->rf_obuf + (sizeof(char) * s), fc->rf_olen - s);
         if (ns < 0 && errno != EAGAIN) {
            // connection dead, remove it
            return rem_rfEpoll;
         }
         s += ns; // increase total written, possibly by zero
      }

      // change to non-blocking from blocking
      opts = (opts | O_NONBLOCK); // add nonblock back into existing flags
      if (fcntl(fc->rf, F_SETFL, opts) < 0) {
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
      if (((fc->rf_ibuf + fc->rf_ilen) - (fc->rf_ibuf + *start)) > hl) { *start = *start + 1; continue; } // invalid message length, or more likely, don't have all of the message yet

      *end = *start + hl;
      switch (hdr->cmd) {
         // they all have a crc, check it
         case LBC_EXPOSE:
         case LBC_MOVE:
         case LBC_CONFIGURE_HIT:
         case LBC_AUDIO_CONTROL:
         case LBC_PYRO_FIRE:
         case LBC_DEVICE_REG:
         case LBC_DEVICE_ADDR:
         case LBC_STATUS_REQ:
         case LBC_STATUS_RESP_LIFTER:
         case LBC_STATUS_RESP_MOVER:
         case LBC_STATUS_RESP_EXT:
         case LBC_STATUS_NO_RESP:
         case LBC_GROUP_CONTROL:
         case LBC_POWER_CONTROL:
         case LBC_QEXPOSE:
         case LBC_QCONCEAL	:
         case LBC_REQUEST_NEW:
            if (crc8(hdr, hl) == 0) {
               mnum = hdr->cmd;
               break;
            }
            break;

         // not a valid number, not a valid header
         default:
            break;
      }

      *start = *start + 1;
   }

   return mnum;
}

// macro used in rf2fasit
#define HANDLE_RF(RF_NUM) case LBC_ ## RF_NUM : return handle_ ## RF_NUM (fc, start, end) ; break;

// mangle an rf message into 1 or more fasit messages, and potentially respond with rf message
int rf2fasit(fasit_connection_t *fc, char *buf, int s) {
   int start, end, mnum;

   // check client
   if (!fc->rf) {
      return doNothing;
   }
  
   // read all available valid messages
   while ((mnum = validMessage(fc, &start, &end)) != -1) {
      if (!packetForMe(fc, start)) {
         DCMSG(RED,"Ignored RF message %d",mnum);
      } else {
         DCMSG(RED,"Recieved RF message %d",mnum);
         if (fc->sleeping && mnum != LBC_POWER_CONTROL) {
            // ignore message when sleeping
            DCMSG(RED,"Slept through RF message %d",mnum);
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
               HANDLE_RF (DEVICE_ADDR);
               default:
                  break;
            }
         }
      }
      clearBuffer(fc, end); // clear out last message
   }
   return doNothing;
}

int handle_STATUS_REQ(fasit_connection_t *fc, int start, int end) {
   // wait 'til we have the most up-to-date information to send
   fc->waiting_status_resp = 1;
   return send_2100_status_req(fc); // gather latest information
}

int send_STATUS_RESP(fasit_connection_t *fc) {
   // build up current response
   LB_status_resp_ext_t s;
   memset(&s, 0, sizeof(LB_status_resp_ext_t));
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
      send_STATUS_NO_RESP(fc);
   }
   return doNothing;
}

int send_STATUS_RESP_LIFTER(fasit_connection_t *fc) {
   // create message from parts of fc->last_status
   LB_status_resp_mover_t bdy;
   bdy.cmd = LBC_STATUS_RESP_LIFTER;
   bdy.addr = fc->id & 0x7FF; // source address (always to basestation)
   bdy.hits = fc->last_status.hits;
   bdy.expose = fc->last_status.expose;

   // set crc and send
   set_crc8(&bdy, sizeof(LB_status_resp_mover_t));
   queueMsg(fc, &bdy, sizeof(LB_status_resp_mover_t));
   return mark_rfWrite;
}

int send_STATUS_RESP_MOVER(fasit_connection_t *fc) {
   // create message from parts of fc->last_status
   LB_status_resp_mover_t bdy;
   bdy.cmd = LBC_STATUS_RESP_MOVER;
   bdy.addr = fc->id & 0x7FF; // source address (always to basestation)
   bdy.hits = fc->last_status.hits;
   bdy.expose = fc->last_status.expose;
   bdy.speed = fc->last_status.speed;
   bdy.dir = fc->last_status.dir;
   bdy.location = fc->last_status.location;

   // set crc and send
   set_crc8(&bdy, sizeof(LB_status_resp_mover_t));
   queueMsg(fc, &bdy, sizeof(LB_status_resp_mover_t));
   return mark_rfWrite;
}

int send_STATUS_RESP_EXT(fasit_connection_t *fc) {
   // finish filling in message and send
   fc->last_status.cmd = LBC_STATUS_RESP_EXT;
   fc->last_status.addr = fc->id & 0x7FF; // source address (always to basestation)
   set_crc8(&fc->last_status, sizeof(LB_status_resp_ext_t));
   queueMsg(fc, &fc->last_status, sizeof(LB_status_resp_ext_t));
   return mark_rfWrite;
}

int send_STATUS_NO_RESP(fasit_connection_t *fc) {
   // create message and send
   LB_status_no_resp_t bdy;
   bdy.cmd = LBC_STATUS_NO_RESP;
   bdy.addr = fc->id & 0x7FF; // source address (always to basestation)
   set_crc8(&bdy, sizeof(LB_status_no_resp_t));
   queueMsg(fc, &bdy, sizeof(LB_status_no_resp_t));
   return mark_rfWrite;
}

int handle_EXPOSE(fasit_connection_t *fc, int start, int end) {
   LB_expose_t *pkt = (LB_expose_t *)(fc->rf_ibuf + start);
   int retval = doNothing;
   static int mfsSDelay = -1;
   static int mfsRDelay = -1;

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
            retval &= send_2110(fc, 0, 0, 0, 0);
            break;
         case 1:
            // single
            retval &= send_2110(fc, 1, 0, mfsSDelay, mfsRDelay);
            break;
         case 2:
            // burst
            retval &= send_2110(fc, 1, 1, mfsSDelay, mfsRDelay);
            break;
         case 3:
            // simulate single or burst randomly
            if (rand() % 2) {
               retval &= send_2110(fc, 1, 0, mfsSDelay, mfsRDelay);
            } else {
               retval &= send_2110(fc, 1, 1, mfsSDelay, mfsRDelay);
            }
            break;
      }
   }

   // send configure hit sensing
   retval &= send_2100_conf_hit(fc, 4, /* blank on conceal */
                                0, /* reset hit count */
                                fc->hit_react, /* remembered hit reaction */
                                pkt->tokill, /* hits to kill */
                                fc->hit_sens, /* remembered hit sensitivity */
                                pkt->hitmode ? 2 : 1, /* burst / single */
                                fc->hit_burst); /* remembered hit burst seperation */

   
   // send expose command
   retval &= send_2100_exposure(fc, pkt->expose ? 90 : 0);
   
   return retval;
}

int handle_MOVE(fasit_connection_t *fc, int start, int end) {
   LB_move_t *pkt = (LB_move_t *)(fc->rf_ibuf + start);
   // convert speed
   float speed = pkt->speed / 100.0;
   if (pkt->speed == 2047) {
      // TODO -- add emergency stop handling here
      speed = 0.0; // just normal stop for right now
   }

   // send movement message
   return send_2100_movement(fc, pkt->direction ? 1: 2, /* forward / reverse */
                             speed); /* converted speed value */ 
}

int handle_CONFIGURE_HIT(fasit_connection_t *fc, int start, int end) {
   LB_configure_t *pkt = (LB_configure_t *)(fc->rf_ibuf + start);
   int retval = doNothing;

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
      retval &= send_14110(fc, 1); // enable PHI
      fc->hit_phi = 1; // remember
      fc->hit_react = 1; // kill
   } else if (fc->hit_phi) {
      retval &= send_14110(fc, 1); // disable previously enabled PHI
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
   retval &= send_2100_conf_hit(fc, 4, /* blank on conceal */
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
   // TODO -- fill me in
   return doNothing;
}

int handle_POWER_CONTROL(fasit_connection_t *fc, int start, int end) {
   LB_power_control_t *pkt = (LB_power_control_t *)(fc->rf_ibuf + start);
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
   // send pyro fire request (will handle "set" and "fire" commands to BES)
   send_2000(fc, pkt->zone);
   return doNothing;
}

int handle_QEXPOSE(fasit_connection_t *fc, int start, int end) {
   // send exposure request
   return send_2100_exposure(fc, 90); // 90^ = exposed
}

int handle_QCONCEAL(fasit_connection_t *fc, int start, int end) {
   // send exposure request
   return send_2100_exposure(fc, 0); // 0^ = concealed
}

int send_DEVICE_REG(fasit_connection_t *fc) {
   // create a message packet with most significant bytes of the FASIT device ID
   LB_device_reg_t bdy;
   memset(&bdy, 0, sizeof(LB_device_reg_t));
   bdy.cmd = LBC_DEVICE_REG;
   bdy.addr = fc->id & 0x7FF; // source address (always to basestation)
   bdy.dev_type = fc->target_type;
   if (fc->target_type == RF_Type_BES) {
      bdy.devid = fc->f2111_resp.body.devid & 0xffffff;
   } else {
      bdy.devid = fc->f2005_resp.body.devid & 0xffffff;
   }

   // put in the crc and send
   set_crc8(&bdy, sizeof(LB_device_reg_t));
   queueMsg(fc, &bdy, sizeof(LB_device_reg_t));
   return mark_rfWrite;
}

// a request for new messages has arrived, send back "DEVICE_REG"
int handle_REQUEST_NEW(fasit_connection_t *fc, int start, int end) {
   LB_request_new_t *pkt = (LB_request_new_t *)(fc->rf_ibuf + start);
   // only send a registry if I have a fully connected fasit client
   if (fc->target_type == RF_Type_Unknown) {
      return doNothing;
   }
   // register -- TODO -- what about if I'm already registered?
   return send_DEVICE_REG(fc);
}

int handle_DEVICE_ADDR(fasit_connection_t *fc, int start, int end) {
   LB_packet_t *pkt = (LB_packet_t *)(fc->rf_ibuf + start);
   return doNothing;
}



