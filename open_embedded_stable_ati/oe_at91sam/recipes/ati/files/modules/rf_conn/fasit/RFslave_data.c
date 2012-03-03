#include "mcp.h"
#include "rf_debug.h"
#include "RFslave.h"

// the start and end values may be set even if no valid message is found
static int validMessage(rf_connection_t *rc, int *start, int *end, int tty) {
   LB_packet_t *hdr;
   int hl = -1;
   int mnum = -1;
   *start = 0;
   // loop through entire buffer, parsing starting at each character
   while (*start < (tty ? rc->tty_ilen : rc->sock_ilen) && mnum == -1) {
      // map the memory, we don't need to manipulate it
      if (tty) {
         hdr = (LB_packet_t*)(rc->tty_ibuf + *start);
      } else {
         hdr = (LB_packet_t*)(rc->sock_ibuf + *start);
      }
      
      // check for valid message first
      hl = RF_size(hdr->cmd);
      if (tty && ((rc->tty_ibuf + rc->tty_ilen) - (rc->tty_ibuf + *start)) > hl) {
         *start = *start + 1; continue; // invalid message length, or more likely, don't have all of the message yet
      } else if (((rc->sock_ibuf + rc->sock_ilen) - (rc->sock_ibuf + *start)) > hl) {
         *start = *start + 1; continue; // invalid message length, or more likely, don't have all of the message yet
      }

      *end = *start + hl;
      DDCMSG(D_RF, tty ? cyan : blue, "Checking %s validMessage for %i", tty ? "TTY" : "SOCKET", hdr->cmd);
      debugRF(tty ? cyan : blue, tty ? rc->tty_ibuf : rc->sock_ibuf);
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
               DDCMSG(D_RF, tty ? cyan : blue, "%s VALID CRC", tty ? "TTY" : "SOCKET");
               return hdr->cmd;
            } else {
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
   int ret;
   struct timespec tv;

   // do infinite timeout if the values are the same
   if (rc->timeout_start.tv_sec == rc->time_start.tv_sec &&
       rc->timeout_start.tv_nsec == rc->time_start.tv_nsec) {
      return INFINITE;
   }

   // get current time
   clock_gettime(CLOCK_MONOTONIC,&tv);

   DDCMSG(D_TIME, RED, "Now: %i:%i, Then: %i:%i=>", tv.tv_sec, tv.tv_nsec, rc->timeout_start.tv_sec, rc->timeout_start.tv_nsec, rc->timeout_end.tv_sec, rc->timeout_end.tv_nsec);
   // subtract seconds straight out
   tv.tv_sec = rc->timeout_start.tv_sec - tv.tv_sec;

   // check to see if we're past our allowed timeslot
   if (tv.tv_sec > rc->timeout_end.tv_sec ||
      (tv.tv_sec == rc->timeout_end.tv_sec &&
       tv.tv_nsec > rc->timeout_start.tv_nsec)) {
      DDCMSG(D_TIME, RED, "Waited too long, infinite timeout now");
      // flush output tty buffer because we missed our timeslot
      rc->tty_olen = 0;
      return INFINITE;
   }

   // subtract microseconds, possibly "carrying the one"
   if (rc->timeout_start.tv_nsec < tv.tv_nsec) {
      // we need to carry
      tv.tv_nsec -= 1000000000l;
      if (tv.tv_sec >= 0) {
         tv.tv_sec--;
      } else {
         tv.tv_sec++;
      }
   }

   // subtract the miscroseconds
   tv.tv_nsec = rc->timeout_start.tv_nsec - tv.tv_nsec;

   // convert to milliseconds
   ret = (tv.tv_sec * 1000) + (tv.tv_nsec / 1000000l);
   DDCMSG(D_TIME, RED, "Later: %i:%i => %i", tv.tv_sec, tv.tv_nsec, ret);

   // timeout early so we wait just the right amount of time later with waitRest()
   if (ret <= 5) {
      return 0; // timeout is 0, which for epoll is return immediately
   } else {
      return max(1, ret - 5); // timeout is positive, it will wait this amount of time
   }
}

// set the timeout for X milliseconds after the start time
void doTimeAfter(rf_connection_t *rc, int msecs) {
   DDCMSG(D_TIME, RED, "Add to timeout %i, %i:%i", msecs, rc->timeout_start.tv_sec, rc->timeout_start.tv_nsec);
   // set infinite wait by making the start and end time the same
   if (msecs == INFINITE) {
      rc->timeout_start.tv_sec = rc->time_start.tv_sec;
      rc->timeout_start.tv_nsec = rc->time_start.tv_nsec;
      return;
   }

   // calculate the lower end time
   rc->timeout_start.tv_sec = rc->time_start.tv_sec + (msecs / 1000); // add the right amount of seconds
   rc->timeout_start.tv_nsec = rc->time_start.tv_nsec + ((msecs - ((msecs/1000)*1000)) * 1000000l); // convoluted math makes it so we don't have an overrun on the tv_nsec size
   if (rc->timeout_start.tv_nsec >= 1000000000l) { // should be off by only one second, so don't loop
      // did this push us to the next second (or more) ?
      rc->timeout_start.tv_sec++;
      rc->timeout_start.tv_nsec -= 1000000000l;
   }

   // calculate the upper end time
   msecs += ((2*rc->timeslot_length)/3); // end time is start time plus 2/3 the timeslot length (to allow for transmission time)
   rc->timeout_end.tv_sec = rc->time_start.tv_sec + (msecs / 1000); // add the right amount of seconds
   rc->timeout_end.tv_nsec = rc->time_start.tv_nsec + ((msecs - ((msecs/1000)*1000)) * 1000000l); // convoluted math makes it so we don't have an overrun on the tv_nsec size
   if (rc->timeout_end.tv_nsec >= 1000000000l) { // should be off by only one second, so don't loop
      // did this push us to the next second (or more) ?
      rc->timeout_end.tv_sec++;
      rc->timeout_end.tv_nsec -= 1000000000l;
   }

   DDCMSG(D_TIME, RED, "Timeout changed to %i:%i => %i:%i", rc->timeout_start.tv_sec, rc->timeout_start.tv_nsec, rc->timeout_end.tv_sec, rc->timeout_end.tv_nsec);
}

// wait until the timeout time arrives (the epoll timeout will get us close)
void waitRest(rf_connection_t *rc) {
   struct timespec tv;

   // wait until we arrive at the valid time (please only do this for periods under 5 milliseconds
   do {
      // get current time
      clock_gettime(CLOCK_MONOTONIC,&tv);
      DDCMSG(D_TIME, RED, "Looking if %i:%i > %i:%i", tv.tv_sec, tv.tv_nsec, rc->timeout_start.tv_sec, rc->timeout_start.tv_nsec);
   } while (tv.tv_sec < rc->timeout_start.tv_sec || /* old seconds are greater OR ...*/
           (tv.tv_sec == rc->timeout_start.tv_sec && /* ... [seconds are equal AND ... */
            tv.tv_nsec < rc->timeout_start.tv_nsec)); /* ... old microseconds are greater] */
   
   // reset timer now
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
   dests = read(tty ? rc->tty : rc->sock, dest, RF_BUF_SIZE);
   err = errno; // save errno

   DDCMSG(D_PACKET, tty ? CYAN : BLUE, "%s READ %i BYTES", tty ? "TTY" : "SOCKET", dests);

   // did we read nothing?
   if (dests <= 0) {
      DDCMSG(D_PACKET, tty ? CYAN : BLUE, "ERROR: %i", err);
      perror("Error: ");
      return rem_ttyEpoll;
      if (err == EAGAIN) {
         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
         perror("Died because ");
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

// if tty is 1, write data from buffer to tty, otherwise write data from buffer into socket
int rcWrite(rf_connection_t *rc, int tty) {
   int s, err;
   DDCMSG(D_RF, tty ? cyan : blue, "%s WRITE", tty ? "TTY" : "SOCKET");

   // reset times
   rc->time_start = rc->timeout_end; // reset to latest time
   rc->timeout_start = rc->timeout_end; // reset to latest time

   // have something to write?
   if ((tty ? rc->tty_olen : rc->sock_olen) <= 0) {
      // we only send data, or listen for writability, if we have something to write
      return tty ? mark_ttyRead : mark_sockRead;
   }

   // write all the data we can
   if (tty) {
      s = write(rc->tty, rc->tty_obuf, rc->tty_olen);
   } else {
      s = write(rc->sock, rc->sock_obuf, rc->sock_olen);
   }
   err = errno; // save errno
   DDCMSG(D_RF, tty ? cyan : blue, "%s WROTE %i BYTES", tty ? "TTY" : "SOCKET", s);
   debugRF(tty ? cyan : blue, tty ? rc->tty_obuf : rc->sock_obuf);

   // did it fail?
   if (s <= 0) {
      if (s == 0 || err == EAGAIN) {
         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
         perror("Died because ");
         DDCMSG(D_RF, tty ? cyan : blue, "%s Dead at %i", tty ? "TTY" : "SOCKET", __LINE__);
         return tty ? rem_ttyEpoll : rem_sockEpoll;
      }
   } else if (s < (tty ? rc->tty_olen : rc->sock_olen)) {
      // we can't leave only a partial message going out, finish writing even if we block
      setblocking(tty ? rc->tty : rc->sock);

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
            perror("Died because ");
            DDCMSG(D_RF, tty ? cyan : blue, "%s Dead at %i", tty ? "TTY" : "SOCKET", __LINE__);
            return tty ? rem_ttyEpoll : rem_sockEpoll;
         }
         s += ns; // increase total written, possibly by zero
      }

      // change to non-blocking from blocking
      setnonblocking(tty ? rc->tty : rc->sock, 0); // no extra socket stuff this time

      // don't try writing again
      return tty ? mark_ttyRead : mark_sockRead;
   } else {
      // everything was written, clear write buffer
      if (tty) {
         rc->tty_olen = 0;
      } else {
         rc->sock_olen = 0;
      }

      // don't try writing again
      return tty ? mark_ttyRead : mark_sockRead;
   }

   // partial success, leave writeable so we try again
   return doNothing;
}

// clear "in" buffer for tty
static void clearBuffer_tty(rf_connection_t *rc, int end) {
   if (end >= rc->tty_ilen) {
DCMSG(RED, "clearing entire buffer");
      // clear the entire buffer
      rc->tty_ilen = 0;
   } else {
      // clear out everything up to and including end
      char tbuf[RF_BUF_SIZE];
DCMSG(RED, "clearing buffer partially: %i-%i", rc->tty_ilen, end);
      D_memcpy(tbuf, rc->tty_ibuf + (sizeof(char) * end), rc->tty_ilen - end);
      D_memcpy(rc->tty_ibuf, tbuf, rc->tty_ilen - end);
      rc->tty_ilen -= end;
   }
DCMSG(RED, "buffer after: %i", rc->tty_ilen);
}

// clear "in" buffer for socket
static void clearBuffer_sock(rf_connection_t *rc, int end) {
   if (end >= rc->sock_ilen) {
      // clear the entire buffer
      rc->sock_ilen = 0;
   } else {
      // clear out everything up to and including end
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

void addToBuffer_tty_out(rf_connection_t *rc, char *buf, int s) {
   addToBuffer_generic(rc->tty_obuf, &rc->tty_olen, buf, s);
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
   for (i = 0; i < min(MAX_IDS, rc->id_lasttime_index+1); i++) {
      if (rc->ids_lasttime[i] == addr) {
         return;
      }
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
   addAddrToLastIDs(rc, pkt->addr);
   return doNothing;
} 

int t2s_handle_EXPOSE(rf_connection_t *rc, int start, int end) {
   int i;
   LB_expose_t *pkt = (LB_expose_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_EXPOSE(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, pkt->addr);
   return doNothing;
} 

int t2s_handle_MOVE(rf_connection_t *rc, int start, int end) {
   int i;
   LB_move_t *pkt = (LB_move_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_MOVE(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, pkt->addr);
   return doNothing;
} 

int t2s_handle_CONFIGURE_HIT(rf_connection_t *rc, int start, int end) {
   int i;
   LB_configure_t *pkt = (LB_configure_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_CONFIGURE_HIT(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, pkt->addr);
   return doNothing;
} 

int t2s_handle_GROUP_CONTROL(rf_connection_t *rc, int start, int end) {
   int i;
   LB_group_control_t *pkt = (LB_group_control_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_GROUP_CONTROL(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, pkt->addr);
   return doNothing;
} 

int t2s_handle_AUDIO_CONTROL(rf_connection_t *rc, int start, int end) {
   int i;
   LB_audio_control_t *pkt = (LB_audio_control_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_AUDIO_CONTROL(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, pkt->addr);
   return doNothing;
} 

int t2s_handle_POWER_CONTROL(rf_connection_t *rc, int start, int end) {
   int i;
   LB_power_control_t *pkt = (LB_power_control_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_POWER_CONTROL(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, pkt->addr);
   return doNothing;
} 

int t2s_handle_PYRO_FIRE(rf_connection_t *rc, int start, int end) {
   int i;
   LB_pyro_fire_t *pkt = (LB_pyro_fire_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_PYRO_FIRE(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, pkt->addr);
   return doNothing;
} 

int t2s_handle_QEXPOSE(rf_connection_t *rc, int start, int end) {
   int i;
   LB_packet_t *pkt = (LB_packet_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_QEXPOSE(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, pkt->addr);
   return doNothing;
} 

int t2s_handle_QCONCEAL(rf_connection_t *rc, int start, int end) {
   int i;
   LB_packet_t *pkt = (LB_packet_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_QCONCEAL(%8p, %i, %i)", rc, start, end);
   addAddrToLastIDs(rc, pkt->addr);
   return doNothing;
} 

int t2s_handle_REQUEST_NEW(rf_connection_t *rc, int start, int end) {
   int i;
   LB_request_new_t *pkt = (LB_request_new_t *)(rc->tty_ibuf + start);
   DDCMSG(D_RF, CYAN, "t2s_handle_REQUEST_NEW(%8p, %i, %i)", rc, start, end);

   // remember last low/high devid
   DCMSG(BLACK, "Setting low/high to %i/%i", pkt->low_dev, pkt->high_dev);
   rc->devid_last_low = pkt->low_dev;
   rc->devid_last_high = pkt->high_dev;

   // remember timeslot length
   rc->timeslot_length = pkt->slottime * 5; // convert to milliseconds
   DCMSG(BLACK, "Setting timeslot length to %i", rc->timeslot_length);

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

   // read all available valid messages up until I have to do something on return
   while (retval == doNothing && (mnum = validMessage(rc, &start, &end, 1)) != -1) { // 1 for tty
      switch (mnum) {
         t2s_HANDLE_RF (STATUS_REQ); // keep track of ids
         t2s_HANDLE_RF (EXPOSE); // keep track of ids
         t2s_HANDLE_RF (MOVE); // keep track of ids
         t2s_HANDLE_RF (CONFIGURE_HIT); // keep track of ids
         t2s_HANDLE_RF (GROUP_CONTROL); // keep track of ids
         t2s_HANDLE_RF (AUDIO_CONTROL); // keep track of ids
         t2s_HANDLE_RF (POWER_CONTROL); // keep track of ids
         t2s_HANDLE_RF (PYRO_FIRE); // keep track of ids
         t2s_HANDLE_RF (QEXPOSE); // keep track of ids
         t2s_HANDLE_RF (QCONCEAL); // keep track of ids
         t2s_HANDLE_RF (REQUEST_NEW); // keep track of devids
         t2s_HANDLE_RF (ASSIGN_ADDR); // keep track of ids
         default:
            break;
      }
      // copy the found message to the socket "out" buffer
      addToBuffer_sock_out(rc, rc->tty_ibuf + start, end - start);

      // clear out the found message
      clearBuffer(rc, end, 1); // 1 for tty

      // change the start time to now
      clock_gettime(CLOCK_MONOTONIC,&rc->time_start);
      rc->timeout_start = rc->time_start; // reset the start time as well
      rc->timeout_end = rc->time_start; // reset the end time as well
      DDCMSG(D_PACKET, RED, "Clock changed to %i:%i", rc->time_start.tv_sec, rc->time_start.tv_nsec);

      // the socket needs to write it out now and whatever the t2s_* handler said to do
      retval |= mark_sockWrite; 
   }
   
   return retval;
}

// find address from socket to tty message for finding timeslot on way back to tty
void addAddrToNowIDs(rf_connection_t *rc, int addr) {
   int i;
   // check to see if we already of this one in the list
   DCMSG(BLACK, "Adding addr %i", addr);
   for (i = 0; i < min(MAX_IDS, rc->id_index+1); i++) {
      if (rc->ids[i] == addr) {
         return;
      }
   }

   // add address to id list
   if (++rc->id_index < MAX_IDS) {
      rc->ids[rc->id_index] = addr;
   }
   DCMSG(BLACK, "rc->id_index %i", rc->id_index);
}

// find devid from socket to tty message for finding timeslot on way back to tty
void addDevidToNowDevIDs(rf_connection_t *rc, int devid) {
   int i;
   // check to see if we already of this one in the list
   DCMSG(BLACK, "Adding devid: %02X:%02X:%02X:%02X", (devid & 0xff000000) >> 24, (devid & 0xff0000) >> 16, (devid & 0xff00) >> 8, devid & 0xff);
   for (i = 0; i < min(MAX_IDS, rc->devid_index+1); i++) {
      if (rc->devids[i] == devid) {
         return;
      }
   }

   // add address to devid list
   if (++rc->devid_index < MAX_IDS) {
      rc->devids[rc->devid_index] = devid;
   }
   DCMSG(BLACK, "rc->devid_index %i", rc->devid_index);
}

int s2t_handle_STATUS_RESP_LIFTER(rf_connection_t *rc, int start, int end) {
   int i;
   LB_status_resp_lifter_t *pkt = (LB_status_resp_lifter_t *)(rc->sock_ibuf + start);
   DDCMSG(D_RF, CYAN, "s2t_handle_STATUS_RESP_LIFTER(%8p, %i, %i)", rc, start, end);
   addAddrToNowIDs(rc, pkt->addr);
   return doNothing;
} 

int s2t_handle_STATUS_RESP_MOVER(rf_connection_t *rc, int start, int end) {
   int i;
   LB_status_resp_mover_t *pkt = (LB_status_resp_mover_t *)(rc->sock_ibuf + start);
   DDCMSG(D_RF, CYAN, "s2t_handle_STATUS_RESP_MOVER(%8p, %i, %i)", rc, start, end);
   addAddrToNowIDs(rc, pkt->addr);
   return doNothing;
} 

int s2t_handle_STATUS_RESP_EXT(rf_connection_t *rc, int start, int end) {
   int i;
   LB_status_resp_ext_t *pkt = (LB_status_resp_ext_t *)(rc->sock_ibuf + start);
   DDCMSG(D_RF, CYAN, "s2t_handle_STATUS_RESP_EXT(%8p, %i, %i)", rc, start, end);
   addAddrToNowIDs(rc, pkt->addr);
   return doNothing;
} 

int s2t_handle_STATUS_NO_RESP(rf_connection_t *rc, int start, int end) {
   int i;
   LB_status_no_resp_t *pkt = (LB_status_no_resp_t *)(rc->sock_ibuf + start);
   DDCMSG(D_RF, CYAN, "s2t_handle_STATUS_NO_RESP(%8p, %i, %i)", rc, start, end);
   addAddrToNowIDs(rc, pkt->addr);
   return doNothing;
} 

int s2t_handle_DEVICE_REG(rf_connection_t *rc, int start, int end) {
   int i;
   LB_device_reg_t *pkt = (LB_device_reg_t *)(rc->sock_ibuf + start);
   DDCMSG(D_RF, CYAN, "s2t_handle_DEVICE_REG(%8p, %i, %i)", rc, start, end);
   debugRF(black, rc->sock_ibuf + start);
   addDevidToNowDevIDs(rc, pkt->devid);
   return doNothing;
} 

// macro used in sock2tty
#define s2t_HANDLE_RF(RF_NUM) case LBC_ ## RF_NUM : retval = s2t_handle_ ## RF_NUM (rc, start, end) ; break;

// transfer socket data to the tty and set up delay times
int sock2tty(rf_connection_t *rc) {
   int start, end, mnum, ts = 0, retval = doNothing; // start doing nothing
   DCMSG(BLACK, "Called sock2tty");

   // read all available valid messages up until I have to do something on return
   while (retval == doNothing && (mnum = validMessage(rc, &start, &end, 0)) != -1) { // 0 for socket
      switch (mnum) {
         s2t_HANDLE_RF (STATUS_RESP_LIFTER); // keep track of ids
         s2t_HANDLE_RF (STATUS_RESP_MOVER); // keep track of ids
         s2t_HANDLE_RF (STATUS_RESP_EXT); // keep track of ids
         s2t_HANDLE_RF (STATUS_NO_RESP); // keep track of ids
         s2t_HANDLE_RF (DEVICE_REG); // keep track of devids
         default:
            break;
      }
      // copy the found message to the tty "out" buffer
      addToBuffer_tty_out(rc, rc->sock_ibuf + start, end - start);

      // clear out the found message
      clearBuffer(rc, end, 0); // 0 for socket
   }

   // find which timeslot I'm in (the mcp should always leave us with exactly one of these tests as true)
   DCMSG(BLACK, "Finding timeslot uinsg rc->id_index (%i) or rc->devid_last_high (%i)", rc->id_index, rc->devid_last_high);
   if (rc->id_index >= 0) {
      // timeslot is decided by which address I am in the chain
      int i;
      DCMSG(BLACK, "Finding timeslot using rc->id_index %i", rc->id_index);
      // find the lowest matching address
      for (ts = 0; ts < min(rc->id_lasttime_index+1, MAX_IDS); ts++) {
         for (i = 0; i < min(rc->id_index+1, MAX_IDS); i++) {
            if (rc->ids_lasttime[ts] == rc->ids[i]) {
               goto found_ts; // just jump down, my ts variable is now correct
            }
         }
      }

      // reset ids so the timeslot resets
      rc->id_index = -1;
   } else if (rc->devid_last_high >= 0) {
      int i;
      DCMSG(BLACK, "Finding timeslot using rc->devid_last_high %i", rc->devid_last_high);
      ts = 0xffff; // start off as a really big number
      // timeslot is decided by which devid I am in the range, find lowest devid
      for (i = 0; i < min(rc->devid_index+1, MAX_IDS); i++) {
         DCMSG(BLACK, "Looking %i-%i < %i for index %i", rc->devids[i], rc->devid_last_low, ts, i);
         if ((rc->devids[i] - rc->devid_last_low) < ts) {
            ts = rc->devids[i] - rc->devid_last_low; // exact match would be 0 slot, then 1, etc.
            DCMSG(BLACK, "Found new low: %i", ts);
         }
      }
      DCMSG(BLACK, "Ended up with %i", ts);
      if (ts == 0xffff || ts < 0) {
         ts = 0; // should never get here, but just in case be a sane value
      }

      // reset ids so the timeslot resets
      rc->devid_index = -1;
      rc->devid_last_low = -1;
      rc->devid_last_high = -1;
   }

   // label to jump to when I found my timeslot
   found_ts:
   DCMSG(BLACK, "Found timeout slot: %i", ts);

   // change timeout
   doTimeAfter(rc, rc->timeslot_length * (ts + 1)); // timeslot length * timeslot I'm in, minimum of timeslot_length

   return (retval | doNothing); // the timeout will determine when we can write, right now do nothing or whatever retval was
}


