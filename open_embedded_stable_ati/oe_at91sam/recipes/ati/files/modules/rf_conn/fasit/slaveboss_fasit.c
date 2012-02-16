#include "mcp.h"
#include "slaveboss.h"
#include "fasit_c.h"

// read a single FASIT message into given buffer, return msg num
int fasitRead(int fd, char **dest, int *dests) {
   // read as much as possible
   *dests = read(fd, *dest, FASIT_BUF_SIZE);

   // did we read nothing?
   if (*dests <= 0) {
      if (*dests == 0 || errno == EAGAIN) {
         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
         return rem_fasitEpoll;
      }
   }

   // find message number
   if (*dests >= sizeof(FASIT_header)) {
      FASIT_header *hdr = (FASIT_header*)(*dest);
      int s = FASIT_size(hdr->num);
      if (*dests < s) {
         // if message not big enough, return none (handlers will keep partial message)
         return -1;
      }
      // found good message, return number
      return hdr->num;
   } else {
      // if no message found, return none (handlers will keep partial message)
      return -1;
   }
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

