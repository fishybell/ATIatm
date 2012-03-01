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
      DDCMSG(D_RF, tty ? cyan : blue, "%s validMessage for %i", tty ? "TTY" : "SOCKET", hdr->cmd);
      debugRF(tty ? cyan : blue, tty ? rc->tty_ibuf : rc->sock_ibuf);
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
   return mnum;
}

// returns the timeout value to pass to epoll
int getTimeout(rf_connection_t *rc) {
   int ret;
   struct timespec tv;

   // do infinite timeout if the values are the same
   if (rc->timeout_when.tv_sec == rc->time_start.tv_sec &&
       rc->timeout_when.tv_nsec == rc->time_start.tv_nsec) {
      return INFINITE;
   }

   // get current time
   clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&tv);

   // subtract seconds straight out
   tv.tv_sec = rc->timeout_when.tv_sec - rc->timeout_when.tv_sec;

   // subtract microseconds, possibly "carrying the one"
   if (rc->timeout_when.tv_nsec < tv.tv_nsec) {
      // we need to carry
      tv.tv_nsec -= 1000000000l;
      tv.tv_sec--;
   }

   // subtract the miscroseconds
   tv.tv_nsec = rc->timeout_when.tv_nsec - tv.tv_nsec;

   // convert to milliseconds
   ret = (tv.tv_sec * 1000) + (tv.tv_nsec / 1000000l);

   return min(1, ret - 5); // timeout early so we wait just the right amount of time later with waitRest()
}

// set the timeout for X milliseconds after the start time
void doTimeAfter(rf_connection_t *rc, int msecs) {
   // set infinite wait by making the start and end time the same
   if (msecs == INFINITE) {
      rc->timeout_when.tv_sec = rc->time_start.tv_sec;
      rc->timeout_when.tv_nsec = rc->time_start.tv_nsec;
      return;
   }

   // calculate the end time
   rc->timeout_when.tv_sec = rc->time_start.tv_sec;
   rc->timeout_when.tv_nsec = rc->time_start.tv_nsec + (msecs * 1000000l);
   while (rc->timeout_when.tv_nsec >= 1000000000l) {
      // did this push us to the next second (or more) ?
      rc->timeout_when.tv_sec++;
      rc->timeout_when.tv_nsec -= 1000000000l;
   }
}

// wait until the timeout time arrives (the epoll timeout will get us close)
void waitRest(rf_connection_t *rc) {
   struct timespec tv;

   // wait until we arrive at the valid time (please only do this for periods under 5 milliseconds
   do {
      // get current time
      clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&tv);
   } while (tv.tv_sec > rc->timeout_when.tv_sec || /* seconds are greater OR ...*/
          (tv.tv_sec == rc->timeout_when.tv_sec && /* ... [seconds are equal AND ... */
           tv.tv_nsec > rc->timeout_when.tv_nsec)); /* ... microseconds are greater] */
}

// if tty is 1, read data from tty into buffer, otherwise read data from socket into buffer
int rcRead(rf_connection_t *rc, int tty) {
   char dest[RF_BUF_SIZE];
   int dests;
   DDCMSG(D_PACKET, tty ? CYAN : BLUE, "%s READING", tty ? "TTY" : "SOCKET");

   // read as much as possible
   dests = read(tty ? rc->tty : rc->sock, dest, RF_BUF_SIZE);

   DDCMSG(D_PACKET, tty ? CYAN : BLUE, "%s READ %i BYTES", tty ? "TTY" : "SOCKET", dests);

   // did we read nothing?
   if (dests <= 0) {
      DDCMSG(D_PACKET, tty ? CYAN : BLUE, "ERROR: %i", errno);
      perror("Error: ");
      if (errno == EAGAIN) {
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
   int s;
   DDCMSG(D_RF, tty ? cyan : blue, "%s WRITE", tty ? "TTY" : "SOCKET");

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
   DDCMSG(D_RF, tty ? cyan : blue, "%s WROTE %i BYTES", tty ? "TTY" : "SOCKET", s);

   // did it fail?
   if (s <= 0) {
      if (s == 0 || errno == EAGAIN) {
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
         if (ns < 0 && errno != EAGAIN) {
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
      // clear the entire buffer
      rc->tty_ilen = 0;
   } else {
      // clear out everything up to and including end
      char tbuf[RF_BUF_SIZE];
      D_memcpy(tbuf, rc->tty_ibuf + (sizeof(char) * end), rc->tty_ilen - end);
      D_memcpy(rc->tty_ibuf, tbuf, rc->tty_ilen - end);
      rc->tty_ilen -= end;
   }
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
         t2s_HANDLE_RF (DEVICE_ADDR); // keep track of ids
         default:
            break;
      }
      // copy the found message to the socket "out" buffer
      addToBuffer_sock_out(rc, rc->tty_ibuf + start, end - start);

      // clear out the found message
      clearBuffer(rc, end, 1); // 1 for tty

      // change the start time to now
      clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&rc->time_start);
   }
   // parse the message buffer and...
   //                             ...keep track of ids that I'm listening on
   //                             ...see if there is a change to the timeslot length
   //                             ...see what timeslot to use
   
   
   return (retval | mark_sockWrite); // the socket needs to write it out now and whatever the t2s_* handler said to do
}

// macro used in sock2tty
#define s2t_HANDLE_RF(RF_NUM) case LBC_ ## RF_NUM : retval = s2t_handle_ ## RF_NUM (rc, start, end) ; break;

// transfer socket data to the tty and set up delay times
int sock2tty(rf_connection_t *rc) {
   int start, end, mnum, retval = doNothing; // start doing nothing

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
      clearBuffer(rc, end, 1); // 1 for socket

      // change the start time to now
      clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&rc->time_start);
   }
   // parse the message buffer and...
   //                             ...keep track of ids that I'm sending on

   // TODO -- find which timeslot I'm in
   doTimeAfter(rc, rc->timeslot_length);

   return (retval | doNothing); // the timeout will determine when we can write, right now do nothing or whatever retval was
}


