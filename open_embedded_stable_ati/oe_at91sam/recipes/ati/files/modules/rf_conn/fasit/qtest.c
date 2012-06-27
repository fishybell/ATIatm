#include "mcp.h"
#include "rf.h"

int verbose = 0x1040;
const char *__PROGRAM__ = "Qtest ";

#define SIZE 4000

int main(int argc, char **argv) {
   queue_item_t *Rx, *Tx, *qi;
   char buf[SIZE], *msg;
   int bs = 0;
   int ad = 1;
   int sequence = 1;
   int cmd = -1;
   int addr = -1;
   int rtime=0, stime=150, itime=150;
   int lqg = 0;

   Rx = malloc(sizeof(queue_item_t));
   memset(Rx, 0, sizeof(queue_item_t));
   Tx = malloc(sizeof(queue_item_t));
   memset(Tx, 0, sizeof(queue_item_t));

   msg = buf;
   while (bs < (SIZE - RF_size(LBC_QUICK_GROUP_BIG))) {
      // create random message
      LB_packet_t *lb = (LB_packet_t*)msg;
      lb->cmd = (rand() % 31); // unseeded, which is good for testing
      while (__ptype(lb->cmd) == 0 || RF_size(lb->cmd) < 3 || (lqg == 1 && (lb->cmd == LBC_QUICK_GROUP_BIG || lb->cmd == LBC_QUICK_GROUP))) { /* keep trying to find random message */
         DDCMSG(D_NEW, RED, "What? trying again: %i %i", lb->cmd, RF_size(lb->cmd));
         lb->cmd = (rand() % 31); // unseeded, which is good for testing
      }
      cmd = lb->cmd;
      if (cmd == LBC_QUICK_GROUP_BIG || cmd == LBC_QUICK_GROUP) {
         LB_quick_group_t *lllq = (LB_quick_group_t*)lb;
         int addrs[3*14], num = (rand() % (3*14)), i;
         for (i=0; i < num; i++) {
            addrs[i] = rand() & 0x7ff; // truncate to 11 bits
         }
         setItemsQR(lllq, addrs, num);
         lqg = 1; // this one was a quick group: don't want two in a row
      } else {
         lqg = 0;
      }
      lb->addr = ad++;
      addr = lb->addr;
      //DDCMSG(D_NEW, RED, "Ended up with: %i/%i %i/%i", lb->cmd, cmd, lb->addr, addr);
      set_crc8(lb);
      //DDCMSG(D_NEW, RED, "After crc: %i/%i %i/%i", lb->cmd, cmd, lb->addr, addr);
      //DDpacket(msg, RF_size(lb->cmd));

      // add to queue
      enQueue(Rx, msg, sequence);
      sequence += 2;

      if (addr != queueTail(Rx)->addr ||
          cmd != queueTail(Rx)->cmd) {
         DCMSG(CYAN, "Angry problem: %i vs. %i/%i and %i vs. %i/%i", 
               addr, lb->addr, queueTail(Rx)->addr,
               cmd, lb->cmd, queueTail(Rx)->cmd);
         exit(-1);
      }
      //DDpacket(msg, RF_size(lb->cmd));

      // move msg buffer
      msg += RF_size(lb->cmd);
      bs += RF_size(lb->cmd);

      //DDqueue(D_POINTER, Rx, "");
      //DDqueue(D_POINTER, queueTail(Rx), "");
      printf("created message %i and have %i bytes left\n", ad-1, SIZE - bs);
   }
   // output original
   DDqueue(D_POINTER, Rx, "Before %i", queueSize(Rx));
   DDqueue(D_POINTER, Tx, "Before %i", queueSize(Tx));
   DDpacket(buf, bs);
   int burst=0;
   while (queueSize(Rx) > 0) {
      printf("\n\nDoing burst %i\n", ++burst);
      // clear for burst
      bs = 254; // max size of 254 in a burst
      memset(buf, 0, bs);
      // burst
      queueBurst(Rx, Tx, buf, &bs, &rtime, &stime, &itime); 
      //DDqueue(D_POINTER, Rx, "After %i", queueSize(Rx));
      //DDqueue(D_POINTER, Tx, "After %i", queueSize(Tx));
      DDpacket(buf, bs);
      if (bs > 254) {
         DCMSG(GRAY, "Burst %i was %i bytes, rtime %i", burst, bs, rtime);
      } else {
         DCMSG(GREEN, "Burst %i was %i bytes, rtime %i", burst, bs, rtime);
      }
   }
   DDqueue(D_POINTER, Rx, "After %i", queueSize(Rx));
   DDqueue(D_POINTER, Tx, "After %i", queueSize(Tx));
}
