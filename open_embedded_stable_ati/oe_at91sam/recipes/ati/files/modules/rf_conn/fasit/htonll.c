#include "mcp.h"

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

