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
      // move data backwards if we didn't write everything
      char *tbuf;
      fc->fasit_olen -= s;
      tbuf = malloc(fc->fasit_olen);
      memcpy(tbuf, fc->fasit_obuf + (sizeof(char) * s), fc->fasit_olen); // copy to temp
      memcpy(fc->fasit_obuf, tbuf, fc->fasit_olen); // copy back
      free(tbuf);
   } else {
      // everything was written, clear write buffer
      fc->fasit_olen = 0;

      // don't try writing again
      return mark_fasitRead;
   }

   // partial success, leave writeable so we try again
   return doNothing;
}
