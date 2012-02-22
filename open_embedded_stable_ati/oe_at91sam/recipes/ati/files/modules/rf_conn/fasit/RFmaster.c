#include "mcp.h"
#include "rf.h"

int verbose,slottime,slot_count;	// globals

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
    char Mbuf[MbufSize],Rbuf[512], buf[200];        /* Buffer MCP socket */
    char  *Mptr,*Mstart;
    char  *Rptr,*Rstart;
    int size,gathered,delta,remaining_time;
    int MsgSize,result,sock_ready,pcount=0;                    /* Size of received message */
    fd_set rf_or_mcp;
    struct timeval timeout;
    int maxcps=500,rfcount=0;		/*  characters per second that we can transmit without melting - 1000 is about 100% */
    double cps;
    uint8 crc;

    // packet header so we can determine the length from the command in the header
    LB_packet_t *LB;

    // initialize our gathering buffer
    Rptr=Rbuf;
    Rstart=Rptr;

    memset(Rstart,0,100);

    remaining_time=100;		//  remaining time before we can transmit (in ms)
    
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
	
	timeout.tv_sec=remaining_time/1000;
	timeout.tv_usec=(remaining_time%1000)*1000;
	sock_ready=select(FD_SETSIZE,&rf_or_mcp,(fd_set *) 0,(fd_set *) 0, &timeout);

	if (sock_ready<0){
	    strerror_r(errno,Mbuf,200);
	    DCMSG(RED,"RFmaster select error: %s", Mbuf);
	    exit(-1);
	}

/*******      we timed out
 *******
 *******      check if the listening timeslots have expired
 *******      check if the radio is cold and has room for the next send
 *******/      
	//    we timed out.   
	if (!sock_ready){
	    // get the actual current time.
	    timestamp(&elapsed_time,&istart_time,&delta_time);
	    DDCMSG(D_TIME,CYAN,"RFmaster:  select timed out at %5ld.%09ld timestamp, delta=%5ld.%09ld"
		   ,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
	    delta = (delta_time.tv_sec*1000)+(delta_time.tv_nsec/1000);
	    DDCMSG(D_TIME,CYAN,"RFmaster:  delta=%2dms",delta);

	    if (delta>remaining_time) {
		//   we have waited long enough.
		//       we can transmit up to 250 bytes of the packet queue

		if (Mptr-Mstart<250){
		    result=write(RFfd,Mstart,Mptr-Mstart);
		} else {
		    result=write(RFfd,Mstart,250);
		}
		if (result<0){
		    strerror_r(errno,buf,200);		    
		    DCMSG(RED,"RFmaster:  write to RF error %s",buf);
		} else {
		    if (!result){
			DCMSG(RED,"RFmaster:  write to RF returned 0");
		    }

		    sprintf(buf,"MCP-> RF  [%2d]  ",result);
		    DCMSG_HEXB(BLUE,buf,Mstart,result);		    
		    
		    // update the ptrs - shift the queue down if we could not send it all
		    if (result<Mptr-Mstart){
			memmove(Mbuf,Mstart,result);
			Mptr-=result;
			Mstart=Mbuf;
		    } else {
			// it was all sent, just reset the pointers
			Mptr=Mstart=Mbuf;
		    }
		}
		
		rfcount+=MsgSize;
		timestamp(&elapsed_time,&istart_time,&delta_time);
		cps=(double) rfcount/(double)elapsed_time.tv_sec;
		DDCMSG(D_TIME,CYAN,"RFmaster average cps = %f.  max at current duty is %d",cps,maxcps);
	    }
	}

	//  Testing has shown that we generally get 8 character chunks from the serial port
	//  when set up the way it is right now.
	// we must splice them back together
	//  which means we have to be aware of the Low Bandwidth protocol
	//  if we have data to read from the RF, read it then blast it back upstream to the MCP
	if (FD_ISSET(RFfd,&rf_or_mcp)){
	    /* Receive message, or continue to recieve message from RF */

//    MAKE SURE THE RFfd is non-blocking!!!	
	    gathered = gather_rf(RFfd,Rptr,Rstart,300);
	    if (gathered>0){  // increment our current pointer
		Rptr=gathered+Rstart;
	    }
	/* Receive message, or continue to recieve message from RF */
	    DDCMSG(D_VERY,GREEN,"RFmaster: gathered =%2d  Rptr=%2d Rstart=%2d Rptr-Rstart=%2d  ",
		  gathered,Rptr-Rbuf,Rstart-Rbuf,Rptr-Rstart);

	    if (gathered>=3){
		// we have a chance of a compelete packet
		LB=(LB_packet_t *)Rstart;	// map the header in
		size=RF_size(LB->cmd);
		if ((Rptr-Rstart) >= size){
		    //  we do have a complete packet
		    // we could check the CRC and dump it here

		    crc=crc8(LB,size);
		    if (!crc) {
			result=write(MCPsock,Rstart,size);
			if (result==size) {
			    sprintf(buf,"RF ->MCP  [%2d]  ",size);
			    DDCMSG_HEXB(D_RF,GREEN,buf,Rstart,size);
			} else {
			    sprintf(buf,"RF ->MCP  [%d!=%d]  ",size,result);
			    DDCMSG_HEXB(D_RF,RED,buf,Rstart,size);
			}
		    } else {
			DCMSG(RED,"RF packet with BAD CRC ignored");
		    }
		    
		    if ((Rptr-Rstart) > size){
			Rstart+=size;	// step ahead to the next packet
			DDCMSG(D_VERY,RED,"Stepping to next packet, Rstart=%d Rptr=%d size=%d ",Rstart-Rbuf,Rptr-Rbuf,size);
			sprintf(buf,"  Next 8 chars in Rbuf at Rstart  ");
			DCMSG_HEXB(RED,buf,Rstart,8);

		    } else {
			Rptr=Rstart=Rbuf;	// reset to the beginning of the buffer
			DDCMSG(D_VERY,RED,"Resetting to beginning of Rbuf, Rstart=%d Rptr=%d size=%d ",Rstart-Rbuf,Rptr-Rbuf,size);
		    }
		    
		} else { // if (Rptr-Rstart) > size)
		    DDCMSG(D_VERY,RED,"we do not have a complete RF packet ");
		}
	    }  // if gathered >=3
	} // if this fd is ready

	//  if we have data to read from the MCP, read it then force it down the radio
	//    we will have to watch to make sure we don't force down too much for the radio
	if (FD_ISSET(MCPsock,&rf_or_mcp)){
    /* Receive message from MCP */
	    MsgSize = read(MCPsock, Mptr, MbufSize-(Mptr-Mbuf));
	    if (MsgSize<0){
		strerror_r(errno,buf,200);
		DCMSG(RED,"RFmaster: read from MCP fd=%d failed  %s ",MCPsock,buf);
		sleep(1);
	    }
	    if (!MsgSize){
		DCMSG(RED,"RFmaster: read from MCP returned 0");
	    }
	
	    if (MsgSize){

		// we need a buffer that we can use to stage output to the radio in
		// plus, we probably need some kind of handshake back to the MCP so
		// we can throttle it down

		// Mbuf is that buffer.   Mptr is the current insert point,
		// and Mstart is the beginning of unsent data

		// we will not actually copy it down to the RF at this point in time.

		Mptr+=MsgSize;	// add on the new length
		
	    }
	}  // end of MCP_sock
    } // end while 1
    close(MCPsock);    /* Close socket */    
}

void print_help(int exval) {
    printf("RFmaster [-h] [-v num] [-t comm] [-p port] [-x delay] \n\n");
    printf("  -h            print this help and exit\n");
    printf("  -p 4000       set listen port\n");
    printf("  -t /dev/ttyS1 set serial port device\n");
    printf("  -s 4          slot_count - max number of slots\n");
    printf("  -l 250        slottime in ms\n");
    printf("  -x 800        xmit test, xmit a request devices every arg milliseconds (default=disabled=0)\n");
    print_verbosity();
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
    int opt,slave_count,xmit;

    slot_count=4;	// determined by hashing function
    slottime=150;	// time of each slot and the hold-off period
    verbose=0;
    xmit=0;		// used for testing
    RFmasterport = defaultPORT;
    strcpy(ttyport,"/dev/ttyS0");
    
    while((opt = getopt(argc, argv, "hv:t:p:s:x:l:")) != -1) {
	switch(opt) {
	    case 'h':
		print_help(0);
		break;

	    case 'v':
		verbose = strtoul(optarg,NULL,16);
		break;

	    case 't':
		strcpy(ttyport,optarg);
		break;

	    case 'p':
		RFmasterport = atoi(optarg);
		break;

	    case 's':
		slot_count = atoi(optarg);
		break;

	    case 'l':
		slottime = atoi(optarg);
		break;

	    case 'x':
		xmit = atoi(optarg);
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
    DCMSG(YELLOW,"RFmaster: slottime =%2dms",slottime);
    DCMSG(YELLOW,"RFmaster: slot_count =%2d",slot_count);
    
//   Okay,   set up the RF modem link here

   RFfd=open_port(ttyport); 


//  this section is just used for testing   
   if (xmit){
       LB_packet_t LB;
       LB_request_new_t *RQ;
       int result,size;
       char buf[200];
       RQ=(LB_request_new_t *)&LB;

       RQ->addr=2047;
       RQ->cmd=LBC_REQUEST_NEW;
	// calculates the correct CRC and adds it to the end of the packet payload
	// also fills in the length field
       size = RF_size(RQ->cmd);
       set_crc8(RQ,3);

       while(1){
	   usleep(xmit*1000);
	    // now send it to the RF master
	   result=write(RFfd,RQ,size);
	   if (result==size) {
	       sprintf(buf,"Xmit test  ->RF  [%2d]  ",size);
	       DCMSG_HEXB(BLUE,buf,RQ,size);
	   } else {
	       sprintf(buf,"Xmit test  ->RF  [%d!=%d]  ",size,result);
	       DCMSG_HEXB(RED,buf,RQ,size);
	   }
       }
   }  // if xmit testing section over.

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
