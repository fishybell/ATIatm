#include "mcp.h"
#include "slaveboss.h"
#include "rf.h"

// read a single RF message into given buffer, return msg num
int rfRead(int fd, char **dest, int *dests) {
   // read as much as possible
   *dests = read(fd, *dest, RF_BUF_SIZE);

   // did we read nothing?
   if (*dests <= 0) {
      if (*dests == 0 || errno == EAGAIN) {
         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
         return rem_rfEpoll;
      }
   }

   // find message number
   if (*dests >= sizeof(LB_packet_t)) {
      LB_packet_t *hdr = (LB_packet_t*)(*dest);
      int s = RF_size(hdr->cmd);
      if (*dests < s) {
         // if message not big enough, return none (handlers will keep partial message)
         return -1;
      }
      // check crc -- TODO -- make sure we're only checking it once
      if (crc8(*dest, s) != 0) {
         // bad crc, ignore data completely
         *dests = 0;
         return -1;
      }
      // found good message, return number
      return hdr->cmd;
   } else {
      // if no message found, return none (handlers will keep partial message)
      return -1;
   }
}

// write all RF messages for connection in fconnsP
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
      // move data backwards if we didn't write everything
      char *tbuf;
      fc->rf_olen -= s;
      tbuf = malloc(fc->rf_olen);
      memcpy(tbuf, fc->rf_obuf + (sizeof(char) * s), fc->rf_olen); // copy to temp
      memcpy(fc->rf_obuf, tbuf, fc->rf_olen); // copy back
      free(tbuf);
   } else {
      // everything was written, clear write buffer
      fc->rf_olen = 0;

      // don't try writing again
      return mark_rfRead;
   }

   // partial success, leave writeable so we try again
   return doNothing;
}
