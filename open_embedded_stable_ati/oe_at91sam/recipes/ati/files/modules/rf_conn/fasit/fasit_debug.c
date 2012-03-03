#include "fasit_debug.h"
#include "mcp.h"

// debug an FASIT packet
void debugFASIT(int color, char *packet) {
   FASIT_header *hdr = (FASIT_header*)(packet);
   DCMSG_HEXB(color, "Raw FASIT msg: ", packet, htons(hdr->length));
   DCMSG(color, "HEADER\nM-Num | ICD-v | seq-# | rsrvd | length\n %6d  %d.%d  %6d  %6d  %7d", htons(hdr->num), htons(hdr->icd1), htons(hdr->icd2), htonl(hdr->seq), htonl(hdr->rsrvd), htons(hdr->length));

   switch (htons(hdr->num)) {
      case 100:
      case 2006:
      default:
         DCMSG(color, "Debugging header only...");
         break;
      case 2000:
         debug_2000(color, packet + sizeof(FASIT_header));
         break; 
      case 2004:
         debug_2004(color, packet + sizeof(FASIT_header));
         break; 
      case 2005:
         debug_2005(color, packet + sizeof(FASIT_header));
         break; 
      case 2100:
         debug_2100(color, packet + sizeof(FASIT_header));
         break; 
      case 2101:
         debug_2101(color, packet + sizeof(FASIT_header));
         break; 
      case 2102:
         debug_2102(color, packet + sizeof(FASIT_header));
         break; 
      case 2110:
         debug_2110(color, packet + sizeof(FASIT_header));
         break; 
      case 2111:
         debug_2111(color, packet + sizeof(FASIT_header));
         break; 
      case 2112:
         debug_2112(color, packet + sizeof(FASIT_header));
         break; 
      case 2113:
         debug_2113(color, packet + sizeof(FASIT_header));
         break; 
      case 2114:
         debug_2114(color, packet + sizeof(FASIT_header));
         break; 
      case 2115:
         debug_2115(color, packet + sizeof(FASIT_header));
         break; 
      case 13110:
         debug_13110(color, packet + sizeof(FASIT_header));
         break; 
      case 13112:
         debug_13112(color, packet + sizeof(FASIT_header));
         break; 
      case 14110:
         debug_14110(color, packet + sizeof(FASIT_header));
         break; 
      case 14112:
         debug_14112(color, packet + sizeof(FASIT_header));
         break; 
      case 14200:
         debug_14200(color, packet + sizeof(FASIT_header));
         break; 
      case 14400:
         debug_14400(color, packet + sizeof(FASIT_header));
         break; 
      case 14401:
         debug_14401(color, packet + sizeof(FASIT_header));
         break; 
   }

}

// individual message debuggers
void debug_2000(int color, char *packet) {
   FASIT_2000 *msg = (FASIT_2000*)(packet);
   DCMSG(color, "cid: %i, zone: %i", msg->cid, htons(msg->zone));
}

void debug_2004(int color, char *packet) {
   FASIT_2004 *msg = (FASIT_2004*)(packet);
   DCMSG(color, "Response: %i:%i", msg->response.resp_num, msg->response.resp_seq);
   DCMSG(color, "resp: %c", msg->body.resp);
}

void debug_2005(int color, char *packet) {
   FASIT_2005 *msg = (FASIT_2005*)(packet);
   DCMSG(color, "Response: %i:%i", msg->response.resp_num, msg->response.resp_seq);
   DCMSG(color, "devid: %8llx, flags: %2x", msg->body.devid, msg->body.flags);
}


void debug_2100(int color, char *packet) {
   FASIT_2100 *msg = (FASIT_2100*)(packet);
   DCMSG(color, "\t\t\t\t\t\t\tmessage body\n"\
      "C-ID | Expos | Aspct |  Dir | Move |  Speed | On/Off | Hits | React | ToKill | Sens | Mode | Burst\n"\
          "%3d    %3d     %3d     %2d    %3d    %7.2f     %4d     %2d     %3d     %3d     %3d    %3d   %5d ",
      msg->cid, msg->exp, msg->asp, msg->dir, msg->move, msg->speed, msg->on, htons(msg->hit), msg->react, htons(msg->tokill), htons(msg->sens), msg->mode, htons(msg->burst));
}

void debug_2101(int color, char *packet) {
   FASIT_2101 *msg = (FASIT_2101*)(packet);
   DCMSG(color, "Response: %i:%i", msg->response.resp_num, msg->response.resp_seq);
   DCMSG(color, "resp: %c", msg->body.resp);
}

void debug_2102(int color, char *packet) {
   FASIT_2102 *msg = (FASIT_2102*)(packet);
   DCMSG(color, "Response: %i:%i", msg->response.resp_num, msg->response.resp_seq);
   DCMSG(color, "\t\t\t\t\t\t\tmessage body\n "\
         "PSTAT | Fault | Expos | Aspct |  Dir | Move |  Speed  | POS | Type | Hits | On/Off | React | ToKill | Sens | Mode | Burst\n"\
         "  %3d    %3d     %3d     %3d     %3d    %3d    %6.2f    %3d   %3d    %3d      %3d     %3d      %3d     %3d    %3d    %3d ",
         msg->body.pstatus, msg->body.fault, msg->body.exp, msg->body.asp, msg->body.dir, msg->body.move, msg->body.speed, msg->body.pos, msg->body.type, htons(msg->body.hit),
         msg->body.hit_conf.on, msg->body.hit_conf.react, htons(msg->body.hit_conf.tokill), htons(msg->body.hit_conf.sens), msg->body.hit_conf.mode, htons(msg->body.hit_conf.burst));
}

void debug_2110(int color, char *packet) {
   FASIT_2110 *msg = (FASIT_2110*)(packet);
   DCMSG(color, "\t\t\t\t\t\t\tmessage body\nOn/Off | Mode | I-Delay | R-Delay\n%7d  %5d  %8d  %8d",
         msg->on, msg->mode, msg->idelay, msg->rdelay);

}

void debug_2111(int color, char *packet) {
   FASIT_2111 *msg = (FASIT_2111*)(packet);
   DCMSG(color, "Response: %i:%i", msg->response.resp_num, msg->response.resp_seq);
   DCMSG(color, "\t\t\t\t\t\t\tmessage body\n Device ID (mac address backwards) | flag_bits == GPS=4, Muzzle Flash=2, MILES Shootback=1");
   DCMSG(color, "0x%8.8llx           0x%2x", msg->body.devid, msg->body.flags);
}

void debug_2112(int color, char *packet) {
   FASIT_2112 *msg = (FASIT_2112*)(packet);
   DCMSG(color, "Response: %i:%i", msg->response.resp_num, msg->response.resp_seq);
   DCMSG(color, "\t\t\t\t\t\t\tmessage body\nOn/Off | Mode | I-Delay | R-Delay\n%7d  %5d  %8d  %8d",
         msg->body.on, msg->body.mode, msg->body.idelay, msg->body.rdelay);
}

void debug_2113(int color, char *packet) {
   FASIT_2113 *msg = (FASIT_2113*)(packet);
   DCMSG(color, "Response: %i:%i", msg->response.resp_num, msg->response.resp_seq);
   DCMSG(color, "fom: %i, ilat: %i, flat: %i, ilon: %i, flon: %i", msg->body.fom, htons(msg->body.ilat), htonl(msg->body.flat), htons(msg->body.ilon), htonl(msg->body.flon));
}

void debug_2114(int color, char *packet) {
   FASIT_2114 *msg = (FASIT_2114*)(packet);
   DCMSG(color, "code: %i, ammo: %i, player: %i, delay: %i", msg->code, msg->ammo, htons(msg->player), msg->delay);
}

void debug_2115(int color, char *packet) {
   FASIT_2115 *msg = (FASIT_2115*)(packet);
   DCMSG(color, "Response: %i:%i", msg->response.resp_num, msg->response.resp_seq);
   DCMSG(color, "code: %i, ammo: %i, player: %i, delay: %i", msg->body.code, msg->body.ammo, htons(msg->body.player), msg->body.delay);
}

void debug_13110(int color, char *packet) {
   FASIT_13110 *msg = (FASIT_13110*)(packet);
   DCMSG(color, "\t\t\t\t\t\t\tmessage body\nOn/Off \n%7d",
         msg->on);
}

void debug_13112(int color, char *packet) {
   FASIT_13112 *msg = (FASIT_13112*)(packet);
   DCMSG(color, "Response: %i:%i", msg->response.resp_num, msg->response.resp_seq);
   DCMSG(color, "\t\t\t\t\t\t\tmessage body\nOn/Off \n%7d",
         msg->body.on);
}

void debug_14110(int color, char *packet) {
   FASIT_14110 *msg = (FASIT_14110*)(packet);
   DCMSG(color, "\t\t\t\t\t\t\tmessage body\nOn/Off \n%7d",
         msg->on);
}

void debug_14112(int color, char *packet) {
   FASIT_14112 *msg = (FASIT_14112*)(packet);
   DCMSG(color, "Response: %i:%i", msg->response.resp_num, msg->response.resp_seq);
   DCMSG(color, "\t\t\t\t\t\t\tmessage body\nOn/Off \n%7d",
         msg->body.on);
}

void debug_14200(int color, char *packet) {
   FASIT_14200 *msg = (FASIT_14200*)(packet);
   DCMSG(color, "\t\t\t\t\t\t\tmessage body\nBlank \n%7d",
         htons(msg->blank));
}

void debug_14400(int color, char *packet) {
   FASIT_14400 *msg = (FASIT_14400*)(packet);
   DCMSG(color, "\t\t\t\t\t\t\tmessage body\n"\
         "C-ID | Length | Data\n"\
         "%3d    %5d   %s",
         msg->cid, htons(msg->length), msg->cid==SES_Copy_Chunk?"<binary data>":(const char*)msg->data);
}

void debug_14401(int color, char *packet) {
   FASIT_14401 *msg = (FASIT_14401*)(packet);
   DCMSG(color, "Response: %i:%i", msg->response.resp_num, msg->response.resp_seq);
   DCMSG(color, "mode: %i, status: %i, track: %i", msg->body.mode, msg->body.status, msg->body.track);
}


