#include "mcp.h"
#include "rf.h"


// tcp port we'll listen to for new connections
#define defaultPORT 4004

// size of client buffer
#define CLIENT_BUFFER 1024

void DieWithError(char *errorMessage){
    char buf[200];
    strerror_r(errno,buf,200);
    DCMSG(RED,"RFslave %s %s \n", errorMessage,buf);
    exit(1);
}

/*************
 *************  HandleRF
 *************
 *************  We have a socket to the MCP,  and Serial connection to the RF
 *************
 *************  Just pass messages between the two...
 *************  
 *************  if we have a socket to an MCP, we act as a driver/link to the RF modem
 *************  through our serial port.  for now the RFslave will only forward comms in
 *************  both directions - TCP connection to RF, and RF to TCP.
 *************    At some point it might be best if the RFslave does some data massaging,
 *************  but until that is clear to me what it would need to do, for now we just pass comms
 *************
 *************/

void HandleRF(int RFfd){
#define MbufSize 4096
    char Mbuf[MbufSize], buf[200], hbuf[200];        /* Buffer MCP socket */
    int MsgSize,result,sock_ready,pcount=0;                    /* Size of received message */
    char *Mptr, Mstart, Mend; 
    fd_set rf_or_mcp;
    struct timeval timeout;
    LB_packet_t *LB,rLB;
    int len,addr,cmd,RF_addr;

    LB_device_reg_t *LB_devreg;
    LB_device_addr_t *LB_addr;
    LB_expose_t *LB_exp;

    RF_addr=2047;	//  only respond to address 2047 for the request new device packet


    
/**   loop until we lose connection  **/
    while(1){
	/* Receive only the first two bytes of the message from RF */
	//  fail!  stupidly reading a byte at a time to see it work
	MsgSize = read(RFfd,Mbuf,2);
	if (MsgSize==1) MsgSize = 1+read(RFfd,&Mbuf[1],1);
	
	LB=(LB_packet_t *)Mbuf;
	for (Mstart=2, Mstart<=RF_size(LB->cmd),Mstart++)
	    read(RFfd,&Mbuf[Mstart],RF_size(LB->cmd)-2);
	

	// of course we might be getting packets that are either split up or glommed together,
	// and we need to parse complete ones individually
	
	sprintf(buf,"packet pseq=%4d read %d from RF. Cmd=%2d addr=%4d RF_addr=%4d\n",pcount++,MsgSize,LB->cmd,LB->addr,RF_addr);
	DCMSG_HEXB(GREEN,buf,Mbuf,MsgSize);
	
	// only respond if our address matches
	if (RF_addr==LB->addr){
	    
	// check the CRC
	//  if good CRC, parse and respond or whatever
	switch (LB->cmd){
	    case LBC_REQUEST_NEW:
		DCMSG(BLUE,"Recieved 'request new devices' packet.");
		// create a RESPONSE packet
		rLB.cmd=LBC_DEVICE_REG;

		LB_devreg =(LB_device_reg_t *)(&rLB);	// map our bitfields in
		LB_devreg->dev_type=1;			// SIT with MFS
		LB_devreg->devid=0x0a0b0c;		// mock MAC address

		RF_addr=1710;	//  use the fancy hash algorithm to come up with the real temp address
		LB_devreg->temp_addr=RF_addr;
		
		// calculates the correct CRC and adds it to the end of the packet payload
		set_crc8(&rLB,RF_size(LB_devreg->cmd));
		
		DCMSG(BLUE,"setting temp addr to %4d (0x%x) after CRC calc  RF_addr= %4d (0x%x)",RF_addr,RF_addr,LB_devreg->temp_addr,LB_devreg->temp_addr);

		// now send it to the RF master
		// after a brief wait
		sleep(1);
		result=write(RFfd,&rLB,RF_size(LB_devreg->cmd));
		sprintf(hbuf,"new device response to RFmaster devid=%6x address=%4d (%4x) len=%2d wrote %d\n"
			,LB_devreg->devid,LB_devreg->temp_addr,LB_devreg->temp_addr,LB_devreg->length,result);
		DCMSG_HEXB(BLUE,hbuf,&rLB,RF_size(LB_devreg->cmd));

		break;

	    case LBC_DEVICE_ADDR:
		DCMSG(BLUE,"Recieved 'device address' packet.");
		LB_addr =(LB_device_addr_t *)(&LB);	// map our bitfields in

		DCMSG(BLUE,"Dest addr %d matches current address, assigning new address %4d (0x%x)  (0x%x):11"
		      ,RF_addr,LB_addr->new_addr,LB_addr->new_addr,LB_addr->new_addr);
		RF_addr=LB_addr->new_addr;	// set our new address
		
		break;

	    case LBC_EXPOSE:
		if (LB->addr==RF_addr){
		    DCMSG(BLUE,"Dest addr %d matches current address, cmd= %d",RF_addr,cmd);
		    LB_exp =(LB_expose_t *)(&LB);	// map our bitfields in

		    DCMSG(BLUE,"Expose: exp hitmode tokill react mfs thermal\n"
			       "        %3d   %3d     %3d   %3d  %3d   %3d",LB_exp->expose,LB_exp->hitmode,LB_exp->tokill,LB_exp->react,LB_exp->mfs,LB_exp->thermal);
		}
		break;
		
	    default:

		if (LB->addr==RF_addr){
		    DCMSG(BLUE,"Dest addr %d matches current address, cmd = %d",RF_addr,LB->cmd);
		}		    
		break;
	}
	}
    }
}

/*************
 *************  RFslave stand-alone program.
 *************   
 *************  if there is no argument, use defaultPORT
 *************  otherwise use the first are as the port number to listen on.
 *************
 *************  Also we first set up our connection to the RF modem on the serial port.
 *************
 *************  then we loop until we have a connection, and then when we get one we call the
 *************  handleRF routine which does all the communicating.
 *************    when the socket dies, we come back here and listen for a new MCP
 *************    
 *************/

int main(int argc, char **argv) {
    int serversock;			/* Socket descriptor for server connection */
    int MCPsock;			/* Socket descriptor to use */
    int RFfd;				/* File descriptor for RFmodem serial port */
    struct sockaddr_in ServAddr;	/* Local address */
    struct sockaddr_in ClntAddr;	/* Client address */
    unsigned short RFslaveport;	/* Server port */
    unsigned int clntLen;               /* Length of client address data structure */
    char ttyport[32];	/* default to ttyS0  */

    

    printf(" argc=%d argv[0]=\"%s\" argv[1]=\"%s\" \n",argc,argv[0],argv[1]);
/*    if (argc == 1){
	RFslaveport = defaultPORT;
	printf(" Listening on default port <%d>, comm port = <%s>\n", RFslaveport,ttyport);
    }
    */
    if (argv[1]){
	strcpy(ttyport,argv[1]);
    } else {
	strcpy(ttyport,"/dev/ttyS1");
    }
    
    printf(" Watching comm port = <%s>\n",ttyport);


    // turn power to low for the radio A/B pin
    RFfd=open("/sys/class/gpio/export",O_WRONLY,"w");
    write(RFfd,"39",1);
    close(RFfd);
    RFfd=open("/sys/class/gpio/gpio39/direction",O_WRONLY,"w");
    write(RFfd,"out",3);
    close(RFfd);
    RFfd=open("/sys/class/gpio/gpio39/value",O_WRONLY,"w");
    write(RFfd,"0",1);		// a "1" here would turn on high power
    close(RFfd);
    RFfd=open("/sys/class/gpio/unexport",O_WRONLY,"w");
    write(RFfd,"39",1);		// this lets any kernel modules use the pin from now on
    close(RFfd);
    
    DCMSG(YELLOW,"A/B set for Low power.\n");
    
//   Okay,   set up the RF modem link here

   RFfd=open_port(ttyport); 
    
   HandleRF(RFfd);
   DCMSG(BLUE,"Connection to MCP closed.   listening for a new MCPs");
	
}






