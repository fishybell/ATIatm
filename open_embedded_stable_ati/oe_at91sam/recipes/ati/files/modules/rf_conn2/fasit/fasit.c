#include "mcp.h"
#include "fasit_c.h"


int FASIT_size(int cmd){
   // set LB_size  based on which command it is
   switch (cmd){
      case 100:
         return sizeof(FASIT_header);
         
      case 2000:
         return sizeof(FASIT_2000);
         
      case 2004:
         return sizeof(FASIT_2004);
         
      case 2005:
         return sizeof(FASIT_2005);
         
      case 2006:
         return sizeof(FASIT_2006);
         
      case 2100:
         return sizeof(FASIT_2100);
         
      case 2101:
         return sizeof(FASIT_2101);
         
      case 2111:
         return sizeof(FASIT_2111);
         
      case 2102:
         return sizeof(FASIT_2102);
         
      case 2114:
         return sizeof(FASIT_2114);
         
      case 2115:
         return sizeof(FASIT_2115);
         
      case 2110:
         return sizeof(FASIT_2110);
         
      case 2112:
         return sizeof(FASIT_2112);
         
      case 2113:
         return sizeof(FASIT_2113);
         
      case 13110:
         return sizeof(FASIT_13110);
         
      case 13112:
         return sizeof(FASIT_13112);
         
      case 14110:
         return sizeof(FASIT_14110);
         
      case 14112:
         return sizeof(FASIT_14112);
         
      case 14200:
         return sizeof(FASIT_14200);
         
      case 14400:
         return sizeof(FASIT_14400);
         
      case 14401:
         return sizeof(FASIT_14401);
         
      default:
         return -1;
    }
}

