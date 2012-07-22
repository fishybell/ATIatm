#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <stdio.h>

#include "mcp.h"
#include "rf.h"
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <netinet/tcp.h>
#include <stdarg.h>

extern int verbose;

#ifndef strnlen
size_t strnlen(const char *s, size_t maxlen);
#endif


#if 0 /* old queue stuff */
// the RFmaster may eventually benifit if these are circular buffers, but for now
//  we just assume that they return to 'empty' often which is easily done by just reseting the pointers
queue_t *queue_init(int size){
   queue_t *M;

   M=(queue_t *)malloc(size+sizeof(queue_t));
   M->size=size;
   M->tail=M->buf;
   M->head=M->buf;

   return M;
}

// add count bytes to the tail of the queue
void EnQueue(queue_t *M, void *ptr,int count){
   char *data=ptr;
   while(count--) *M->tail++=*data++ ;
}

// add count bytes to the head of the queue (makes it not a queue, but I won't tell if you don't)
void QueuePush(queue_t *M, void *ptr,int count){
   char *tbuf = malloc(M->size);
   memcpy(tbuf+count, M->head, Queue_Depth(M));   // copy old queue stuff to front...ish
   memcpy(tbuf, ptr, count);                      // copy new stuff to front
   memcpy(M->head, tbuf, Queue_Depth(M) + count); // copy back everything
   free(tbuf);
   M->tail += count;
}


/*** peek at the queue and return a
 ***   0 if there is not a complete packet at the head
 ***   1 if there is a request_new_devices packet
 ***   2 if there is a status_req (or other packet that is expecting a response)
 ***   3 if there is a command that expects no response
 ***
 ***   I suppose it could check the CRC or something too, and return something different in that case,
 ***   but it was first written to look at commands from the MCP which should always be without crc errors
 ***/

int QueuePtype(queue_t *M){
   int depth;
   LB_packet_t *LB;

   depth=Queue_Depth(M);

   if (depth>=3) { // there are enough bytes for a command
      LB=(LB_packet_t *)M->head;
      if (RF_size(LB->cmd)<=depth){ // there are enough bytes for this command
         return __ptype(LB->cmd);
      }
   }
   return(0);
}

// remove count bytes from the head of the queue and normalize the queue
void DeQueue(queue_t *M,int count){
   memmove(M->buf,M->head+count,Queue_Depth(M));
   M->tail-=count;
   M->head=M->buf;
}

void ClearQueue(queue_t *M){
   M->tail=M->buf;
   M->head=M->buf;
}

// move count from the front of queue Msrc to tail of Mdst
//   and shift up the old queue for now
// having bounds checking and stuff might be nice during debugging
void ReQueue(queue_t *Mdst,queue_t *Msrc,int count){
   memcpy(Mdst->tail,Msrc->head,count); // copy count bytes
   Mdst->tail+=count;                           // increment the tail
   DeQueue(Msrc,count); // and remove from src queue 
}

#endif /* end of old queue stuff */

// internal ptype function, used by QueuePtype, PType, and queuePtype
int __ptype(int cmd) {
   switch (cmd) {
      case LBC_ILLEGAL: return 0;
      case LBC_REQUEST_NEW: return 1;
      case LBC_STATUS_REQ: return 2;
      case LB_CONTROL_QUEUE:
      case LB_CONTROL_SENT:
      case LB_CONTROL_REMOVED:
         return 4;
      case LBC_QUICK_GROUP:
      case LBC_QUICK_GROUP_BIG:
         return 5;
      case LBC_CONFIGURE_HIT:
      case LBC_HIT_BLANKING:
      case LBC_ACCESSORY:
      case LBC_QCONCEAL:
      case LBC_QEXPOSE:
      case LBC_EXPOSE:
      case LBC_MOVE:
      case LBC_ASSIGN_ADDR:
         return 6;
      default: return 3;
   }
}

// does the same as QueuePtype only with a buffer - uses CRC8 to decide if it was complete
int Ptype(char *buf){
   int crc;
   LB_packet_t *LB;

   LB=(LB_packet_t *)buf;
   if (LB->cmd==LBC_ILLEGAL) return(0);    
//   if (LB->cmd==LBC_ILLEGAL_CANCEL) return(0);    
   crc=crc8(buf);
   if (!crc){ // there seemed to be a good command
      return __ptype(LB->cmd);
   }
   return(0);
}

//  this is a macro
//int Queue_Depth(queue_t *M){
//    M->tail-M->head;
//}


void print_verbosity(void){
   printf("  -v xx         set verbosity bits.   Examples:\n");
   printf("  -v 1           sets D_PACKET  for packet info\n");
   printf("  -v 2           sets D_RF    \n");
   printf("  -v 4           sets D_CRC   \n");
   printf("  -v 8           sets D_POLL  \n");
   printf("  -v 10          sets D_TIME  \n");
   printf("  -v 20          sets D_VERY  \n");
   printf("  -v 40          sets D_NEW  \n");
   printf("  -v 80          sets D_MEGA  \n");
   printf("  -v 100         sets D_MINION  \n");
   printf("  -v 200         sets D_MSTATE  \n");
   printf("  -v 400         sets D_QUEUE  \n");
   printf("  -v 800         sets D_PARSE  \n");
   printf("  -v 1000        sets D_POINTER  \n");
   printf("  -v 2000        sets D_T_SLOT  \n");
   printf("  -v FFFF        sets all of the above  \n");
}

void print_verbosity_bits(void){
   DDCMSG(D_PACKET      ,black,"  D_PACKET");
   DDCMSG(D_RF          ,black,"  D_RF");
   DDCMSG(D_CRC         ,black,"  D_CRC");
   DDCMSG(D_POLL        ,black,"  D_POLL");
   DDCMSG(D_TIME        ,black,"  D_TIME");
   DDCMSG(D_VERY        ,black,"  D_VERY");
   DDCMSG(D_NEW         ,black,"  D_NEW");   
   DDCMSG(D_MEGA        ,black,"  D_MEGA");
   DDCMSG(D_MINION      ,black,"  D_MINION");
   DDCMSG(D_MSTATE      ,black,"  D_MSTATE");
   DDCMSG(D_QUEUE       ,black,"  D_QUEUE");
   DDCMSG(D_PARSE       ,black,"  D_PARSE");
   DDCMSG(D_POINTER     ,black,"  D_POINTER");
   DDCMSG(D_T_SLOT      ,black,"  D_T_SLOT");
}


int RF_size(int cmd){
   // set LB_size  based on which command it is
   switch (cmd){
      case  LBC_QEXPOSE:
      case  LBC_BURST:
      case  LBC_RESET:
/*      case  LBC_ILLEGAL_CANCEL: */
      case  LBC_STATUS_REQ:
         return (3);

      case  LBC_POWER_CONTROL:
      case  LBC_PYRO_FIRE:
         return (4);

      case  LBC_MOVE:
      case  LBC_GROUP_CONTROL:
      case  LBC_HIT_BLANKING:
      case  LB_CONTROL_QUEUE:
      case  LB_CONTROL_SENT:
      case  LB_CONTROL_REMOVED:
         return (5);

      case  LBC_AUDIO_CONTROL:
      case  LBC_CONFIGURE_HIT:
      case  LBC_ACCESSORY:
         return (6);

      case  LBC_REPORT_ACK:
      case  LBC_EVENT_REPORT:
      case  LBC_EXPOSE:
      case  LBC_ASSIGN_ADDR:
      case  LBC_QCONCEAL:
         return (7);

      case  LBC_REQUEST_NEW:
         return (8);

      case  LBC_STATUS_RESP:
         return (11);

      case  LBC_DEVICE_REG:
         return (13);

      case  LBC_QUICK_GROUP:
         return (65);

      case  LBC_QUICK_GROUP_BIG:
         return (165);

      default:
         return (1);
   }
}

//   and in fact, there needs to be more error checking, and throwing away of bad checksum packets
//   not sure how to re-sync after a garbage packet - probably have to zero it out one byte at a time.

int gather_rf(int fd, char *tail,int max){
   int ready;
   int err;
   
   /* read as much as we can or max from the non-blocking fd. */
   ready=read(fd,tail,max);
   err=errno;
   DDCMSG(D_VERY,GREEN,"gather_rf:  errno=%d new bytes=%2d  .%02x.%02x.%02x.%02x.  tail=%p ",errno,ready,(uint8)tail[0],(uint8)tail[1],(uint8)tail[2],(uint8)tail[3],tail);

   if (ready<=0) { /* parse the error message   */
      char buf[100];

      if (ready==0) {
         DCMSG(RED,"gather_rf:  read returned 0 bytes");
//         sleep(1);
      } else {
         strerror_r(errno,buf,100);
         DDCMSG(D_RF, RED, "gather_rf:  read returned error %s \n", buf);

         if (errno!=EAGAIN){
            DCMSG(RED,"gather_rf:  halting \n");
            exit(-1);
         }
      }
   } else {
      DDCMSG(D_VERY,GREEN,"gather_rf:  new bytes=%2d ",ready);
      return(ready);
   }
   return ready;
}

void DDpacket_internal(const char *program, uint8 *buf,int len){
   uint8 *buff;
   char cmdname[32],hbuf[1000],qbuf[200];
   static int devid_map[2048];
   static int devid_map_init = 0;
   LB_packet_t *LB;
   LB_device_reg_t *LB_devreg;
   LB_assign_addr_t *LB_addr;
   LB_request_new_t *LB_new;

   int i,plen,addr,devid,ptype,pnum,color;

   if (!devid_map_init) {
      memset(devid_map, 0, sizeof(int)*2048);
      devid_map_init = 1;
   }

   buff=buf;    // copy ptr so we can mess with it
   pnum=1;
   // while we have complete packets (and we don't go beyond the end)
   while(((buff-buf)<len)&&(ptype=Ptype(buff))) {

      switch (ptype){
         case 1:
            color=YELLOW;
            break;
         case 2:
            color=BLUE;
            break;
         case 3:
            color=GREEN;
            break;
         case 4:
            color=CYAN;
            break;
         case 6:
            color=GRAY;
            break;
         default:
            color=RED;
      }

      LB=(LB_packet_t *)buff;
      plen=RF_size(LB->cmd);
      addr=RF_size(LB->addr);
      switch (LB->cmd) {
         case LBC_DEVICE_REG:
         {
            LB_device_reg_t *L=(LB_device_reg_t *)LB;
            strcpy(cmdname,"Device_Reg");
            sprintf(hbuf,"devid=%3x devtype=%d exp=%d speed=%d move=%d react:%d loc=%d hm=%d tk=%d sens=%d th=%d fault=%d uptime=%d",L->devid,L->dev_type,L->expose,L->speed,L->move,L->react,L->location,L->hitmode,L->tokill,L->sensitivity,L->timehits,L->fault, L->uptime);
            color=MAGENTA;
         }
         break;

         case LBC_STATUS_REQ:
         {
            LB_status_req_t *L=(LB_status_req_t *)LB;
            strcpy(cmdname,"Status_Req");
            sprintf(hbuf,"RFaddr=%3d devid=%3x",L->addr, devid_map[L->addr]);
         }
         break;

         case LBC_REPORT_ACK:
         {
            LB_report_ack_t *L=(LB_report_ack_t *)LB;
            strcpy(cmdname,"Report_Ack");
            sprintf(hbuf,"RFaddr=%3d   event=%2d hit=%d report=%d",L->addr,L->event,L->hits,L->report);
         }
         break;

         case LBC_EVENT_REPORT:
         {
            LB_event_report_t *L=(LB_event_report_t *)LB;
            strcpy(cmdname,"Event_Report");
            sprintf(hbuf,"RFaddr=%3d   event=%2d hit=%d qualified=%d report=%d",L->addr,L->event,L->hits,L->qualified,L->report);
            color=MAGENTA;
         }
         break;

         case LBC_EXPOSE:
         {
            LB_expose_t *L=(LB_expose_t *)LB;
            strcpy(cmdname,"Expose");
            sprintf(hbuf,"RFaddr=%3d event=%2d .....",L->addr,L->event);
         }
         break;

         case LBC_MOVE:
         {
            LB_move_t *L=(LB_move_t *)LB;
            strcpy(cmdname,"Move");
            sprintf(hbuf,"RFaddr=%3d move=%d speed=%d",L->addr,L->move,L->speed);
         }
            break;

         case LBC_CONFIGURE_HIT:
            strcpy(cmdname,"Configure_Hit");
            sprintf(hbuf,"RFaddr=%3d .....",LB->addr);
            break;

         case LBC_GROUP_CONTROL:
            strcpy(cmdname,"Group_Control");
            sprintf(hbuf,"RFaddr=%3d .....",LB->addr);
            break;

         case LBC_AUDIO_CONTROL:
            strcpy(cmdname,"Audio_Control");
            sprintf(hbuf,"RFaddr=%3d .....",LB->addr);
            break;

         case LBC_QUICK_GROUP: {
            int len, num, addrs[3*14], i;
            LB_quick_group_t *L=(LB_quick_group_t *)LB;
            strcpy(cmdname,"Quick Group");
            num = getItemsQR(L, addrs);
            sprintf(hbuf,"Temp_addr=%i Num=%i", L->temp_addr, num);
            i = -1;
            while ((1000 - (len = strnlen(hbuf,1000))) > 15 && ++i < num) {
               sprintf(hbuf+len, " addr%i=%3d", i, addrs[i]);
            }
         }  break;

         case LBC_QUICK_GROUP_BIG: {
            int len, num, addrs[8*14], i;
            LB_quick_group_big_t *L=(LB_quick_group_big_t *)LB;
            strcpy(cmdname,"Quick Group Big");
            num = getItemsQR(L, addrs);
            sprintf(hbuf,"Temp_addr=%i Num=%i", L->temp_addr, num);
            i = -1;
            while ((1000 - (len = strnlen(hbuf,1000))) > 15 && ++i < num) {
               sprintf(hbuf+len, " addr%i=%3d", i, addrs[i]);
            }
         }  break;

         case LBC_POWER_CONTROL:
            strcpy(cmdname,"Power_Control");
            sprintf(hbuf,"RFaddr=%3d .....",LB->addr);
            break;

         case LBC_PYRO_FIRE:
            strcpy(cmdname,"Pyro_Fire");
            sprintf(hbuf,"RFaddr=%3d .....",LB->addr);
            break;

         case LBC_ACCESSORY: {
            LB_accessory_t *L=(LB_accessory_t*)LB;
            strcpy(cmdname,"Accessory");
            sprintf(hbuf,"RFaddr=%3d on=%d type=%d rdelay=%d idelay=%d",L->addr,L->on,L->type,L->rdelay,L->idelay);
         }  break;

         case LBC_HIT_BLANKING: {
            LB_hit_blanking_t *L=(LB_hit_blanking_t*)LB;
            strcpy(cmdname,"Hit_Blanking");
            sprintf(hbuf,"RFaddr=%3d blanking=%d",L->addr,L->blanking);
         }  break;

         case LBC_STATUS_RESP:
         {
            LB_status_resp_t *L=(LB_status_resp_t *)LB;
            strcpy(cmdname,"Status_Resp_Ext");
            sprintf(hbuf,"RFaddr=%3d exp=%d speed=%d did_exp_cmd=%d move=%d react:%d loc=%d hm=%d tk=%d sens=%d th=%d fault=%d event=%d",L->addr,L->expose,L->speed,L->did_exp_cmd,L->move,L->react,L->location,L->hitmode,L->tokill,L->sensitivity,L->timehits,L->fault,L->event);
            color=MAGENTA;
         }
            break;

         case LBC_QEXPOSE:
            strcpy(cmdname,"Qexpose");
            sprintf(hbuf,"RFaddr=%3d .....",LB->addr);
            break;

         case LBC_RESET:
            strcpy(cmdname,"Reset");
            sprintf(hbuf,"RFaddr=%3d .....",LB->addr);
            break;

         case LBC_BURST:
         {
            LB_burst_t *L=(LB_burst_t *)LB;
            strcpy(cmdname,"Burst");
            sprintf(hbuf,"Number=%3d Sequence=%3d.....",L->number, L->sequence);
         }
         break;

         case LBC_QCONCEAL:
         {
            LB_qconceal_t *L=(LB_qconceal_t *)LB;
            strcpy(cmdname,"Qconceal");
            sprintf(hbuf,"RFaddr=%3d event=%2d uptime=%d dsec",L->addr,L->event,L->uptime);
         }
         break;

         case LB_CONTROL_QUEUE:
         {
            LB_control_queue_t *L=(LB_control_queue_t *)LB;
            strcpy(cmdname,"Control*Queue");
            sprintf(hbuf,"RFaddr=%3d sequence:%i",L->addr,L->sequence);
         }
         break;

         case LB_CONTROL_SENT:
         {
            LB_control_sent_t *L=(LB_control_sent_t *)LB;
            strcpy(cmdname,"Control*Sent");
            sprintf(hbuf,"RFaddr=%3d sequence:%i",L->addr,L->sequence);
         }
         break;

         case LB_CONTROL_REMOVED:
         {
            LB_control_removed_t *L=(LB_control_removed_t *)LB;
            strcpy(cmdname,"Control*Removed");
            sprintf(hbuf,"RFaddr=%3d sequence:%i",L->addr,L->sequence);
         }
         break;

         case LBC_REQUEST_NEW:
         {
            LB_request_new_t *L=(LB_request_new_t *)LB;
            strcpy(cmdname,"Request_New");
            sprintf(hbuf,"lowdev=%3x forget_addr=%8x slottime=%d(%dms)  ",L->low_dev,L->forget_addr,L->slottime,L->slottime*5);
         }
         break;

         case LBC_ASSIGN_ADDR:
         {
            LB_assign_addr_t *L=(LB_assign_addr_t *)LB;
            strcpy(cmdname,"Assign_Addr");
            sprintf(hbuf,"rereg=%d devid=%3x new_addr=%3d",L->reregister,L->devid,L->new_addr);

            // remember this info for later
            devid_map[L->new_addr] = L->devid;
         }
         break;

         default:
            strcpy(cmdname,"UNKNOWN COMMAND");
            sprintf(hbuf," ... ... ... ");

      }

      sprintf(qbuf,"%s %2d: %2i:%s  %s  ", program, pnum, LB->cmd, cmdname, hbuf);
      printf("\x1B[3%d;%dm%s",(color)&7,((color)>>3)&1,qbuf);
      for (i=0; i<plen-1; i++) printf("%02x.", buff[i]);
      printf("%02x\n", buff[plen-1]);
      buff+=plen;
      pnum++;
   }
   printf("\x1B[30;0m");

}


#define false 0
#define true ~false

// utility function to get Device ID (mac address) and return just the 3 unique Action target bytes
uint32 getDevID () {
   struct ifreq ifr;
   struct ifreq *IFR;
   struct ifconf ifc;
   char buf[1024];
   int sock, i;
   u_char addr[6];
   uint32 retval=0;
   // this function only actually finds the mac once, but remembers it
   static int found = false;

   // did we find it before?
   if (!found) {
      // find mac by looking at the network interfaces
      sock = socket(AF_INET, SOCK_DGRAM, 0); // need a valid socket to look at interfaces
      if (sock == -1) {
         PERROR("getDevID-socket() SOCK_DGRAM error");
         return 0;
      }
      // only look at the first ethernet device
      if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, "eth0", 5) == -1) {
         PERROR("getDevID-setsockopt() BINDTO error");
         return 0;
      }

      // grab all interface configs
      ifc.ifc_len = sizeof(buf);
      ifc.ifc_buf = buf;
      ioctl(sock, SIOCGIFCONF, &ifc);

      // find first interface with a valid mac
      IFR = ifc.ifc_req;
      for (i = ifc.ifc_len / sizeof(struct ifreq); --i >= 0; IFR++) {

         strcpy(ifr.ifr_name, IFR->ifr_name);
         if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
            // found an interface ...
            if (!(ifr.ifr_flags & IFF_LOOPBACK)) {
               // and it's not the loopback ...
               if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                  // and it has a mac address
                  found = 1;
                  break;
               }
            }
         }
      }

      // close our socket
      close(sock);

      // did we find one? (this time?)
      if (found) {
         // copy to static address so we don't look again
         memcpy(addr, ifr.ifr_hwaddr.sa_data, 6);
         DCMSG(GREEN,"getDevID:  FOUND MAC: %02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
         retval=(addr[3]<<16)|(addr[4]<<8)|addr[5];
      } else {
         DCMSG(RED,"getDevID:  Mac address not found");
         retval=0xffffff;

      }
   }

   // return whatever we found before (if anything)
   return retval;
}


// based on polynomial x^8 + x^2 + x^1 + x^0

static __uint8_t crc8_table[256] = {
   0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
   0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
   0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
   0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
   0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
   0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
   0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
   0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
   0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
   0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
   0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
   0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
   0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
   0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
   0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
   0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};


// calculates the crc, adds it and the length to the end of the packet
void set_crc8(void *buf) {
   static char hbuf[100];
   char *data = (char*)buf;
   LB_packet_t *LB=(LB_packet_t *)buf;
   int size;            // was size = length-1;
   unsigned char crc = 0; // initial value of 0

   size=RF_size(LB->cmd)-1;

   //    sprintf(hbuf,"crc8: cmd=%d  length=%d\n",LB->cmd,size+1);
   //    DCMSG_HEXB(YELLOW,hbuf,buf,size+1);

   while (size--) {
      crc = crc8_table[(__uint8_t)(crc ^ *data)];
      data++;
   }

   *data++=crc; // add on the CRC we just calculated
   // don't do this: it will mess up the crc we just calculated
   //    *data++=length;        // add the length at the end
   //    *data=0;               // tack a zero after that

   if (verbose&D_CRC){  // saves doing the sprintf's if not wanted
      sprintf(hbuf,"set_crc8: LB len=%d set crc=0x%x  ",RF_size(LB->cmd),crc);
      DDCMSG_HEXB(D_CRC,YELLOW,hbuf,buf,size+1);
   }
}

// just calculates the crc
uint8 crc8(void *cbuf) {
   static char hbuf[100];
   char *data = (char*)cbuf;
   LB_packet_t *LB=(LB_packet_t *)cbuf;
   int size;            // was size = length-1;
   unsigned char crc = 0; // initial value of 0

   size=RF_size(LB->cmd);

//   sprintf(hbuf,"crc8: cmd=%d  length=%d\n",LB->cmd,size);
//   DCMSG_HEXB(YELLOW,hbuf,cbuf,size);


   while (size--) {
      crc = crc8_table[(__uint8_t)(crc ^ *data)];
      data++;
   }

   if (verbose&D_CRC){  // saves doing the sprintf's if not wanted
      if (crc) {
         sprintf(hbuf,"crc8: LB len=%d  BAD CRC=0x%x  ",RF_size(LB->cmd),crc);
         DDCMSG_HEXB(D_CRC,RED,hbuf,cbuf,size+1);
      } else {
         sprintf(hbuf,"crc8: LB len=%d  GOOD CRC=0x%x  ",RF_size(LB->cmd),crc);
         DDCMSG_HEXB(D_CRC,YELLOW,hbuf,cbuf,size+1);
      }
   }
   return crc;
}

// new queue code


// finds the total length of message data in the queue -- future -- to see if we should try collating or not
int queueLength(queue_item_t *qi) {
   int length = 0;
   while (qi != NULL) {
      length += qi->size;
      qi = qi->next;
   }
   return length;
}

// peeks at queue and returns message type
int queuePtype(queue_item_t *qi) {
   if (qi->cmd <= 0 && qi->next != NULL) {
      // qi is head object that contains no data
      return qi->next->ptype;
   } else {
      // qi is an item
      return qi->ptype;
   }
}

// finds the total number items in the queue
int queueSize(queue_item_t *qi) {
   int size = 0;
   while (qi != NULL) {
      if (qi->size > 0) {
         size++;
      }
      qi = qi->next;
   }
   return size;
}

// frees memory, unlinks, fills buf with a "removed" response message (bsize should come filled in with the size of
//   buf, and after return contains size of data in buf)
void removeItem(queue_item_t *qi, char *buf, int *bsize) {
   // link previous item to next item
   if (qi->prev != NULL) {
      // build message
      LB_control_removed_t *lcr = (LB_control_removed_t*)buf;
      if (buf != NULL && bsize != NULL && *bsize >= RF_size(LB_CONTROL_REMOVED) && /* should build message and ... */
          qi->addr != -1 && qi->sequence != -1) { /* has valid response data set */
         DDCMSG(D_POINTER, GRAY, "Creating a \"Removed\" message for c:%i a:%i s:%i", qi->cmd, qi->addr, qi->sequence);
         lcr->cmd = LB_CONTROL_REMOVED;
         lcr->addr = qi->addr;
         lcr->sequence = qi->sequence;
         *bsize = RF_size(lcr->cmd);
         set_crc8(lcr);
      } else if (bsize != NULL) {
         *bsize = 0; // nothing set as was not valid
      }

      // unlink
      if (qi->next != NULL) {
         // stitch previous and next items together
         qi->next->prev = qi->prev; // remove qi from original queue
         qi->prev->next = qi->next; // ...
      } else {
         // qi was end of queue
         qi->prev->next = NULL;
      }

      // free memory
      free(qi->msg);
      free(qi);
   } else {
      // no previous item, that means we're a head item, which means we won't be doing anything here
      if (bsize != NULL) {
         *bsize = 0;
      }
   }
}

// same as removeItem(...), but response message is "sent" (bsize should come filled in with the size of buf,
//   and after return contains size of data in buf)
void sentItem(queue_item_t *qi, char *buf, int *bsize) {
   // link previous item to next item
   if (qi->prev != NULL) {
      // build message
      LB_control_sent_t *lcs = (LB_control_sent_t*)buf;
      if (buf != NULL && bsize != NULL && *bsize >= RF_size(LB_CONTROL_SENT) && /* should build message and ... */
          qi->addr != -1 && qi->sequence != -1) { /* has valid response data set */
         DDCMSG(D_POINTER, GRAY, "Creating a \"Sent\" message for c:%i a:%i s:%i", qi->cmd, qi->addr, qi->sequence);
         lcs->cmd = LB_CONTROL_SENT;
         lcs->addr = qi->addr;
         lcs->sequence = qi->sequence;
         *bsize = RF_size(lcs->cmd);
         set_crc8(lcs);
      } else if (bsize != NULL) {
         *bsize = 0; // nothing set as was not valid
      }

      // unlink
      if (qi->next != NULL) {
         // stitch previous and next items together
         qi->next->prev = qi->prev; // remove qi from original queue
         qi->prev->next = qi->next; // ...
      } else {
         // qi was end of queue
         qi->prev->next = NULL;
      }

      // free memory
      DDCMSG(D_POINTER, BLACK, "About to free msg %p and qi %p", qi->msg, qi);
      free(qi->msg);
      free(qi);
   } else {
      // no previous item, that means we're a head item, which means we won't be doing anything here
      if (bsize != NULL) {
         *bsize = 0;
      }
   }
}

// assuming qi is the head of the queue, this deallocates the memory and unlinks the whole queue, leaving the
//   head unlinked but otherwise intact (no "removed" or "sent" messages are created)
void clearQueue(queue_item_t *qi) {
   while (qi != NULL) {
      queue_item_t *tmp = qi->next;
      removeItem(qi, NULL, NULL);
      qi = tmp;
   }
}

// queue a new RF message on the back of an existing queue
void enQueue(queue_item_t *qi, char *msg, int sequence) {
   queue_item_t *orig = qi;
   LB_packet_t *pkt = (LB_packet_t*)msg; // map header to message
   if (qi != NULL) {
      // create new queue item
      queue_item_t *new;
      new = malloc(sizeof(queue_item_t));
      memset(new, 0, sizeof(queue_item_t));

      // set values
      new->size = RF_size(pkt->cmd);
      new->msg = malloc(new->size);
      memcpy(new->msg, msg, new->size);
      new->cmd = pkt->cmd & 0x1f;
      new->addr = pkt->addr & 0x7ff;
      new->sequence = sequence & 0xffff;
      new->ptype = __ptype(pkt->cmd);

      // link
      qi = queueTail(qi);
      new->prev = qi;
      qi->next = new;
      new->next = NULL;
   }
}

// put the entire "from" queue into the front of the "into" queue
void reQueue(queue_item_t *into, queue_item_t *from, queue_item_t *until) {
   queue_item_t *tail, *ihead, *fhead;
   if (into == NULL || from == NULL) {
      return; // nothing to do if we don't have real queues
   }
   // find head of into
   ihead = into->next;

   // find head of from
   fhead = from->next;

   if (fhead == NULL) {
      return; // putting a queue with no items to the front of the into queue is now done
   }

   // find tail of from (or at least last item before until)
   tail = from;
   while (tail->next != until) {
      tail = tail->next;
   }

   // link into to from head
   into->next = fhead;
   fhead->prev = into;

   // link from tail to into head
   tail->next = ihead;
   if (ihead != NULL) {
      ihead->prev = tail;
   }

   // link from to until
   from->next = until;
   if (until != NULL) {
      until->prev = from;
   }
}

// find the tail of a queue
queue_item_t *queueTail(queue_item_t* qi) {
   queue_item_t *tail = qi;
   while (tail->next != NULL) {
      //DCMSG(YELLOW, "Looking for tail: %p -> %p", tail, tail->next);
      tail = tail->next;
   }
   //DCMSG(YELLOW, "Found tail %p -> %p", tail, tail->next);
   return tail;
}

#define MAX_SLOTS 30
// create a burst message to be sent from the Rx queue. all items put in the burst are put in the Tx queue for
//   manual removal and sending of "sent" messages after actual send. if send is unsuccessful, items can be
//   requeued to Rx by linking Tx's tail to Rx's head, then Rx's head becomes Tx's head. (bsize should come
//   filled in with the size of buf, and after return contains size of data in buf)
int queueBurst(queue_item_t *Rx, queue_item_t *Tx, char *buf, int *bsize, int *remaining_time, int *slottime, int *inittime) {
   int should_end = 0;
   int max_slots = MAX_SLOTS;
   static int last_qgroup = 1000;
   static int last_sequence = 0;
   int should_repeat = 0;
   queue_item_t *qi = Rx->next; // start at head of Rx
   queue_item_t *tail = queueTail(Tx), *temp;
   LB_packet_t *tpkt;
   int bs = *bsize; // remember how much we have left
   *remaining_time = 5; // reset our remaining_time before we are allowed to Tx again   time needs to be smarter
#define QI2BUF(qi, buf) {\
   memcpy(buf, qi->msg, qi->size); \
   buf += qi->size; \
   bs -= qi->size; \
}
#define MOVE_Q(qi, tail) {\
   temp = qi->next; /* the next item we will use */ \
   if (qi->next != NULL) { \
      /* stitch previous and next items together */ \
      qi->next->prev = qi->prev; /* remove qi from original queue */ \
      qi->prev->next = qi->next; /* ... */ \
   } else { \
      /* qi was end of queue */ \
      qi->prev->next = NULL; \
   } \
   tail->next = qi; /* move to tail of new queue */ \
   qi->prev = tail; /* ... */ \
   qi->next = NULL; /* ... */ \
   tail = tail->next; /* move tail */ \
   qi = temp; /* next item in original queue */ \
}
   // do a first pass, just looking for any repeating command messages
   while (bs > 16 && qi != NULL) {
      // is this one of repeating command type?
      if (qi->ptype == 6) {
         // yes, move a single item out of the queue and move on
         should_repeat = 1;
         QI2BUF(qi, buf);
         MOVE_Q(qi, tail);
         // TODO -- quick groups with ptype 6 as actual command
      } else {
         // no, just look at next item
         qi = qi->next;
      }
   }
   if (should_repeat) {
      *bsize -= bs; // bsize will now be how many we used
      last_sequence++;
      if (last_sequence > 15) {
         last_sequence = 1;
      }
      return last_sequence;
   }
   // no repeating commands found, move on to normal message bursting and reset qi and tail
   qi = Rx->next;
   tail = queueTail(Tx);
   while (should_end != 1 && /* we haven't buffered a request-new message */
         qi != NULL && /* we still have more messages in the queue */
         bs > 16) { /* buffer has enough size for another non quick-group message */
      // look at current packet
      switch (qi->ptype) {
         case 0: // illegal
            // move on
            break;

         case 1: { // request new message
            LB_request_new_t *LB_new =(LB_request_new_t*)qi->msg;        // we need to parse out the slottime and inittime from this packet.
            // check if we've used too many slots
            if (max_slots < MAX_SLOTS) { // using any is too much
               // ...and end without queueing this message if we have
               should_end = 1;
               continue;
            }
            // grab time values
            *inittime = LB_new->inittime * 5; // convert to milliseconds
            *slottime = LB_new->slottime * 5; // convert to milliseconds
            // move timers
            *remaining_time += (8 * *slottime); // time for each response: 10 slots = 8 + padding on each end
            // queue
            QI2BUF(qi, buf);
            // slots used
            max_slots -= 8;
         }  break;

         case 2: // status request
            // check if we've already used too many slots
            if (max_slots <= 0) { // we've used all?
               // ...and end without queueing this message if we have
               should_end = 1;
               continue;
            }
            // move timers
            *remaining_time += *slottime; // add time for a response to this message
            // queue
            QI2BUF(qi, buf);
            // slot used
            max_slots--;
            break;

         case 3: // normal
            // queue
            QI2BUF(qi, buf);
            // move timers
            *remaining_time += (*slottime/3); // add 1/3 slottime for target to process command (actual clients add 1/4)
            break;

         case 4: // control message
            // move on -- ends up in Tx queue, but not in buf
            break;

         case 5: // quick group
            should_end = 1; // so we'll end after this whether we add it to the queue or not
            if (qi->next != NULL && bs >= (qi->size + qi->next->size)) {
               // find out how many pseudo messages we're looking at
               LB_quick_group_big_t *lqg = (LB_quick_group_big_t*)qi->msg; // assume big, but big or little work the same
               int num = lqg->number;
               int pkts = 0;
               while (num-- > 0) {
                  pkts += lqg->addresses[num].number;
               }
               //DCMSG(RED, "\n\n----------------------------\nQueue Quick Group (%i %i) %i %i %i\n--------------------------\n", pkts, lqg->number, bs, qi->size, qi->next->size);

               // move timers
               if (qi->next->ptype == 2) {
                  // check if we've used too many slots
                  if (max_slots < MAX_SLOTS) { // we've used any
                     // ...and end without queueing this message if we have
                     continue;
                  }
                  // quick group for a status request
                  *remaining_time += (pkts * *slottime); // add time for a response to each pseudo message
               }

               // queue
               QI2BUF(qi, buf);

               // move on to next item
               MOVE_Q(qi, tail);

               // queue
               QI2BUF(qi, buf);
            } // if we didn't send, the next time a burst is created it will have enough room
            break;

         case 6: // repeating message (could/should this be everything but messages that have responses?)
            // move on -- ends up in Tx queue, but not in buf
            break;
      } 
      // move to back of Tx queue and move on to next item
      MOVE_Q(qi, tail);
   } /* end of queue loop */
   *bsize -= bs; // bsize will now be how many we used
   return 0;
}

// print debug output for the given queue if verbose as bit "v"
void DDqueue_internal(const char *qn, int v, queue_item_t *qi, const char *f, int line, char *fmt, ...) {
   int i = 1;
   char msg[1024];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(msg, 1024, fmt, ap);
   va_end(ap);
   if ((verbose&v)==v) {
      DCOLOR(MAGENTA);
      printf("Q %s @ %s:%i %s: ", qn, f, line, msg);
      do {
         printf("(%i) %p ", i++, qi);
         if (qi == NULL) {
            break;
         } else {
            int color;
            switch (qi->ptype) {
               case 1:
                  color=YELLOW;
                  break;
               case 2:
                  color=BLUE;
                  break;
               case 3:
                  color=GREEN;
                  break;
               case 4:
                  color=CYAN;
                  break;
               default:
                  color=RED;
                  break;
            }

            DCOLOR(color);
            printf("(p:%p, m:%p, s:%i, c:%i, a:%i, sq:%i) ", qi->prev, qi->msg, qi->size, qi->cmd, qi->addr, qi->sequence);
            DCOLOR(MAGENTA);
         }
         if ((i % 3) == 0) {
            // three in a row == enough for this line, start a new one
            printf("->\n");
         } else {
            printf("-> ");
         }
         qi = qi->next;
      } while (qi != NULL);
      printf("(nil)\n");
      DCOLOR(black);
   }
}

// get address from quick report packet (big or small)
// - passed in addrs pointer should have enough space for 8*14 addresses (big) or 3*14 address (normal)
int getItemsQR(void *msg, int *addrs) {
   LB_quick_group_big_t *pkt = (LB_quick_group_big_t*)msg; // assume big, but big or little work the same
   int num = pkt->number;
   int a = 0, i, j;
   for (i = 0; i < num; i++) {
      j = pkt->addresses[i].number;
      if (j-- <= 0) { break; }
      addrs[a++] = pkt->addresses[i].addr1;
      if (j-- <= 0) { break; }
      addrs[a++] = pkt->addresses[i].addr2;
      if (j-- <= 0) { break; }
      addrs[a] = pkt->addresses[i].addr3_1 << 1;
      addrs[a++] |= pkt->addresses[i].addr3_2;
      if (j-- <= 0) { break; }
      addrs[a++] = pkt->addresses[i].addr4;
      if (j-- <= 0) { break; }
      addrs[a++] = pkt->addresses[i].addr5;
      if (j-- <= 0) { break; }
      addrs[a] = pkt->addresses[i].addr6_1 << 2;
      addrs[a++] |= pkt->addresses[i].addr6_2;
      if (j-- <= 0) { break; }
      addrs[a++] = pkt->addresses[i].addr7;
      if (j-- <= 0) { break; }
      addrs[a++] = pkt->addresses[i].addr8;
      if (j-- <= 0) { break; }
      addrs[a] = pkt->addresses[i].addr9_1 << 3;
      addrs[a++] |= pkt->addresses[i].addr9_2;
      if (j-- <= 0) { break; }
      addrs[a++] = pkt->addresses[i].addr10;
      if (j-- <= 0) { break; }
      addrs[a++] = pkt->addresses[i].addr11;
      if (j-- <= 0) { break; }
      addrs[a] = pkt->addresses[i].addr12_1 << 4;
      addrs[a++] |= pkt->addresses[i].addr12_2;
      if (j-- <= 0) { break; }
      addrs[a++] = pkt->addresses[i].addr13;
      if (j-- <= 0) { break; }
      addrs[a++] = pkt->addresses[i].addr14;
   }
   return a;
}

// set address for quick report packet (big or small)
//  - passed in addrs packet pointer should already be allocated and set to the correct big/normal command (will enfore maximum of 3*14 & 8*14 and return however many didn't fit; ideally 0)
int setItemsQR(void *msg, int *addrs, int num) {
   int leftover, a, i, j;
   LB_quick_group_big_t *pkt = (LB_quick_group_big_t*)msg; // use big data structure, but big or little work the same
   if (pkt->cmd == LBC_QUICK_GROUP_BIG) {
      if (num > 112) {
         leftover = num - 112;
         num = 112;
      }
   } else if (pkt->cmd == LBC_QUICK_GROUP) {
      if (num > 42) {
         leftover = num - 42;
         num = 42;
      }
   } else {
      // nothing added
      DDCMSG(D_NEW, RED, "Returning value given %i", num);
      return num;
   }
   if (num % 14 == 0) {
      pkt->number = num / 14; // 14 per chunk (all 100% full)
   } else {
      pkt->number = (num / 14) + 1; // 14 per chunk (all but one 100% full)
   }
   leftover = max(0, leftover); // 
   DDCMSG(D_NEW, RED, "Have %i leftovers, %i number, and %i left to add", leftover, pkt->number, num);

// TODO FOR TUESDAY: fix this to set rather than get addresses
   a = 0;
   for (i = 0; i < pkt->number; i++) {
      DDCMSG(D_NEW, RED, "Resetting j on number %i with %i added", i, a);
      j = 0;
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr1 = addrs[a++];
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr2 = addrs[a++];
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr3_1 = (addrs[a] >> 1);
      pkt->addresses[i].addr3_2 = (addrs[a++] & 0x1);
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr4 = addrs[a++];
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr5 = addrs[a++];
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr6_1 = (addrs[a] >> 2);
      pkt->addresses[i].addr6_2 = (addrs[a++] & 0x3);
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr7 = addrs[a++];
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr8 = addrs[a++];
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr9_1 = (addrs[a] >> 3);
      pkt->addresses[i].addr9_2 = (addrs[a++] & 0x7);
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr10 = addrs[a++];
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr11 = addrs[a++];
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr12_1 = (addrs[a] >> 4);
      pkt->addresses[i].addr12_2 = (addrs[a++] & 0xf);
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr13 = addrs[a++];
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);
      if (a >= num) { goto set_num; }
      pkt->addresses[i].addr14 = addrs[a++];
      j++;
      DDCMSG(D_NEW, RED, "Added addr%i=%i to addresses %i", j, addrs[a-1], i);

      set_num:
      DDCMSG(D_NEW, RED, "j is %i, a is %i, i is %i", j, a, i);
      pkt->addresses[i].number = j;
   }
   return leftover;
}

