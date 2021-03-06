#include <sys/stat.h>
#include "mcp.h"
#include "rf_debug.h"
#include "RFslave.h"

size_t strnlen(const char *s, size_t maxlen);	// make the compiler happier

extern struct timespec start_time; // global timers
extern minion_time_t mt;

// cancel the outbound rf queue and everything that uses it
void clearTxQ(rf_connection_t *rc) {
   int i;
   // reset tty output buffer
   //DCMSG(RED, "tty:%i id:%i idl:%i did:%i dlo:%3x dhi:%3x", rc->tty_olen,
   //      rc->id_index, rc->id_lasttime_index,
   //      rc->devid_index, rc->devid_last_low, rc->devid_last_high);
   rc->tty_olen = 0;
   // reset timeslot stuff
   rc->id_index = -1;
   for (i = 0; i < MAX_IDS; i++) {rc->ids[i] = -1;}
   rc->id_lasttime_index = -1;
   for (i = 0; i < MAX_IDS; i++) {rc->ids_lasttime[i] = -1;}
   rc->devid_index = -1;
   rc->devid_last_low = -1;
   rc->devid_last_high = -1;
   for (i = 0; i < MAX_IDS; i++) {rc->devids[i] = -1;}
}

// the start and end values may be set even if no valid message is found
static int validMessage(rf_connection_t *rc, int *start, int *end, int tty) {
   LB_packet_t *hdr;
   int hl = -1;
   int mnum = -1;
   // loop through entire buffer, parsing starting at each character
   while (*start >= 0 && ((tty == 0 && *start < rc->sock_ilen) ||
                          (tty == 1 && *start < rc->tty_ilen) ||
                          (tty == 2 && *start < rc->tty_olen)) && mnum == -1) {
      // map the memory, we don't need to manipulate it
      if (tty) {
         if (tty == 1) {
            hdr = (LB_packet_t*)(rc->tty_ibuf + *start);
         } else {
            hdr = (LB_packet_t*)(rc->tty_obuf + *start);
         }
      } else {
         hdr = (LB_packet_t*)(rc->sock_ibuf + *start);
      }
      
      // check for valid message first
      hl = RF_size(hdr->cmd);
      if (tty == 1 && ((rc->tty_ilen - (*start)) < hl)) {
         DDCMSG(D_RF, cyan, "Invalid tty message length %i - %i < %i", rc->tty_ilen, *start, hl);
         *start = *start + 1; continue; // invalid message length, or more likely, don't have all of the message yet
      } else if (tty == 2 && ((rc->tty_olen - (*start)) < hl)) {
         DDCMSG(D_RF, cyan, "Invalid tty message length %i - %i < %i", rc->tty_olen, *start, hl);
         *start = *start + 1; continue; // invalid message length, or more likely, don't have all of the message yet
      } else if (!tty && ((rc->sock_ilen - (*start)) < hl)) {
         DDCMSG(D_RF, blue, "Invalid socket message length %i - %i < %i", rc->sock_ilen, *start, hl);
         *start = *start + 1; continue; // invalid message length, or more likely, don't have all of the message yet
      }
      if (hdr->cmd == LBC_QUICK_GROUP) {
         //DCMSG(MAGENTA, "Found quick group message");
         DDpacket((char*)hdr, RF_size(hdr->cmd));
      }

      *end = *start + hl; // one character after the end of the message
      DDCMSG(D_RF, tty ? cyan : blue, "Checking %s validMessage for %i (s:%i e:%i)", tty ? "TTY" : "SOCKET", hdr->cmd, *start, *end);
      switch (hdr->cmd) {
         // they all have a crc, check it
         case LBC_REPORT_ACK:
         case LBC_EXPOSE:
         case LBC_MOVE:
         case LBC_CONFIGURE_HIT:
         case LBC_AUDIO_CONTROL:
         case LBC_PYRO_FIRE:
         case LBC_ACCESSORY:
         case LBC_HIT_BLANKING:
         case LBC_DEVICE_REG:
         case LBC_ASSIGN_ADDR:
         case LBC_STATUS_REQ:
         case LBC_STATUS_RESP:
         case LBC_GROUP_CONTROL:
         case LBC_POWER_CONTROL:
         case LBC_BURST:
         case LBC_RESET:
         case LBC_QEXPOSE:
         case LBC_QUICK_GROUP:
         case LBC_QUICK_GROUP_BIG:
         case LBC_QCONCEAL:
         case LBC_EVENT_REPORT:
         case LBC_REQUEST_NEW:
         case LBC_DATA_START:
         case LBC_DATA_CHUNK:
         case LBC_DATA_NACK:
            if (crc8(hdr) == 0) {
               DDCMSG(D_RF, tty ? cyan : blue, "%s VALID CRC", tty ? "TTY" : "SOCKET");
               return hdr->cmd;
            } else {
               DCMSG(GREEN, "INVALID CRC for msg %i", hdr->cmd);
               DDCMSG(D_RF, tty ? cyan : blue, "%s INVALID CRC", tty ? "TTY" : "SOCKET");
            }
            break;

         // not a valid number, not a valid header
         default:
            break;
      }

      *start = *start + 1;
   }

//   if (tty) {
//      DDCMSG(D_RF, cyan, "TTY invalid: %08X %i %i", rc->tty_ibuf, *start, rc->tty_ilen);
//      DDCMSG(D_RF, HEXB(cyan, "TTY invalid:", rc->tty_ibuf+*start, rc->tty_ilen-*start);
//   } else {
//      DDCMSG(D_RF, cyan, "SOCKET invalid: %08X %i %i", rc->sock_ibuf, *start, rc->sock_ilen);
//      DDCMSG(D_RF, HEXB(cyan, "SOCKET invalid:", rc->sock_ibuf+*start, rc->sock_ilen-*start);
//   }
   return -1;
}

// returns the timeout value to pass to epoll
int getTimeout(rf_connection_t *rc) {
   long tv;

   // do infinite timeout if the values are the same
   if (rc->timeout_start == rc->time_start) {
      return INFINITE;
   }

   // get current time
   tv = getTime();

   DDCMSG(D_TIME, RED, "Now: %9ld, Then:  %9ld=>   SomeOther:  %9ld",
     tv, rc->timeout_start, rc->timeout_end);

   // check to see if we're past our allowed timeslot
   if (tv > rc->timeout_end) {
      DDCMSG(D_TIME, YELLOW, "Waited too long, infinite timeout now");
      timestamp(&mt);
      DDCMSG(D_POINTER, MAGENTA, "Waited too long @ %3i.%03i (d:%i.%03i)", DEBUG_TS(mt.elapsed_time), DEBUG_TS(mt.delta_time));
      // flush output tty buffer because we missed our timeslot
      //DCMSG(RED, "clearTxQ @ %s:%i", __FILE__, __LINE__);
      clearTxQ(rc);
      return INFINITE;
   }

   // subtract milliseconds straight out to get remaining time
   tv = rc->timeout_start - tv;

   DDCMSG(D_TIME, RED, "Later: %9ld", tv);

   // timeout early so we wait just the right amount of time later with waitRest()
   if (tv <= 10) {
      DDCMSG(D_TIME, GRAY, "Returning 0 from getTimeout()...waiting");
      waitRest(rc);
      return 0; // timeout is 0, which for epoll is return immediately
   } else {
      DDCMSG(D_TIME, GRAY, "Returning %ld from getTimeout()",max(1, tv - 5));
      return max(1, tv - 5); // timeout is positive, it will wait this amount of time
   }
}

// set the timeout for X milliseconds after the start time
void doTimeAfter(rf_connection_t *rc, int msecs) {
   DDCMSG(D_TIME, RED, "Add to timeout %9ld := %9ld + %i", rc->timeout_start, rc->time_start, msecs);
   // set infinite wait by making the start and end time the same
   if (msecs == INFINITE) {
      DDCMSG(D_TIME, CYAN, "Reset timeout_start to %ld @ %i", rc->time_start, __LINE__);
      rc->timeout_start = rc->time_start;
      return;
   }

   // calculate the lower end time
   rc->timeout_start = rc->time_start + msecs;

   // calculate the upper end time
   msecs += ((2*rc->timeslot_length)/3); // end time is start time plus 2/3 the timeslot length (to allow for transmission time)
   rc->timeout_end += msecs;

   DDCMSG(D_TIME, RED, "Timeout changed to %9ld => %9ld", rc->timeout_start, rc->timeout_end);
}

// wait until the timeout time arrives (the epoll timeout will get us close)
void waitRest(rf_connection_t *rc) {
   long tv;

   // wait until we arrive at the valid time (please only do this for periods under 5 milliseconds
   do {
      // get current time
      tv = getTime();
      //DDCMSG(D_TIME, RED, "Looking if %9ld > %9ld", tv, rc->timeout_start);
   } while ((tv + 2) < rc->timeout_start); // add on a couple of milliseconds for radio readiness
   
   // reset timer now
   DDCMSG(D_TIME, CYAN, "Reset all to %ld @ %i", tv, __LINE__);
   rc->timeout_start = tv;
   rc->timeout_end = tv;
   rc->time_start = tv;
}

// if tty is 1, read data from tty into buffer, otherwise read data from socket into buffer
int rcRead(rf_connection_t *rc, int tty) {
   char dest[RF_BUF_SIZE];
   int dests, err;
   DDCMSG(D_PACKET, tty ? CYAN : BLUE, "%s READING", tty ? "TTY" : "SOCKET");

   // read as much as possible
   dests = read(tty ? rc->child : rc->sock, dest, RF_BUF_SIZE); // the child process does the actual tty reading
   err = errno; // save errno

   timestamp(&mt);
   DDCMSG(D_NEW, tty ? CYAN : BLUE, "%s READ %i BYTES @ time %5ld.%09ld", tty ? "TTY" : "SOCKET", dests, mt.elapsed_time.tv_sec, mt.elapsed_time.tv_nsec);
   if (verbose&D_POINTER && tty) {
      DDCMSG(D_POINTER, CYAN, "TTY RX %i BYTES @ time %3i.%03i (d:%3i.%03i)", dests, DEBUG_TS(mt.elapsed_time), DEBUG_TS(mt.delta_time));
      DDpacket(dest, dests);
   }

   // did we read nothing?
   if (dests <= 0) {
      DDCMSG(D_PACKET, tty ? CYAN : BLUE, "ERROR: %i", err);
      PERROR("Error: ");
//      return rem_ttyEpoll;
      if (err == EAGAIN) {
         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
         PERROR("Died because ");
         DDCMSG(D_PACKET, tty ? CYAN : BLUE, "%s Dead at %i", tty ? "TTY" : "SOCKET", __LINE__);
         return tty ? rem_ttyEpoll : rem_sockEpoll;
      }
   }

   // data found, add it to the buffer
   if (tty) {
      addToBuffer_tty_in(rc, dest, dests);
   } else {
      addToBuffer_sock_in(rc, dest, dests);
   }

   // let the handler handle it
   return doNothing;
}

#define MIN_WRITE 15
void padTTY(rf_connection_t *rc, int tty, int s) {
/*   if (tty && s < MIN_WRITE) {
      char emptbuf[MIN_WRITE];
      memset(emptbuf, 0, MIN_WRITE);
      DDCMSG(D_NEW, cyan, "TTY PADDING %i BYTES", MIN_WRITE - s);
      write(rc->tty, emptbuf, MIN_WRITE - s);
   }*/
}

// if tty is 1, write data from buffer to tty, otherwise write data from buffer into socket
int rcWrite(rf_connection_t *rc, int tty) {
   int s, err;
   DDCMSG(D_RF, tty ? cyan : blue, "%s WRITE", tty ? "TTY" : "SOCKET");

   // reset times
   if (tty) {
      DDCMSG(D_TIME, CYAN, "Reset time_start & timeout_start to %ld @ %i", rc->timeout_end, __LINE__);
      rc->time_start = rc->timeout_end; // reset to latest time
      rc->timeout_start = rc->timeout_end; // reset to latest time
   }

   // have something to write?
   if ((tty ? rc->tty_olen : rc->sock_olen) <= 0) {
      // we only send data, or listen for writability, if we have something to write
      return tty ? mark_ttyRead : mark_sockRead;
   }

   // if we're tty, block
   if (tty) {
      //setblocking(rc->tty);
   }

   // write all the data we can
   if (tty) {
      s = write(rc->tty, rc->tty_obuf, rc->tty_olen);
   } else {
      s = write(rc->sock, rc->sock_obuf, rc->sock_olen);
   }
   err = errno; // save errno
   timestamp(&mt);
   DDCMSG(D_NEW, tty ? cyan : blue, "%s WROTE %i BYTES @ time %5ld.%09ld", tty ? "TTY" : "SOCKET", s, mt.elapsed_time.tv_sec, mt.elapsed_time.tv_nsec);
   debugRF(tty ? cyan : blue, tty ? rc->tty_obuf : rc->sock_obuf, tty ? rc->tty_olen : rc->sock_olen);
   if (verbose&D_POINTER && tty) {
      DDCMSG(D_POINTER, GRAY, "TTY TX %i BYTES @ time %3i.%03i (d:%3i.%03i)", s, DEBUG_TS(mt.elapsed_time), DEBUG_TS(mt.delta_time));
      DDpacket(rc->tty_obuf, s);
   }

   // did it fail?
   if (s <= 0) {
      if (s == 0 || err == EAGAIN) {
         // if we're tty, block no more
         if (tty) {
            //setnonblocking(rc->tty, 0);
         }

         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
         PERROR("Died because ");
         DDCMSG(D_RF, tty ? cyan : blue, "%s Dead at %i", tty ? "TTY" : "SOCKET", __LINE__);
         // if we're tty, block no more
         if (tty) {
            //setnonblocking(rc->tty, 0);
         }

         return tty ? rem_ttyEpoll : rem_sockEpoll;
      }
   } else if (s < (tty ? rc->tty_olen : rc->sock_olen)) {
      // loop until written (since we're blocking, it won't loop forever, just until timeout)
      while (s >= 0) {
         int ns;
         if (tty) {
            ns = write(rc->tty, rc->tty_obuf + (sizeof(char) * s), rc->tty_olen - s);
         } else {
            ns = write(rc->sock, rc->sock_obuf + (sizeof(char) * s), rc->sock_olen - s);
         }
         err = errno; // save errno
         if (ns < 0 && err != EAGAIN) {
            // connection dead, remove it
            PERROR("Died because ");
            DDCMSG(D_RF, tty ? cyan : blue, "%s Dead at %i", tty ? "TTY" : "SOCKET", __LINE__);
            // if we're tty, block no more
            if (tty) {
               //setnonblocking(rc->tty, 0);
      //DCMSG(RED, "clearTxQ @ %s:%i", __FILE__, __LINE__);
               clearTxQ(rc);
            }

            return tty ? rem_ttyEpoll : rem_sockEpoll;
         }
         s += ns; // increase total written, possibly by zero
      }

      // make sure to pad the tty to at least MIN_WRITE bytes
      padTTY(rc, tty, s);

      // if we're tty, block no more
      if (tty) {
         //setnonblocking(rc->tty, 0);
      //DCMSG(RED, "clearTxQ @ %s:%i", __FILE__, __LINE__);
         clearTxQ(rc); // if we didn't write the whole thing, oh well, we had our chance
      }

      // don't try writing again
      return tty ? mark_ttyRead : mark_sockRead;
   } else {
      // everything was written, clear write buffer
      if (tty) {
      //DCMSG(RED, "clearTxQ @ %s:%i", __FILE__, __LINE__);
         clearTxQ(rc);
      } else {
         rc->sock_olen = 0;
      }

      // make sure to pad the tty to at least MIN_WRITE bytes
      padTTY(rc, tty, s);

      // if we're tty, block no more
      if (tty) {
         //setnonblocking(rc->tty, 0);
      }

      // don't try writing again
      return tty ? mark_ttyRead : mark_sockRead;
   }
   // can't get here, all if/elses have returns...

   // make sure to pad the tty to at least MIN_WRITE bytes
   padTTY(rc, tty, s);

   // if we're tty, block no more
   if (tty) {
      //setnonblocking(rc->tty, 0);
      //DCMSG(RED, "clearTxQ @ %s:%i", __FILE__, __LINE__);
      clearTxQ(rc);
   }

   // partial success, leave writeable so we try again
   return doNothing;
}

// clear "in" buffer for tty
static void clearBuffer_tty(rf_connection_t *rc, int end) {
   if (end >= rc->tty_ilen) {
DDCMSG(D_MEGA, RED, "clearing entire buffer");
      // clear the entire buffer
      rc->tty_ilen = 0;
   } else {
      // clear out everything up to end
      char tbuf[RF_BUF_SIZE];
DDCMSG(D_MEGA, RED, "clearing buffer partially: %i-%i", rc->tty_ilen, end);
      D_memcpy(tbuf, rc->tty_ibuf + (sizeof(char) * end), rc->tty_ilen - end);
      D_memcpy(rc->tty_ibuf, tbuf, rc->tty_ilen - end);
      rc->tty_ilen -= end;
   }
DDCMSG(D_MEGA, RED, "buffer after: %i", rc->tty_ilen);
}

// clear "in" buffer for socket
static void clearBuffer_sock(rf_connection_t *rc, int end) {
   if (end >= rc->sock_ilen) {
      // clear the entire buffer
      rc->sock_ilen = 0;
   } else {
      // clear out everything up to end
      char tbuf[RF_BUF_SIZE];
      D_memcpy(tbuf, rc->sock_ibuf + (sizeof(char) * end), rc->sock_ilen - end);
      D_memcpy(rc->sock_ibuf, tbuf, rc->sock_ilen - end);
      rc->sock_ilen -= end;
   }
}

// we don't worry about clearing the data before a valid message, just up to the end
void clearBuffer(rf_connection_t *rc, int end, int tty) {
   if (tty) {
      clearBuffer_tty(rc, end);
   } else {
      clearBuffer_sock(rc, end);
   }
}

void addToBuffer_generic(char *dbuf, int *dbuf_len, char *ibuf, int ibuf_len) {
   // replace buffer?
   if (*dbuf_len <= 0) {
      D_memcpy(dbuf, ibuf, ibuf_len);
      *dbuf_len = ibuf_len;
   } else {
      // add to buffer
      D_memcpy(dbuf + (sizeof(char) * (*dbuf_len)), ibuf, ibuf_len);
      *dbuf_len += ibuf_len;
   }
}

// check to see if the given packet is a response packet or not
// these packets should only have one in the queue at a time for a given source address
int isPacketResponse(LB_packet_t *LB) {
   if (LB->cmd == LBC_STATUS_RESP) {
      return 1;
   } else {
      return 0;
   }
}

void addToBuffer_tty_out(rf_connection_t *rc, char *buf, int s) {
   // look to see if we should replace an item rather than queue it
   LB_packet_t *LB = (LB_packet_t*)buf, *LBt;
   if (isPacketResponse(LB)) {
      int start, end, mnum, s2;
      start=0;
      while ((mnum = validMessage(rc, &start, &end, 2)) != -1) { // 2 for tty outbound
         LBt = (LB_packet_t*)(buf + start);
         if (LBt->addr == LB->addr && isPacketResponse(LBt)) {
            // matching address and also a response: replace
            char tbuf[RF_BUF_SIZE];
            s2 = rc->tty_olen-end; // remember length of tail
            DDCMSG(D_QUEUE, GRAY, "Replaced %i with %i from %i in queue (%i, %i, %i)",
                   LBt->cmd, LB->cmd, LB->addr, start, end, s2);
            D_memcpy(tbuf, buf+end, s2); // copy tail end of buffer to temp
            D_memcpy(buf+start, buf, s); // copy new message to correct spot
            rc->tty_olen = start + s; // change length of outbound buffer
            addToBuffer_generic(rc->tty_obuf, &rc->tty_olen, tbuf, s2);  // copy tail back to end
            return; // should only ever be one unsent response
         }
         DDCMSG(D_PARSE, BLACK, "Calling validMessage again @ %i", __LINE__);
         start += RF_size(LB->cmd);
      }
      // did not find existing resposne, push it along as normal
      addToBuffer_generic(rc->tty_obuf, &rc->tty_olen, buf, s);
   } else {
      // normal message, push it along
      addToBuffer_generic(rc->tty_obuf, &rc->tty_olen, buf, s);
   }
}

void addToBuffer_tty_in(rf_connection_t *rc, char *buf, int s) {
   addToBuffer_generic(rc->tty_ibuf, &rc->tty_ilen, buf, s);
}

void addToBuffer_sock_out(rf_connection_t *rc, char *buf, int s) {
   addToBuffer_generic(rc->sock_obuf, &rc->sock_olen, buf, s);
}

void addToBuffer_sock_in(rf_connection_t *rc, char *buf, int s) {
   addToBuffer_generic(rc->sock_ibuf, &rc->sock_ilen, buf, s);
}

// find address from tty to socket message for finding timeslot on way back to tty
void addAddrToLastIDs(rf_connection_t *rc, int addr) {
   int i;
   // check to see if we already of this one in the list
   if (addr != FAKE_ID) { // we allow duplicate fake ids, but nothing else
      for (i = 0; i < min(MAX_IDS, rc->id_lasttime_index+1); i++) {
         if (rc->ids_lasttime[i] == addr) {
            return;
         }
      }
   } else {
      DDCMSG(D_POINTER, BLUE, "adding fake @ index %i", rc->id_lasttime_index + 1);
   }

   // add address to id list
   if (++rc->id_lasttime_index < MAX_IDS) {
      rc->ids_lasttime[rc->id_lasttime_index] = addr;
   }
}

int t2s_handle_STATUS_REQ(rf_connection_t *rc, int start, int end) {
   int i;
   LB_status_req_t *pkt = (LB_status_req_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_STATUS_REQ(%8p, %i, %i)", rc, start, end);
   if (rc->quick_num == 0) {
      addAddrToLastIDs(rc, pkt->addr);
   } else {
      for (i = 0; i < rc->quick_num; i++) {
         //DCMSG(CYAN, "Using quick address %i", rc->quick_addrs[i]);
         addAddrToLastIDs(rc, rc->quick_addrs[i]); // add to list in order
      }
   }
   return doNothing;
} 

int t2s_handle_EXPOSE(rf_connection_t *rc, int start, int end) {
   int i;
   LB_expose_t *pkt = (LB_expose_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_EXPOSE(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_MOVE(rf_connection_t *rc, int start, int end) {
   int i;
   LB_move_t *pkt = (LB_move_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_MOVE(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_REPORT_ACK(rf_connection_t *rc, int start, int end) {
   int i;
   LB_report_ack_t *pkt = (LB_report_ack_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_REPORT_ACK(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_CONFIGURE_HIT(rf_connection_t *rc, int start, int end) {
   int i;
   LB_configure_t *pkt = (LB_configure_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_CONFIGURE_HIT(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_GROUP_CONTROL(rf_connection_t *rc, int start, int end) {
   int i;
   LB_group_control_t *pkt = (LB_group_control_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_GROUP_CONTROL(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_AUDIO_CONTROL(rf_connection_t *rc, int start, int end) {
   int i;
   LB_audio_control_t *pkt = (LB_audio_control_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_AUDIO_CONTROL(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_POWER_CONTROL(rf_connection_t *rc, int start, int end) {
   int i;
   LB_power_control_t *pkt = (LB_power_control_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_POWER_CONTROL(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_PYRO_FIRE(rf_connection_t *rc, int start, int end) {
   int i;
   LB_pyro_fire_t *pkt = (LB_pyro_fire_t *)(rc->tty_ibuf + start);
   DDCMSG(D_NEW, CYAN, "t2s_handle_PYRO_FIRE(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_ACCESSORY(rf_connection_t *rc, int start, int end) {
   int i;
   LB_accessory_t *pkt = (LB_accessory_t *)(rc->tty_ibuf + start);
   DDCMSG(D_NEW, CYAN, "t2s_handle_ACCESSORY(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_HIT_BLANKING(rf_connection_t *rc, int start, int end) {
   int i;
   LB_hit_blanking_t *pkt = (LB_hit_blanking_t *)(rc->tty_ibuf + start);
   DDCMSG(D_NEW, CYAN, "t2s_handle_HIT_BLANKING(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_QEXPOSE(rf_connection_t *rc, int start, int end) {
   int i;
   LB_packet_t *pkt = (LB_packet_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_QEXPOSE(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_QUICK_GROUP(rf_connection_t *rc, int start, int end) {
   int i;
   LB_quick_group_t *pkt = (LB_quick_group_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_QUICK_GROUP(%8p, %i, %i)", rc, start, end);
   rc->quick_num = getItemsQR(pkt, rc->quick_addrs);
   //DCMSG(CYAN, "t2s_handle_QUICK_GROUP found %i addresses", rc->quick_num);
   return doNothing;
} 

int t2s_handle_QUICK_GROUP_BIG(rf_connection_t *rc, int start, int end) {
   int i;
   LB_quick_group_big_t *pkt = (LB_quick_group_big_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_QUICK_GROUP_BIG(%8p, %i, %i)", rc, start, end);
   rc->quick_num = getItemsQR(pkt, rc->quick_addrs);
   //DCMSG(CYAN, "t2s_handle_QUICK_GROUP_BIG found %i addresses", rc->quick_num);
   return doNothing;
} 

int t2s_handle_RESET(rf_connection_t *rc, int start, int end) {
   int i;
   LB_packet_t *pkt = (LB_packet_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_RESET(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_QCONCEAL(rf_connection_t *rc, int start, int end) {
   int i;
   LB_packet_t *pkt = (LB_packet_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_QCONCEAL(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, FAKE_ID); // keep track of non-replying commands as fake ids
   return doNothing;
} 

int t2s_handle_REQUEST_NEW(rf_connection_t *rc, int start, int end) {
   int i;
   LB_request_new_t *pkt = (LB_request_new_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_REQUEST_NEW(%8p, %i, %i)", rc, start, end);

   // remember last low/high devid
   DDCMSG(D_T_SLOT|D_VERY, BLACK, "Setting low/high to %X/%X", pkt->low_dev, pkt->low_dev+7);
   rc->devid_last_low = pkt->low_dev;
   rc->devid_last_high = pkt->low_dev+7;

   // remember timeslot length
   rc->timeslot_init = pkt->inittime * 5; // convert to milliseconds
   rc->timeslot_length = pkt->slottime * 5; // convert to milliseconds

   // keep track of this slot in the overall slot list
   addAddrToLastIDs(rc, FAKE_DEVID);

   DDCMSG(D_T_SLOT|D_VERY, BLACK, "Setting timeslot stuff to %i %i", rc->timeslot_init, rc->timeslot_length);

   return doNothing;
} 

int t2s_handle_ASSIGN_ADDR(rf_connection_t *rc, int start, int end) {
   int i;
   LB_assign_addr_t *pkt = (LB_assign_addr_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_ASSIGN_ADDR(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, pkt->new_addr);
   return doNothing;
} 


// macro used in tty2sock
#define t2s_HANDLE_RF(RF_NUM) case LBC_ ## RF_NUM : retval = t2s_handle_ ## RF_NUM (rc, start, end) ; break;

// transfer tty data to the socket
int tty2sock(rf_connection_t *rc) {
   int start, end, mnum, retval = doNothing; // start doing nothing
   int added_to_buf = 0;

   // read all available valid messages up until I have to do something on return
   debugRF(cyan, rc->tty_ibuf, rc->tty_ilen);
   start = 0;
   while ((retval == doNothing || retval == mark_sockWrite) && (mnum = validMessage(rc, &start, &end, 1)) != -1) { // 1 for tty
      int use_command = 1;
      switch (mnum) {
         t2s_HANDLE_RF (STATUS_REQ); // keep track of ids? yes
         t2s_HANDLE_RF (EXPOSE); // keep track of ids? no
         t2s_HANDLE_RF (REPORT_ACK); // keep track of ids? no
         t2s_HANDLE_RF (MOVE); // keep track of ids? no
         t2s_HANDLE_RF (CONFIGURE_HIT); // keep track of ids? no
         t2s_HANDLE_RF (GROUP_CONTROL); // keep track of ids? no
         t2s_HANDLE_RF (AUDIO_CONTROL); // keep track of ids? no
         t2s_HANDLE_RF (POWER_CONTROL); // keep track of ids? no
         t2s_HANDLE_RF (PYRO_FIRE); // keep track of ids? no
         t2s_HANDLE_RF (ACCESSORY); // keep track of ids? no
         t2s_HANDLE_RF (HIT_BLANKING); // keep track of ids? no
         t2s_HANDLE_RF (QEXPOSE); // keep track of ids? no
         t2s_HANDLE_RF (QUICK_GROUP); // keep track of ids? yes
         t2s_HANDLE_RF (QUICK_GROUP_BIG); // keep track of ids? yes
         t2s_HANDLE_RF (QCONCEAL); // keep track of ids? no
         t2s_HANDLE_RF (REQUEST_NEW); // keep track of devids
         t2s_HANDLE_RF (ASSIGN_ADDR); // keep track of ids? no
         t2s_HANDLE_RF (RESET); // keep track of ids? no
         case LBC_DATA_NACK: {
            // if we heard someone else nack the same chunk I'm missing, there's no need for me to nack
            LB_data_nack_t *dn = (LB_data_nack_t*)(rc->tty_ibuf + start);
            if (dn->id == rc->file_id && rc->file_chunks_seen != NULL) {
               if (rc->file_chunks_seen[dn->chunk] == 2) { // 
               }
            }
         } break;
         case LBC_DATA_START: {
            struct stat sbuf;
            LB_data_start_t *ds = (LB_data_start_t*)(rc->tty_ibuf + start);
            // check if we've already seen this
            if (ds->id == rc->file_id) {
               // same id = same file
               break;
            }
            // check against last.transferred file to see if we've already received this file
            if (stat(LAST_DATA, &sbuf) == 0) {
               // file exists, check contents
               char lt_data[256], lt_name[256]; // should both be much less, but better safer than sorry
               int lt_fd = open(LAST_DATA, O_RDONLY);
               if (lt_fd != -1) {
                  unsigned int t_md5[4];
                  read(lt_fd, lt_data, 256);
                  close(lt_fd);

                  sscanf(lt_data, "%x %x %x %x %s", &t_md5[0], &t_md5[1], &t_md5[2], &t_md5[3], lt_name);
                  if (strncmp(lt_name, ds->name, strnlen(lt_name, 41)) == 0 &&
                      memcmp(ds->md5sum, t_md5, 16) == 0) {
                     rc->file_ignore = 1;
                     break;
                  }
               }
            }
            // setup file_* variables
            rc->file_id = ds->id;
            rc->file_last_id = ds->id;
            memset(rc->file_name, 0, 41);
            memcpy(rc->file_name, ds->name, 40);
            memset(rc->file_md5sum, 0, 16);
            memcpy(rc->file_md5sum, ds->md5sum, 16);
            rc->file_execute = ds->execute;
            rc->file_reboot = ds->reboot;
            rc->file_size = ds->size;
            if (rc->file_chunks_seen != NULL) {
               rc->file_chunks_seen = realloc(rc->file_chunks_seen, sizeof(int) * rc->file_size);
            } else {
               rc->file_chunks_seen = malloc(sizeof(int) * rc->file_size);
            }
            memset(rc->file_chunks_seen, 0, sizeof(int) * rc->file_size);
         } break;
         case LBC_DATA_CHUNK: {
            // TODO -- process chunk of data into file if I haven't already correctly received this chunk
         } break;
         case LBC_BURST: { // keep track of packets
            LB_burst_t *pkt = (LB_burst_t *)(rc->tty_ibuf + start);
            rc->packets = pkt->number;

            // change the start time to now time (which happened at the start of the burst)
            DDCMSG(D_TIME, YELLOW, "---------------------------CHANGE IN NUM PACKETS: %i from BURST command", rc->packets);
            DDCMSG(D_TIME, CYAN, "Reset all to %ld @ %i", rc->nowt, __LINE__);
            rc->time_start = rc->nowt;
            rc->timeout_start = rc->nowt; // reset start time as well
            rc->timeout_end = rc->nowt; // reset end time as well
            DDCMSG(D_TIME, BLACK, "Clock changed to %9ld", rc->time_start);

            // check to see if we should be ignoring this message
            if (pkt->sequence > 0 && /* sequence of 0 is a non-ignorable packet */
                rc->last_sequence == pkt->sequence &&
                rc->last_number == pkt->number) {
               // this is the same burst as before, just ignore everything until the next burst
               rc->ignoring = 1;
               DDCMSG(D_POINTER, RED, "Found repeat burst");
            } else {
               // new burst, don't ignore, but remember pieces
               rc->ignoring = 0;
               rc->last_sequence = pkt->sequence;
               rc->last_number = pkt->number;
            }

            //DCMSG(RED, "clearTxQ @ %s:%i", __FILE__, __LINE__);
            clearTxQ(rc);
            use_command = 0; // don't send this command on to slaveboss
         }  break;
         default:
            use_command = 0; // ignore requests from other clients
            break;
      }

      // was it a valid command that we handled?
      if (use_command) {
         rc->packets--; // received a new packet, the burst just got smaller
         DDCMSG(D_TIME, YELLOW, "---------------------------CHANGE IN NUM PACKETS: %i with cmd %i", rc->packets, mnum);
         if (!rc->ignoring) {
            // copy the found message to the socket "out" buffer
            addToBuffer_sock_out(rc, rc->tty_ibuf + start, end - start);
            added_to_buf = 1;
         } else {
            DDCMSG(D_POINTER, RED, "Ignored repeat packet");
         }
         if (rc->quick_num > 0 && mnum != LBC_QUICK_GROUP && mnum != LBC_QUICK_GROUP_BIG) {
            // used quick group addresses
            //DCMSG(CYAN, "Cleared quick_num from being %i", rc->quick_num);
            rc->quick_num = 0;
            DDpacket(rc->tty_ibuf + start, end - start);
         } else if (mnum == LBC_QUICK_GROUP || mnum == LBC_QUICK_GROUP_BIG) {
            //DCMSG(GRAY, "Ideally found quick num %i", rc->quick_num);
            DDpacket(rc->tty_ibuf + start, end - start);
         }
      }

      // clear out the found message
      clearBuffer(rc, end, 1); // 1 for tty
      start=0;
      DDCMSG(D_PARSE, BLACK, "Calling validMessage again @ %i", __LINE__);
   }
   
   // if we put anything in the socket "out" buffer...
   if (added_to_buf) {
      // the socket needs to write it out now and whatever the t2s_* handler said to do
      retval |= mark_sockWrite; 
   }
   
   return retval;
}

// find address from socket to tty message for finding timeslot on way back to tty
void addAddrToNowIDs(rf_connection_t *rc, int addr) {
   int i;
   // check to see if we already of this one in the list
   DDCMSG(D_MEGA, BLACK, "Adding addr %i", addr);
   for (i = 0; i < min(MAX_IDS, rc->id_index+1); i++) {
      if (rc->ids[i] == addr) {
         return;
      }
   }

   // add address to id list
   if (++rc->id_index < MAX_IDS) {
      rc->ids[rc->id_index] = addr;
   }
   DDCMSG(D_MEGA, BLACK, "rc->id_index %i", rc->id_index);
}

// find devid from socket to tty message for finding timeslot on way back to tty
void addDevidToNowDevIDs(rf_connection_t *rc, int devid) {
   int i;
   // check to see if we already of this one in the list
   DDCMSG(D_MEGA, BLACK, "Adding devid: %02X:%02X:%02X:%02X", (devid & 0xff000000) >> 24, (devid & 0xff0000) >> 16, (devid & 0xff00) >> 8, devid & 0xff);
   for (i = 0; i < min(MAX_IDS, rc->devid_index+1); i++) {
      if (rc->devids[i] == devid) {
         return;
      }
   }

   // add address to devid list
   if (++rc->devid_index < MAX_IDS) {
      rc->devids[rc->devid_index] = devid;
   }

   DDCMSG(D_MEGA, BLACK, "rc->devid_index %i", rc->devid_index);
}

int s2t_handle_STATUS_RESP(rf_connection_t *rc, int start, int end) {
   int i;
   LB_status_resp_t *pkt = (LB_status_resp_t *)(rc->sock_ibuf + start);
   DDCMSG(D_RF, CYAN, "s2t_handle_STATUS_RESP(%8p, %i, %i)", rc, start, end);
   addAddrToNowIDs(rc, pkt->addr);
   return doNothing;
} 

int s2t_handle_EVENT_REPORT(rf_connection_t *rc, int start, int end) {
   int i;
   LB_event_report_t *pkt = (LB_event_report_t *)(rc->sock_ibuf + start);
   DDCMSG(D_RF, CYAN, "s2t_handle_EVENT_REPORT(%8p, %i, %i)", rc, start, end);
   addAddrToNowIDs(rc, pkt->addr);
   return doNothing;
} 

int s2t_handle_DEVICE_REG(rf_connection_t *rc, int start, int end) {
   int i;
   LB_device_reg_t *pkt = (LB_device_reg_t *)(rc->sock_ibuf + start);
   DDCMSG(D_RF, CYAN, "s2t_handle_DEVICE_REG(%8p, %i, %i)", rc, start, end);
   debugRF(black, rc->sock_ibuf + start, end-start);
   addDevidToNowDevIDs(rc, pkt->devid);
   return doNothing;
} 

// macro used in sock2tty
#define s2t_HANDLE_RF(RF_NUM) case LBC_ ## RF_NUM : retval = s2t_handle_ ## RF_NUM (rc, start, end) ; break;

// transfer socket data to the tty and set up delay times
int sock2tty(rf_connection_t *rc) {
   int i, start, end, mnum, ts = 0, retval = doNothing; // start doing nothing
   int fakes=0; // keep track of number of fakes counted
   DDCMSG(D_MEGA, BLACK, "Called sock2tty");

   // read all available valid messages up until I have to do something on return
   debugRF(blue, rc->sock_ibuf, rc->sock_ilen);
   start = 0;
   while (retval == doNothing && (mnum = validMessage(rc, &start, &end, 0)) != -1) { // 0 for socket
      switch (mnum) {
         s2t_HANDLE_RF (STATUS_RESP); // keep track of ids
         s2t_HANDLE_RF (EVENT_REPORT); // keep track of ids
         s2t_HANDLE_RF (DEVICE_REG); // keep track of devids
         default:
            break;
      }
      // copy the found message to the tty "out" buffer
      addToBuffer_tty_out(rc, rc->sock_ibuf + start, end - start);

      // clear out the found message
      clearBuffer(rc, end, 0); // 0 for socket
      start=0;
      DDCMSG(D_PARSE, BLACK, "Calling validMessage again @ %i", __LINE__);
   }

   // find which timeslot I'm in (the mcp should always leave us with exactly one of these tests as true)
   DDCMSG(D_T_SLOT, BLACK, "Finding timeslot uinsg rc->id_index (%X) or rc->devid_last_high (%X)", rc->id_index, rc->devid_last_high);
   if (rc->id_index >= 0) {
      // timeslot is decided by which address I am in the chain
      int j;
      ts = -1; // so first slot is 0
      DDCMSG(D_T_SLOT, BLACK, "Finding timeslot using rc->id_index %i", rc->id_index);
      // find the lowest matching address
      for (j = 0; j < min(rc->id_lasttime_index+1, MAX_IDS); j++) {
         DDCMSG(D_T_SLOT, BLACK, "Looking at index %i...", j);
         if (rc->ids_lasttime[j] == FAKE_DEVID) {
            fakes += (4*8); // 8 slots times 4, as 4 fakes count as a regular
            DDCMSG(D_POINTER, GREEN, "chunk of fakes: %i", fakes);
            continue; // move on...
         } else if (rc->ids_lasttime[j] == FAKE_ID) {
            fakes++; // found another fake
            DDCMSG(D_POINTER, GREEN, "another fake: %i", fakes);
         } else {
            ts++; // found a real slot, remember total, mine will be the last we look at
         }
         for (i = 0; i < min(rc->id_index+1, MAX_IDS); i++) {
            DDCMSG(D_T_SLOT, BLACK, "Looking at id_index %i...", i);
            if (rc->ids_lasttime[j] == rc->ids[i]) {
               DDCMSG(D_T_SLOT, BLACK, "Found at %ix%i...%ix%i of %ix%i", j, i, rc->ids_lasttime[j], rc->ids[i], min(rc->id_lasttime_index+1, MAX_IDS), min(rc->id_index+1, MAX_IDS));
               DDCMSG(D_POINTER, BLACK, "Found at %ix%i...%ix%i of %ix%i", j, i, rc->ids_lasttime[j], rc->ids[i], min(rc->id_lasttime_index+1, MAX_IDS), min(rc->id_index+1, MAX_IDS));
               goto found_ts; // just jump down, my j variable is now correct
            }
         }
      }

#if 0
      // label to jump to when I found my timeslot
      found_ts:
      // reset ids so the timeslot resets
      rc->id_index = -1;
      for (i = 0; i < MAX_IDS; i++) {rc->ids[i] = -1;}
      rc->id_lasttime_index = -1;
      for (i = 0; i < MAX_IDS; i++) {rc->ids_lasttime[i] = -1;}
#endif
   } else if (rc->devid_last_high >= 0) {
      int i;
      DDCMSG(D_T_SLOT, BLACK, "Finding timeslot using rc->devid_last_high %X", rc->devid_last_high);
      // first find out how much is in front of this type of timeslot by adding in multiple fakes
      for (ts = 0; ts < min(rc->id_lasttime_index+1, MAX_IDS); ts++) {
         DDCMSG(D_T_SLOT, BLACK, "Looking at ts %i...", ts);
         if (rc->ids_lasttime[ts] == FAKE_DEVID) {
            break; // found our real slot
         } else if (rc->ids_lasttime[ts] == FAKE_ID) {
            fakes++; // found another fake
            DDCMSG(D_POINTER, GREEN, "another of fake: %i", fakes);
         } else {
            fakes += 4; // a regular counts as four fakes
            DDCMSG(D_POINTER, GREEN, "chunk of fakes: %i", fakes);
         }
      }
      ts = 0xffff; // start off as a really big number
      // timeslot is decided by which devid I am in the range, find lowest devid
      for (i = 0; i < min(rc->devid_index+1, MAX_IDS); i++) {
         DDCMSG(D_T_SLOT, BLACK, "Looking %X-%X < %X for index %i", rc->devids[i], rc->devid_last_low, ts, i);
         if ((rc->devids[i] - rc->devid_last_low) < ts) {
            ts = rc->devids[i] - rc->devid_last_low; // exact match would be 0 slot, then 1, etc.
            DDCMSG(D_VERY, BLACK, "Found new low: %i", ts);
         }
      }
      DDCMSG(D_T_SLOT, BLACK, "Ended up with %i", ts);
      if (ts == 0xffff || ts < 0) {
         ts = 0; // should never get here, but just in case be a sane value
      }
#if 0
      // reset ids so the timeslot resets
      rc->devid_index = -1;
      rc->devid_last_low = -1;
      rc->devid_last_high = -1;
      for (i = 0; i < MAX_IDS; i++) {rc->devids[i] = -1;}
#endif
   }
   // label to jump to when I found my timeslot
   found_ts:
   // calculate time remaining from timeslot, initial time, and fakes
   i = rc->timeslot_init + (rc->timeslot_length * ts) + ((rc->timeslot_length * fakes) / 4);
   DDCMSG(D_T_SLOT, BLACK, "Found timeout slot: %i (%i + %i + %i = %i)", ts, rc->timeslot_init, (rc->timeslot_length * ts), ((rc->timeslot_length * fakes) / 4), i);
   DDCMSG(D_POINTER, BLACK, "Found timeout slot: %i (%i + %i + %i(f:%i) = %i)", ts, rc->timeslot_init, (rc->timeslot_length * ts), ((rc->timeslot_length * fakes) / 4), fakes, i);

   // change timeout
   doTimeAfter(rc, i); // initial time + timeslot length * timeslot I'm in + fakes that came before me

   return (retval | doNothing); // the timeout will determine when we can write, right now do nothing or whatever retval was
}


