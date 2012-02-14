#include "mcp.h"
#include "rf.h"

int verbose;	// so debugging works right in all modules


// tcp port we'll listen to for new connections
#define defaultPORT 4004

// size of client buffer
#define CLIENT_BUFFER 1024

void DieWithError(char *errorMessage){
    char buf[200];
    strerror_r(errno,buf,200);
    DCMSG(RED,"RFmaster %s %s \n", errorMessage,buf);
    exit(1);
}

/*************
 *************  HandleRF
 *************
 *************  when we first start, or if the we are idle for some amount of time,
 *************  we need to send 'request new devices' on the radio,
 *************  and register those devices.
 *************
 *************  At the MCP level, it really shouldn't start any minions until and unless
 *************  we have a registered slave device, and if that registered device dissappears
 *************  then after some suitable time the minion for it should die or hibernate or something
 *************
 *************  We have a socket to the MCP,  and Serial connection to the RF
 *************
 *************  Just pass messages between the two...
 *************  
 *************  if we have a socket to an MCP, we act as a driver/link to the RF modem
 *************  through our serial port.  for now the RFmaster will only forward comms in
 *************  both directions - TCP connection to RF, and RF to TCP.
 *************    At some point it might be best if the RFmaster does some data massaging,
 *************  but until that is clear to me what it would need to do, for now we just pass comms
 *************
 *************/

#define MbufSize 4096

void HandleRF(int MCPsock,int RFfd){
    struct timespec elapsed_time, start_time, istart_time,delta_time;
    char Mbuf[MbufSize],Rbuf[256], buf[200];        /* Buffer MCP socket */
    int Rptr,Rstart,size;
    int MsgSize,result,sock_ready,pcount=0;                    /* Size of received message */
    fd_set rf_or_mcp;
    struct timeval timeout;
    int maxcps=500,rfcount=0;		/*  characters per second that we can transmit without melting - 1000 is about 100% */
    double cps;

    // packet header so we can determine the length from the command in the header
    LB_packet_t *LB;

    


    Rstart=0;	// start position in the Rbuf of this packet - in case we get more than 1 packet
    Rptr=0;	// position in the Rbuf - so we can join split packets
/**   loop until we lose connection  **/
    clock_gettime(CLOCK_MONOTONIC,&istart_time);	// get the intial current time
    while(1) {

	
	timestamp(&elapsed_time,&istart_time,&delta_time);
	DDCMSG(D_TIME,CYAN,"RFmaster top of main loop at %5ld.%09ld timestamp, delta=%5ld.%09ld"
	       ,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);

	/*   do a select to see if we have a message from either the RF or the MCP  */
	/* then based on the source, send the message to the destination  */
	/* create a fd_set so we can monitor both the mcp and the connection to the RCC*/
	FD_ZERO(&rf_or_mcp);
	FD_SET(MCPsock,&rf_or_mcp);		// we are interested hearing the mcp
	FD_SET(RFfd,&rf_or_mcp);		// we also want to hear from the RF world
	
	/*   actually for now we will block until we have data - no need to timeout [yet?]
	 *     I guess a timeout that will make sense later is if the radio is ready for data
	 *   and we have no MCP data to send, then we should make the rounds polling
	 *   for status.  At the moment I don't want to impliment this at the RFmaster level,
	 *   for my initial working code I am shooting for the MCP to do all the work and
	 *   the RFmaster to just pass data back and forth.
	 */

//	DCMSG(YELLOW,"RFmaster waiting for select(rf or mcp)");

	timeout.tv_sec=0;
	timeout.tv_usec=100000;	
	sock_ready=select(FD_SETSIZE,&rf_or_mcp,(fd_set *) 0,(fd_set *) 0, NULL);

	if (sock_ready<0){
	    strerror_r(errno,Mbuf,200);
	    DCMSG(RED,"RFmaster select error: %s", Mbuf);
	    exit(-1);
	}

	//  Testing has shown that we generally get 8 character chunks from the serial port
	//  when set up the way it is right now.
	// we must splice them back together
	//  which means we have to be aware of the Low Bandwidth protocol
	//  if we have data to read from the RF, read it then blast it back upstream to the MCP
	if (FD_ISSET(RFfd,&rf_or_mcp)){
	    /* Receive message, or continue to recieve message from RF */
	    if ((MsgSize = read(RFfd,&Rbuf[Rptr],MbufSize)) < 0) DieWithError("read(RFfd,...) failed");
	    Rptr+=MsgSize;	// accumulate the packet size

	    if (Rptr-Rstart<3){
		// no chance of complete packet, so just increment the Rptr and keep waiting
	    } else {
		// we have a chance of a compelete packet
		LB=(LB_packet_t*)&Rbuf[Rstart];	// map the header in
		size=RF_size(LB->cmd);
		if ((Rstart-Rptr) <= size){
		    //  we do have a complete packet
		    // we could check the CRC and dump it here
		    
		    result=write(MCPsock,&Rbuf[Rstart],size);
		    if (result==size) {
			sprintf(buf,"RF ->MCP  [%2d]  ",size);
			DCMSG_HEXB(GREEN,buf,&Rbuf[Rstart],size);
		    } else {
			sprintf(buf,"RF ->MCP  [%d!=%d]  ",size,result);
			DCMSG_HEXB(RED,buf,&Rbuf[Rstart],size);
		    }
		    if ((Rstart-Rptr) > size){
			Rstart+=size;	// step ahead to the next packet
		    } else {
			Rstart=0;	// step ahead to the next packet

		    }
		}
	    }
	

	    memset(Mbuf,0,MsgSize+3);
	}

	//  if we have data to read from the MCP, read it then force it down the radio
	//    we will have to watch to make sure we don't force down too much for the radio
	if (FD_ISSET(MCPsock,&rf_or_mcp)){
    /* Receive message from MCP */
	    if ((MsgSize = recv(MCPsock, Mbuf, MbufSize, 0)) < 0)
		DieWithError("recv() failed");

	    if (MsgSize){

		// we need a buffer that we can use to stage output to the radio in
		// plus, we probably need some kind of handshake back to the MCP so
		// we can throttle it down

		// we will keep a count of chars, and at the next transmission we will know if it will be
		// above or below the duty cycle limit.  But we can also transmit up to burst time limit
		// before we care.  we should keep a moving average of about a minute for CPS, and watch the burst times
		// IIR average should work.
		// blindly write it  to the  radio
		
		result=write(RFfd,Mbuf,MsgSize);
		if (result==MsgSize) {
		    sprintf(buf,"MCP-> RF  [%2d]  ",MsgSize);
		    DCMSG_HEXB(BLUE,buf,Mbuf,MsgSize);
		} else {
		    sprintf(buf,"MCP-> RF  [%d!=%d]  ",MsgSize,result);
		    DCMSG_HEXB(RED,buf,Mbuf,MsgSize);
		}

		rfcount+=MsgSize;
		timestamp(&elapsed_time,&istart_time,&delta_time);

		cps=(double) rfcount/(double)elapsed_time.tv_sec;
		
		DDCMSG(D_TIME,CYAN,"RFmaster average cps = %f.  max at current duty is %d",cps,maxcps);
		
		memset(Mbuf,0,MsgSize+3);
	    } else {
		// the socket to the MCP seems to have closed...

	    }
	}  // end of MCP_sock
    } // end while 1
    close(MCPsock);    /* Close socket */    
}


void print_help(int exval) {
    printf("RFmaster [-h] [-v num] [-t comm] [-p port] \n\n");
    printf("  -h            print this help and exit\n");
    printf("  -p 4000       set listen port\n");
    printf("  -t /dev/ttyS1 set serial port device\n");
    printf("  -v 2          set verbosity bits\n");
    printf("  -s 10         max number of slaves to look for\n");
    exit(exval);
}

/*************
 *************  RFmaster stand-alone program.
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
    unsigned short RFmasterport;	/* Server port */
    unsigned int clntLen;               /* Length of client address data structure */
    char ttyport[32];	/* default to ttyS0  */
    int opt,slave_count;

    slave_count=10;
    verbose=0;
    RFmasterport = defaultPORT;
    strcpy(ttyport,"/dev/ttyS0");
    
    while((opt = getopt(argc, argv, "hv:t:p:s:")) != -1) {
	switch(opt) {
	    case 'h':
		print_help(0);
		break;

	    case 'v':
		verbose = atoi(optarg);
		break;

	    case 't':
		strcpy(ttyport,optarg);
		break;

	    case 'p':
		RFmasterport = atoi(optarg);
		break;

	    case 's':
		slave_count = atoi(optarg);
		break;

	    case ':':
		fprintf(stderr, "Error - Option `%c' needs a value\n\n", optopt);
		print_help(1);
		break;

	    case '?':
		fprintf(stderr, "Error - No such option: `%c'\n\n", optopt);
		print_help(1);

		break;
	}
    }
    DCMSG(YELLOW,"RFmaster: verbosity is set to 0x%x", verbose);
    DCMSG(YELLOW,"RFmaster: listen for MCP on TCP/IP port %d",RFmasterport);
    DCMSG(YELLOW,"RFmaster: comm port for Radio transciever = %s",ttyport);
    DCMSG(YELLOW,"RFmaster: will look for up to %d Slave devices",slave_count);
    
//   Okay,   set up the RF modem link here

   RFfd=open_port(ttyport); 

//  now Create socket for the incoming connection */

    if ((serversock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	DieWithError("socket() failed");

	/* Construct local address structure */
    memset(&ServAddr, 0, sizeof(ServAddr));   /* Zero out structure */
    ServAddr.sin_family = AF_INET;                /* Internet address family */
    ServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    ServAddr.sin_port = htons(RFmasterport);      /* Local port */

	/* Bind to the local address */
    if (bind(serversock, (struct sockaddr *) &ServAddr, sizeof(ServAddr)) < 0)
	DieWithError("bind() failed");

	/* Set the size of the in-out parameter */
    clntLen = sizeof(ClntAddr);

	/* Mark the socket so it will listen for incoming connections */
	/* the '1' for MAXPENDINF might need to be a '0')  */
    if (listen(serversock, 2) < 0)
	DieWithError("listen() failed");

    for (;;) {

	/* Wait for a client to connect */
	if ((MCPsock = accept(serversock, (struct sockaddr *) &ClntAddr,  &clntLen)) < 0)
	    DieWithError("accept() failed");

	/* MCPsock is connected to a Master Control Program! */

	DCMSG(BLUE,"Good connection to MCP <%s>  (or telnet or somebody)", inet_ntoa(ClntAddr.sin_addr));
	HandleRF(MCPsock,RFfd);
	DCMSG(RED,"Connection to MCP closed.   listening for a new MCP");
	
    }
    /* NOT REACHED */
}



