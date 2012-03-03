#ifndef __FASIT_DEBUG_H__
#define __FASIT_DEBUG_H__

#include "fasit_c.h"

// debug individual FASIT packets
void debug_2000(int color, char *packet);
void debug_2004(int color, char *packet);
void debug_2005(int color, char *packet);
void debug_2100(int color, char *packet);
void debug_2101(int color, char *packet);
void debug_2102(int color, char *packet);
void debug_2110(int color, char *packet);
void debug_2111(int color, char *packet);
void debug_2112(int color, char *packet);
void debug_2113(int color, char *packet);
void debug_2114(int color, char *packet);
void debug_2115(int color, char *packet);
void debug_13110(int color, char *packet);
void debug_13112(int color, char *packet);
void debug_14110(int color, char *packet);
void debug_14112(int color, char *packet);
void debug_14200(int color, char *packet);
void debug_14400(int color, char *packet);
void debug_14401(int color, char *packet);
 
// debug generic FASIT packet
void debugFASIT(int color, char *packet);

#endif

