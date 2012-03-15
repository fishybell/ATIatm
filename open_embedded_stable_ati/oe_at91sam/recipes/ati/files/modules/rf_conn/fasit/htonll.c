#include "mcp.h"

// for some reason we have a ntohs/htons, but no ntohf/htonf
float ntohf(float f) {
   __uint32_t holder = *(__uint32_t*)(&f), after;
   // byte starts as 1, 2, 3, 4, ends as 4, 3, 2, 1
   after = ((holder & 0x000000ff) << 24) | \
           ((holder & 0x0000ff00) << 8) | \
           ((holder & 0x00ff0000) >> 8) | \
           ((holder & 0xff000000) >> 24);

   return *(float*)(&after);
}

uint64 htonll( uint64 id){
    unsigned char *bytes,temp;

    bytes=(unsigned char *)&id;

    temp=bytes[0];
    bytes[0]=bytes[7];
    bytes[7]=temp;

    temp=bytes[1];
    bytes[1]=bytes[6];
    bytes[6]=temp;

    temp=bytes[2];
    bytes[2]=bytes[5];
    bytes[5]=temp;

    temp=bytes[3];
    bytes[3]=bytes[4];
    bytes[4]=temp;

    return(id);
}

