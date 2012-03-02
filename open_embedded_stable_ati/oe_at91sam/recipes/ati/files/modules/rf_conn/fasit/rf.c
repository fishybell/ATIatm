#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <linux/if.h>

#include "mcp.h"
#include "rf.h"
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <netinet/tcp.h>

extern int verbose;


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

// add the 4 bytes of the sequence number to the tail of the queue
void EnQueueSeq(queue_t *M, int seq){
    int count=4;
    char *data=(char *)&seq;
    while(count--) *M->tail++=*data++ ;
}

// peek at the sequence number at the head of the queue.
// return a 0 if the queue is empty
int QueueSeq_peek(queue_t *M){
    int count=4;
    int seq;

    if (M->tail==M->head) return(0);

    seq=*((int *)M->head);
    return(seq);

}

// remove count bytes from the head of the queue and normalize the queue
void DeQueue(queue_t *M,int count){
    memmove(M->buf,M->head+count,Queue_Depth(M));
    M->tail-=count;
    M->head=M->buf;
}

// move count from the front of queue Msrc to tail of Mdst
//   and shift up the old queue for now
// having bounds checking and stuff might be nice during debugging
void ReQueue(queue_t *Mdst,queue_t *Msrc,int count){
    memcpy(Mdst->tail,Msrc->head,count);	// copy count bytes
    Mdst->tail+=count;				// increment the tail
    DeQueue(Msrc,count);	// and remove from src queue 
}

//  this is a macro
//int Queue_Depth(queue_t *M){
//    M->tail-M->head;
//}


void print_verbosity(void){
    printf("  -v xx         set verbosity bits.   Examples:\n");
    printf("  -v 1	    sets D_PACKET  for packet info\n");
    printf("  -v 2	    sets D_RF    \n");
    printf("  -v 4	    sets D_CRC   \n");
    printf("  -v 8	    sets D_POLL  \n");
    printf("  -v 10	    sets D_TIME  \n");
    printf("  -v 20	    sets D_VERY  \n");
    printf("  -v 80	    sets D_MEGA  \n");
    printf("  -v 3F	    sets all of the above  \n");
}

int RF_size(int cmd){
    // set LB_size  based on which command it is
    switch (cmd){
	case  LBC_STATUS_REQ:
	    return (3);
	    
	case  LBC_STATUS_RESP_LIFTER:
	    return (4);
	    
	case  LBC_STATUS_RESP_MOVER:
	    return (8);
	    
	case  LBC_STATUS_RESP_EXT:
	    return (11);
	    
	case  LBC_STATUS_NO_RESP:
	    return (3);
	    
	case  LBC_EXPOSE:
	    return (5);
	    
	case  LBC_MOVE:
	    return (5);
	    
	case  LBC_CONFIGURE_HIT:
	    return (6);
	    
	case  LBC_GROUP_CONTROL:
	    return (5);
	    
	case  LBC_AUDIO_CONTROL:
	    return (6);
	    
	case  LBC_POWER_CONTROL:
	    return (4);

	case  LBC_PYRO_FIRE:
	    return (4);

	case  LBC_QEXPOSE:
	    return (3);
	    
	case  LBC_QCONCEAL:
	    return (3);
	    
	case  LBC_DEVICE_REG:
	    return (6);
	    
	case  LBC_REQUEST_NEW:
	    return (9);
	    
	case  LBC_ASSIGN_ADDR:
	    return (7);

	default:
	    return (1);
    }
}

//   and in fact, there needs to be more error checking, and throwing away of bad checksum packets
//   not sure how to re-sync after a garbage packet - probably have to zero it out one byte at a time.

int gather_rf(int fd, char *tail, char *head,int max){
    int ready;

    /* read as much as we can or max from the non-blocking fd. */
    ready=read(fd,tail,max);

    if (ready<=0) { /* parse the error message   */
	char buf[100];

	strerror_r(errno,buf,100);
	DCMSG(RED,"gather_rf:  read returned error %s \n",buf);

	if (errno!=EAGAIN){
	    DCMSG(RED,"gather_rf:  halting \n");
	    exit(-1);
	}
    } else {
	tail+=ready;	// increment the position pointer
	DDCMSG(D_VERY,GREEN,"gather_rf:  new bytes=%2d new total=%2d ",ready,tail-head);
	return(tail-head);
    }
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
	    perror("getDevID-socket() SOCK_DGRAM error");
	    return 0;
	}
      // only look at the first ethernet device
	if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, "eth0", 5) == -1) {
	    perror("getDevID-setsockopt() BINDTO error");
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
    int size;		// was size = length-1;
    unsigned char crc = 0; // initial value of 0

    size=RF_size(LB->cmd)-1;
    
//    sprintf(hbuf,"crc8: cmd=%d  length=%d\n",LB->cmd,size+1);
//    DCMSG_HEXB(YELLOW,hbuf,buf,size+1);
    
    while (size--) {
	crc = crc8_table[(__uint8_t)(crc ^ *data)];
	data++;
    }

    *data++=crc;	// add on the CRC we just calculated
// don't do this: it will mess up the crc we just calculated
//    *data++=length;	// add the length at the end
//    *data=0;		// tack a zero after that

    if (verbose&D_CRC){	// saves doing the sprintf's if not wanted
	sprintf(hbuf,"set_crc8: LB len=%d set crc=0x%x  ",RF_size(LB->cmd),crc);
	DCMSG_HEXB(YELLOW,hbuf,buf,size+1);
    }
}

// just calculates the crc
uint8 crc8(void *buf) {
    static char hbuf[100];
    char *data = (char*)buf;
    LB_packet_t *LB=(LB_packet_t *)buf;
    int size;		// was size = length-1;
    unsigned char crc = 0; // initial value of 0

    size=RF_size(LB->cmd);

//    sprintf(hbuf,"crc8: cmd=%d  length=%d\n",LB->cmd,size);
//    DCMSG_HEXB(YELLOW,hbuf,buf,size);

    
    while (size--) {
	crc = crc8_table[(__uint8_t)(crc ^ *data)];
	data++;
    }

    if (verbose&D_CRC){	// saves doing the sprintf's if not wanted
	if (crc) {
	    sprintf(hbuf,"crc8: LB len=%d  BAD CRC=0x%x  ",RF_size(LB->cmd),crc);
	    DCMSG_HEXB(RED,hbuf,buf,size+1);
	} else {
	    sprintf(hbuf,"crc8: LB len=%d  GOOD CRC=0x%x  ",RF_size(LB->cmd),crc);
	    DCMSG_HEXB(YELLOW,hbuf,buf,size+1);
	}
    }
    return crc;
}

