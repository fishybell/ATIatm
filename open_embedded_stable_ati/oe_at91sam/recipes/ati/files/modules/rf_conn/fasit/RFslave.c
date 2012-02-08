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
    char Mbuf[MbufSize], buf[200];        /* Buffer MCP socket */
    int MsgSize,result,sock_ready,pcount=0;                    /* Size of received message */
    fd_set rf_or_mcp;
    struct timeval timeout;
    LB_packet_t *LB,rLB;
    int len,addr,cmd;

    LBC_device_reg *LB_devreg;

    
/**   loop until we lose connection  **/
    while(1){

	/* Receive message from RF */
	if ((MsgSize = read(RFfd,Mbuf,MbufSize)) < 0)
	    DieWithError("read(RFfd,...) failed");

	sprintf(buf,"RFslave: pcount=%4d  read %d chars from RF.",pcount++,MsgSize);
	DCMSG_HEXB(GREEN,buf,Mbuf,MsgSize);

	LB=(LB_packet_t *)Mbuf;
	// check the CRC

	//  if good CRC, parse and respond or whatever

	switch (LB->header&0x1F){

	    case LBC_REQUEST_NEW:
		DCMSG(BLUE,"Recieved 'request new devices' packet.");
		rLB.header=LB_HEADER(0,LBC_DEVICE_REG);

		LB_devreg =(LBC_device_reg *)(rLB.payload);	// map our bitfields in
		LB_devreg->dev_type=1;			// SIT with MFS
		LB_devreg->devid=0x0a0b0c;		// mock MAC address
		LB_devreg->temp_addr=1;


		// calculates the correct CRC and adds it to the end of the packet payload
		// also fills in the length field
		LB_CRC_add(&rLB,6);
		// now send it to the RF master
		result=write(RFfd,&rLB,rLB.length);
		DCMSG(BLUE,"write returned %d.",result);

		break;

	    case LBC_DEVICE_ADDR:
	    
		break;

	    default:


		break;

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






