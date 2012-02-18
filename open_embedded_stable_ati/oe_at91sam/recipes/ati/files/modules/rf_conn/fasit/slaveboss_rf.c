#include "mcp.h"
#include "slaveboss.h"
#include "rf.h"

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

