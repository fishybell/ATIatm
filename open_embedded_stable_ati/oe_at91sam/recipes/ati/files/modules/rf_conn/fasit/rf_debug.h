#ifndef __RF_DEBUG_H__
#define __RF_DEBUG_H__

#include "rf.h"

// debug individual RF packets
void debug_STATUS_REQ(int color, LB_packet_t *pkt);
void debug_EXPOSE(int color, LB_packet_t *pkt);
void debug_MOVE(int color, LB_packet_t *pkt);
void debug_CONFIGURE_HIT(int color, LB_packet_t *pkt);
void debug_GROUP_CONTROL(int color, LB_packet_t *pkt);
void debug_AUDIO_CONTROL(int color, LB_packet_t *pkt);
void debug_POWER_CONTROL(int color, LB_packet_t *pkt);
void debug_PYRO_FIRE(int color, LB_packet_t *pkt);
void debug_STATUS_RESP_LIFTER(int color, LB_packet_t *pkt);
void debug_STATUS_RESP_MOVER(int color, LB_packet_t *pkt);
void debug_STATUS_RESP_EXT(int color, LB_packet_t *pkt);
void debug_STATUS_NO_RESP(int color, LB_packet_t *pkt);
void debug_QEXPOSE(int color, LB_packet_t *pkt);
void debug_QCONCEAL(int color, LB_packet_t *pkt);
void debug_DEVICE_REG(int color, LB_packet_t *pkt);
void debug_REQUEST_NEW(int color, LB_packet_t *pkt);
void debug_ASSIGN_ADDR(int color, LB_packet_t *pkt);

// debug generic RF packet
void debugRF(int color, char *packet, int len);

#endif

