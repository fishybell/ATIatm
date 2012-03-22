#include "rf_debug.h"
#include "RFslave.h"

void debug_STATUS_REQ(int color, LB_packet_t *pkt) {
   LB_status_req_t *p = (LB_status_req_t*)pkt;
   DCMSG(color, "LBC_STATUS_REQ: cmd: %i, addr: %i, crc: %02X", p->cmd, p->addr, p->crc);
}

void debug_EXPOSE(int color, LB_packet_t *pkt) {
   LB_expose_t *p = (LB_expose_t*)pkt;
   DCMSG(color, "LBC_EXPOSE: cmd: %i, addr: %i, event: %i, expose: %i, hitmode: %i, tokill: %i, react: %i, mfs: %i, thermal: %i, crc: %02X", p->cmd, p->addr, p->event, p->expose, p->hitmode, p->tokill, p->react, p->mfs, p->thermal, p->crc);
}

void debug_MOVE(int color, LB_packet_t *pkt) {
   LB_move_t *p = (LB_move_t*)pkt;
   DCMSG(color, "LBC_MOVE: cmd: %i, addr: %i, direction: %i, speed: %i, crc: %02X", p->cmd, p->addr, p->direction, p->speed, p->crc);
}

void debug_CONFIGURE_HIT(int color, LB_packet_t *pkt) {
   LB_configure_t *p = (LB_configure_t*)pkt;
   DCMSG(color, "LBC_CONFIGURE_HIT: cmd: %i, addr: %i, hitmode: %i, tokill: %i, react: %i, sensitivity: %i, timehits: %i, hitcountset: %i, crc: %02X", p->cmd, p->addr, p->hitmode, p->tokill, p->react, p->sensitivity, p->timehits, p->hitcountset, p->crc);
}


void debug_GROUP_CONTROL(int color, LB_packet_t *pkt) {
   LB_group_control_t *p = (LB_group_control_t*)pkt;
   DCMSG(color, "LBC_GROUP_CONTROL: cmd: %i, addr: %i, gcmd: %i, gaddr: %i, crc: %02X", p->cmd, p->addr, p->gcmd, p->gaddr, p->crc);
}

void debug_AUDIO_CONTROL(int color, LB_packet_t *pkt) {
   LB_audio_control_t *p = (LB_audio_control_t*)pkt;
   DCMSG(color, "LBC_AUDIO_CONTROL: cmd: %i, addr: %i, function: %i, volume: %i playmode: %i, track: %i, crc: %02X", p->cmd, p->addr, p->function, p->volume, p->playmode, p->track, p->crc);
}

void debug_POWER_CONTROL(int color, LB_packet_t *pkt) {
   LB_power_control_t *p = (LB_power_control_t*)pkt;
   DCMSG(color, "LBC_POWER_CONTROL: cmd: %i, addr: %i, pcmd: %i, crc: %02X", p->cmd, p->addr, p->pcmd, p->crc);
}

void debug_PYRO_FIRE(int color, LB_packet_t *pkt) {
   LB_pyro_fire_t *p = (LB_pyro_fire_t*)pkt;
   DCMSG(color, "LBC_PYRO_FIRE: cmd: %i, addr: %i, zone: %i, crc: %02X", p->cmd, p->addr, p->zone, p->crc);
}

void debug_STATUS_RESP_LIFTER(int color, LB_packet_t *pkt) {
   LB_status_resp_lifter_t *p = (LB_status_resp_lifter_t *)pkt;
   DCMSG(color, "LBC_STATUS_RESP_LIFTER: cmd: %i, addr: %i, hits: %i, expose: %i, crc: %02X", p->cmd, p->addr, p->hits, p->expose, p->crc);
}

void debug_STATUS_RESP_MOVER(int color, LB_packet_t *pkt) {
   LB_status_resp_mover_t *p = (LB_status_resp_mover_t*)pkt;
   DCMSG(color, "LBC_STATUS_RESP_MOVER: cmd: %i, addr: %i, hits: %i, expose: %i, speed: %i, dir: %i, location: %i, crc: %02X", p->cmd, p->addr, p->hits, p->expose, p->speed, p->dir, p->location, p->crc);
}

void debug_STATUS_RESP_EXT(int color, LB_packet_t *pkt) {
   LB_status_resp_ext_t *p = (LB_status_resp_ext_t*)pkt;
   DCMSG(color, "LBC_STATUS_RESP_EXT: cmd: %i, addr: %i, hits: %i, expose: %i, speed: %i, dir: %i, react: %i, location: %i, hitmode: %i, tokill: %i, sensitivity: %i, timehits: %i, fault: %i, crc: %02X", p->cmd, p->addr, p->hits, p->expose, p->speed, p->dir, p->react, p->location, p->hitmode, p->tokill, p->sensitivity, p->timehits, p->fault, p->crc);
}

void debug_STATUS_NO_RESP(int color, LB_packet_t *pkt) {
   LB_status_no_resp_t *p = (LB_status_no_resp_t*)pkt;
   DCMSG(color, "LBC_STATUS_NO_RESP: cmd: %i, addr: %i, crc: %02X", p->cmd, p->addr, p->crc);
}

void debug_QEXPOSE(int color, LB_packet_t *pkt) {
   LB_status_req_t *p = (LB_status_req_t*)pkt;
   DCMSG(color, "LBC_QEXPOSE: cmd: %i, addr: %i, crc: %02X", p->cmd, p->addr, p->crc);
}

void debug_QCONCEAL(int color, LB_packet_t *pkt) {
   LB_qconceal_t *p = (LB_qconceal_t*)pkt;
   DCMSG(color, "LBC_QCONCEAL: cmd: %i, addr: %im event: %i, uptime: %i, crc: %02X", p->cmd, p->addr, p->event, p->uptime, p->crc);
}

void debug_DEVICE_REG(int color, LB_packet_t *pkt) {
   LB_device_reg_t *p = (LB_device_reg_t*)pkt;
   DCMSG(color, "LBC_DEVICE_REG: cmd: %i, dev_type: %x, devid: %x, crc: %02X", p->cmd, p->dev_type, p->devid, p->crc);
}

void debug_REQUEST_NEW(int color, LB_packet_t *pkt) {
   LB_request_new_t *p = (LB_request_new_t*)pkt;
   DCMSG(color, "LBC_REQUEST_NEW: cmd: %i, forget_addr: %8x, low_dev: %x, slottime: %i, crc: %02X", p->cmd, p->forget_addr, p->low_dev, p->slottime, p->crc);
}

void debug_REPORT_REQ(int color, LB_packet_t *pkt) {
   LB_report_req_t *p = (LB_report_req_t*)pkt;
   DCMSG(color, "LBC_REPORT_REQ: cmd: %i, addr: %i, event: %i, crc: %02X", p->cmd, p->addr, p->event, p->crc);
}

void debug_EVENT_REPORT(int color, LB_packet_t *pkt) {
   LB_event_report_t *p = (LB_event_report_t*)pkt;
   DCMSG(color, "LBC_EVENT_REPORT: cmd: %i, addr: %i, event: %i, hits: %i, crc: %02X", p->cmd, p->addr, p->event, p->hits, p->crc);
}

void debug_ASSIGN_ADDR(int color, LB_packet_t *pkt) {
   LB_assign_addr_t *p = (LB_assign_addr_t*)pkt;
   DCMSG(color, "LBC_ASSIGN_ADDR: cmd: %i, reregister=%i, devid =%x, new_addr: %i, crc: %02X", p->cmd, p->reregister, p->devid, p->new_addr, p->crc);
}

// debug an RF packet
void debugRF(int color, char *packet, int len) {
   int start = 0;
   if (!(verbose & D_MEGA)) { return; }
   while (start < len) {
      LB_packet_t *pkt = (LB_packet_t*)(packet+start);
      DCMSG(color, "RF msg buffer size: %i", len);
      DCMSG_HEXB(color, "Raw RF msg: ", packet+start, RF_size(pkt->cmd));
//      usleep(500000);
      switch (pkt->cmd) {
         case LBC_STATUS_REQ:
            debug_STATUS_REQ(color,pkt); break;
         case LBC_EXPOSE:
            debug_EXPOSE(color,pkt); break;
         case LBC_MOVE:
            debug_MOVE(color,pkt); break;
         case LBC_CONFIGURE_HIT:
            debug_CONFIGURE_HIT(color,pkt); break;
         case LBC_GROUP_CONTROL:
            debug_GROUP_CONTROL(color,pkt); break;
         case LBC_AUDIO_CONTROL:
            debug_AUDIO_CONTROL(color,pkt); break;
         case LBC_POWER_CONTROL:
            debug_POWER_CONTROL(color,pkt); break;
         case LBC_PYRO_FIRE:
            debug_PYRO_FIRE(color,pkt); break;
         case LBC_STATUS_RESP_LIFTER:
            debug_STATUS_RESP_LIFTER(color,pkt); break;
         case LBC_STATUS_RESP_MOVER:
            debug_STATUS_RESP_MOVER(color,pkt); break;
         case LBC_STATUS_RESP_EXT:
            debug_STATUS_RESP_EXT(color,pkt); break;
         case LBC_STATUS_NO_RESP:
            debug_STATUS_NO_RESP(color,pkt); break;
         case LBC_QEXPOSE:
            debug_QEXPOSE(color,pkt); break;
         case LBC_QCONCEAL:
            debug_QCONCEAL(color,pkt); break;
         case LBC_DEVICE_REG:
            debug_DEVICE_REG(color,pkt); break;
         case LBC_REQUEST_NEW:
            debug_REQUEST_NEW(color,pkt); break;
         case LBC_REPORT_REQ:
            debug_REPORT_REQ(color,pkt); break;
         case LBC_EVENT_REPORT:
            debug_EVENT_REPORT(color,pkt); break;
         case LBC_ASSIGN_ADDR:
            debug_ASSIGN_ADDR(color,pkt); break;
      }
      start += max(1,RF_size(pkt->cmd));
   }
}


