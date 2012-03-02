#include "mcp.h"
#include "rf.h"

int verbose;	// globals

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

#define Rxsize 4096
#define M1size 512
#define M2size 16384
#define M3size 16384
#define Txsize 256

void HandleRF(int MCPsock,int RFfd){
    struct timespec elapsed_time, start_time, istart_time,delta_time;
    queue_t *Rx,*M1,*M2,*M3,*Tx;
    int seq=1;		// the packet sequence numbers

    char Rbuf[512];
    char buf[200];        /* text Buffer  */
    char  *Rptr,*Rstart;
    int size,gathered,delta,remaining_time,bytecount;
    int MsgSize,result,sock_ready,pcount=0;                    /* Size of received message */
    fd_set rf_or_mcp;
    struct timeval timeout;
    int maxcps=500,rfcount=0;		/*  characters per second that we can transmit without melting - 1000 is about 100% */
    double cps;
    uint8 crc;
    int slottime,total_slots;
    int low_dev,high_dev;
    int seq_M2,seq_M3,burst;


    // packet header so we can determine the length from the command in the header
    LB_packet_t *LB;
    LB_request_new_t *LB_new;

    // initialize our gathering buffer
    Rptr=Rbuf;
    Rstart=Rptr;

    Rx=queue_init(Rxsize);	// incoming packet buffer
    M1=queue_init(M1size);	// sorting buffers
    M2=queue_init(M2size);
    M3=queue_init(M3size);
    Tx=queue_init(Txsize);	// outgoing Tx buffer

    memset(Rstart,0,100);

    remaining_time=100;		//  remaining time before we can transmit (in ms)

    /**   loop until we lose connection  **/
    clock_gettime(CLOCK_MONOTONIC,&istart_time);	// get the intial current time
    timestamp(&elapsed_time,&istart_time,&delta_time);	// make sure the delta_time gets set    
    while(1) {

	timestamp(&elapsed_time,&istart_time,&delta_time);
	DDCMSG(D_TIME,CYAN,"RFmaster:  Top of loop    %5ld.%09ld timestamp, delta=%5ld.%09ld"
	       ,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
	delta = (delta_time.tv_sec*1000)+(delta_time.tv_nsec/1000000);
	DDCMSG(D_TIME,CYAN,"RFmaster:  delta=%2dms",delta);

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

	timeout.tv_sec=remaining_time/1000;
	timeout.tv_usec=(remaining_time%1000)*1000;
	sock_ready=select(FD_SETSIZE,&rf_or_mcp,(fd_set *) 0,(fd_set *) 0, &timeout);
	DDCMSG(D_MEGA,YELLOW,"RFmaster:  select returned, sock_ready=%d",sock_ready);
	if (sock_ready<0){
	    strerror_r(errno,buf,200);
	    DCMSG(RED,"RFmaster select error: %s", buf);
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
	    DDCMSG(D_MEGA,CYAN,"RFmaster:  select timed out at %5ld.%09ld timestamp, delta=%5ld.%09ld"
		   ,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
	    delta = (delta_time.tv_sec*1000)+(delta_time.tv_nsec/1000000);
	    DDCMSG(D_MEGA,CYAN,"RFmaster:  delta=%2dms",delta);

	    DDCMSG(D_MEGA,YELLOW,"RFmaster:  select timed out  delta=%d remaining_time=%d  M1=%d M2=%d M3=%d"
		   ,delta,remaining_time,Queue_Depth(M1),Queue_Depth(M2),Queue_Depth(M3));

	    if (delta>remaining_time-1) {

/***************    there can be three types of MCP packets we need to be concerned with
 ***************
 ***************    1)  request new devices
 ***************        this is expecting responses from all un-assigned RFslaves.
 ***************        it will have to be sent alone or at the end of a string of no-response packets.
 ***************
 ***************    2)  status requests
 ***************        eventually these need to be set up to expect slotted responses from multiple
 ***************        devices - as we will want to burst multiple ones
 ***************
 ***************    3)  Commands
 ***************        commands will not recieve responses.
 ***************        they can be strung together in a burst that preceeds any response expecting requests
 ***************
 ***************    type #1 should be relativly seldom and can be sent alone or tacked to the end of a #3
 ***************            it expects up to  ((slottime+1)*(high_dev-low_dev)) responses time to wait
 ***************            
 ***************    type #2 eventually needs to be set as a burst that expects responses, can also be
 ***************            tacked onto the end of a #3
 ***************            
 ***************    type #3 can all be strung together now up to less than about 250 bytes
 ***************            to be sent as a 'burst'
 ***************
 ***************    Possibly when they are read from the MCP they should be split into 3 seperate queues
 ***************    
 ***************/

/****   advanced algortihm:
 ****
 ****   if there is a #1 packet
 ****       send up to 240 bytes of M3 and tack on the M1
 ****       do a timestamp to reset our clock
 ****       set remaining time to   (slot_count+1)*slottime 
 ****   else
 ****       if there is an M2 packet
 ****           figure out how big M2 is,
 ****               if M2 >240  then send a chunk of it
 ****           send up to 240-(M2 size chunk)  bytes of M3 - if any
 ****           tack on the M2 size chunk
 ****           set remaining time to   (M2slots)*slottime 
 ****       else
 ****           if there is an M3 packet
 ****               send up to 240  bytes of M3
 ****           else
 ****               think about polling using LBC_status_requests
 ****           endif
 ****       endif
 ****   endif
 ****
 ****  add to remaining time based on heat or 480cps and size we sent
 ****
 ****
 ****   simple algorithm:  CURRENTLY IMPLEMENTED
 ****   
 ****   if there is a #1 packet (should only be 3 bytes)
 ****       send it
 ****       do a timestamp to reset our clock
 ****       set remaining time to   (slot_count+1)*slottime    slot_count = high_dev-low_dev
 ****   else
 ****       if there is an M2 packet
 ****           send just 1 packet and set up the time to wait
 ****           set remaining time to   2*slottime 
 ****       else
 ****           if there is an M3 packet
 ****               send up to 240  bytes of M3
 ****           else
 ****               think about polling using LBC_status_requests
 ****           endif
 ****       endif
 ****   endif
 ****  add to remaining time based on heat or 480cps and size we sent
 ****/
//
		DDCMSG(D_MEGA,CYAN,"RFmaster:  seq_M1[%2d]=%2d  seq_M2[%2d]=%2d  seq_M3[%2d]=%2d"
		       ,Queue_Depth(M1),QueueSeq_peek(M1),Queue_Depth(M2),QueueSeq_peek(M2),Queue_Depth(M3),QueueSeq_peek(M3));

		//  we will start with the simple algorithm until debugged
		if (QueueSeq_peek(M1)){		// returns the sequence number of the queue, 0 if empty
		    LB=(LB_packet_t *)(M1->head+4);	// move past the sequence number
		    size=RF_size(LB->cmd);		    
		    ReQueue_seqstrip(Tx,M1,size);	// strips sequence number, copys to the TxBuf and deletes from front of old queue
		    remaining_time =(total_slots)*slottime;
		} else {

		    burst=3;
		    seq_M2=QueueSeq_peek(M2);
		    seq_M3=QueueSeq_peek(M3);
		    
		    while((seq_M2||seq_M3)&&(Queue_Depth(Tx)<240)&&burst){ // while we have room and something more to send

			DDCMSG(D_MEGA,CYAN,"RFmaster:  while bursting   seq_M1[%2d]:s%2d  seq_M2[%2d]:s%2d  seq_M3[%2d]:s%2d   Tx[%d]   burst=%d"
			       ,Queue_Depth(M1),QueueSeq_peek(M1),Queue_Depth(M2),QueueSeq_peek(M2),Queue_Depth(M3),QueueSeq_peek(M3),Queue_Depth(Tx),burst);

			usleep(100000);
			
			// do the M2 burst
			if (seq_M2&&((seq_M2<seq_M3)||!seq_M3)){
			    LB=(LB_packet_t *)(M2->head+4);
			    size=RF_size(LB->cmd);
			    ReQueue_seqstrip(Tx,M2,size);	// copy it to the TxBuf and deletes from front of old queue
			    remaining_time +=slottime;	// add time for a response to this one
			    burst=2;
			} else {
			    // do the M3 burst
			    if (seq_M3&&((seq_M3<seq_M2)||!seq_M2)){
				if (burst==3){

				    sprintf(buf,"M3[%2d:%2d]  ",M3->head-M3->buf,M3->tail-M3->buf);
				    DCMSG_HEXB(YELLOW,buf,M3->head,Queue_Depth(M3));
				    
				    LB=(LB_packet_t *)(M3->head+4);
				    size=RF_size(LB->cmd);
				    
				    DDCMSG(D_MEGA,YELLOW,"RFmaster: in M3 -  before requeueing %d bytes to Tx: M3[%2d:%2d]:%d(seq=%d/seq_M3=%d)   Tx[%2d:%2d]:%d "
					   ,size,M3->head-M3->buf,M3->tail-M3->buf,Queue_Depth(M3),QueueSeq_peek(M3),seq_M3,Tx->head-Tx->buf,Tx->tail-Tx->buf,Queue_Depth(Tx));	
				    ReQueue_seqstrip(Tx,M3,size);	// copy it to the TxBuf and deletes from front of old queue
				    DDCMSG(D_RF  ,YELLOW,"RFmaster: in M3 -  after requeueing %d bytes to Tx: M3[%2d:%2d]:%d(seq=%d/seq_M3=%d)   Tx[%2d:%2d]:%d "
					   ,size,M3->head-M3->buf,M3->tail-M3->buf,Queue_Depth(M3),QueueSeq_peek(M3),seq_M3,Tx->head-Tx->buf,Tx->tail-Tx->buf,Queue_Depth(Tx));	
				} else {
				    burst=0;	// bursting is done!  force the packet to send
				}
			    } else {
				burst=0;	// bursting is done!  force the packet to send

			    }
			}

			//   update the seq for the next loop through
			seq_M2=QueueSeq_peek(M2);
			seq_M3=QueueSeq_peek(M3);
		    }// end of while bursting

		    remaining_time +=500;	// blindly wait an extra 500 ms
		}  // else it is not M1


		DDCMSG(D_MEGA,CYAN,"RFmaster:  before Tx to RF.  Tx[%d]",Queue_Depth(Tx));

		if (Queue_Depth(Tx)){  // if we have something to Tx, Tx it.
		    sprintf(buf,"MCP-> RF  [Que=%2d]  ",Queue_Depth(Tx));
		    DCMSG_HEXB(YELLOW,buf,Tx->head,Queue_Depth(Tx));

		    result=write(RFfd,Tx->head,Queue_Depth(Tx));
		    if (result<0){
			strerror_r(errno,buf,200);		    
			DCMSG(RED,"RFmaster:  write Tx queue to RF error %s",buf);
		    } else {
			if (!result){
			    DCMSG(RED,"RFmaster:  write Tx queue to RF returned 0");
			}
			sprintf(buf,"MCP-> RF  [%2d]  ",result);
			DCMSG_HEXB(BLUE,buf,Tx->head,result);
		    }
		    DeQueue(Tx,Queue_Depth(Tx));
		}
		timestamp(&elapsed_time,&istart_time,&delta_time);
		DDCMSG(D_MEGA,CYAN,"RFmaster:  Just might have Tx'ed to RF   at %5ld.%09ld timestamp, delta=%5ld.%09ld"
		       ,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
		/*****    this stuff below isn't really tested  *****/		
		rfcount+=MsgSize;
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

		/* Receive message, or continue to recieve message from RF */
		DDCMSG(D_VERY,GREEN,"RFmaster: cmd=%d addr=%d RF_size =%2d  Rptr-Rstart=%2d  ",
		       LB->cmd,LB->addr,size,Rptr-Rstart);

		if ((Rptr-Rstart) >= size){
		    //  we do have a complete packet
		    // we could check the CRC and dump it here

		    crc=crc8(LB);
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



/***************    if we have data to read from the MCP, read it then sort it into the three packet-type queues.
 ***************    prefixing a sequence number so we don't send any data out-of-order when we do our sorting and bandwidth optimizing
 ***************    
 ***************   If the socket to MCP closed, we free the dynamically allocated queues and then we return to main
 ***************   which waits for another MCP to connect
 ***************
 ***************/

	if (FD_ISSET(MCPsock,&rf_or_mcp)){
	    /* Receive message from MCP */
	    MsgSize = recv(MCPsock, Rx->tail, Rxsize-(Rx->tail-Rx->buf),0);	    
	    DCMSG(YELLOW,"RFmaster: read %d from MCP.  now to sort",MsgSize);

	    if (MsgSize<0){
		strerror_r(errno,buf,200);
		DCMSG(RED,"RFmaster: read from MCP fd=%d failed  %s ",MCPsock,buf);
		sleep(1);
	    }
	    if (!MsgSize){
		DCMSG(RED,"RFmaster: read from MCP returned 0 - MCP closed");
		free(Rx);
		free(M1);
		free(M2);
		free(M3);
		free(Tx);
		close(MCPsock);    /* Close socket */    
		return;			/* go back to the main loop waiting for MCP connection */
	    }

	    if (MsgSize){

/***************    there can be three types of MCP packets we need to be concerned with
 ***************
 ***************    1)  request new devices
 ***************        get put into M1queue
 ***************
 ***************    2)  status requests
 ***************        get put into M2queue
 ***************
 ***************    3)  Commands
 ***************        get put into M3queue
 ***************
 ***************    we are going to prefix anything we put in the M[123] queues with a sequence number.
 ***************    this enables us to keep chronological order
 ***************
 ***************    we could also make a little 'info' structure that has sequence numbers, time stamps, or who knows what
 ***************    in it and that could be prefixed onto each packet.     but for now we will just use a seq num
 ***************    
 ***************    We are going to do a little parsing here and split them into seperate queues
 ***************
 ***************    there will also need to be some kind of throttle to slow the MCP if we are full.
 ***************    
 ***************    */

		Rx->tail+=MsgSize;	// add on the new length
		bytecount=Queue_Depth(Rx);

		// loop until we are out of complete packets
		while (bytecount>=3) {  // make sure there is a possibility of a whole packet
		    LB=(LB_packet_t *)Rx->head;
		    size=RF_size(LB->cmd);
		    DDCMSG(D_VERY,YELLOW,"RFmaster: bytecount = %2d LB  cmd=%d size=%d",bytecount,LB->cmd,size);

		    if (size>=bytecount){  // sort this packet which is complete although there could be more
			switch (LB->cmd) {
			    case LBC_REQUEST_NEW:	// put in queue #1
				
				LB_new =(LB_request_new_t *) LB;	// we need to parse out the slottime, high_dev and low_dev from this packet.
				slottime=LB_new->slottime*5;		// convert passed slottime back to milliseconds
				low_dev=LB_new->low_dev;
				high_dev=LB_new->high_dev;
				total_slots=high_dev-low_dev+2;		// total number of slots with end padding

				EnQueueSeq(M1,seq++);	// prefixes the sequence number into the queue
				ReQueue(M1,Rx,size);	// move count from the front of queue Rx to tail of M1
				DCMSG(YELLOW,"RFmaster: requeued into M1[%d:%d]:%d s%d  size=%d",
				      M1->head-M1->buf,M1->tail-M1->buf,Queue_Depth(M1),QueueSeq_peek(M1),size);
				sprintf(buf,"M1[%2d:%2d]  ",M1->head-M1->buf,M1->tail-M1->buf);
				DCMSG_HEXB(YELLOW,buf,M1->head,Queue_Depth(M1));
				break;

			    case LBC_STATUS_REQ:	// put in queue #2
				EnQueueSeq(M2,seq++);	// prefixes the sequence number into the queue
				ReQueue(M2,Rx,size);	// move count from the front of queue Rx to tail of M1
				DCMSG(YELLOW,"RFmaster: requeued into M2[%d:%d]:%d s%d  size=%d",
				      M2->head-M2->buf,M2->tail-M2->buf,Queue_Depth(M2),QueueSeq_peek(M2),size);
				sprintf(buf,"M2[%2d:%2d]  ",M2->head-M2->buf,M2->tail-M2->buf);
				DCMSG_HEXB(YELLOW,buf,M2->head,Queue_Depth(M2));

				break;

			    default:		// everything else is assumed to be a command that does not expect a response - put in queue #3
				EnQueueSeq(M3,seq++);	// prefixes the sequence number into the queue
				ReQueue(M3,Rx,size);	// move count from the front of queue Rx to tail of M1				
				DCMSG(YELLOW,"RFmaster: requeued into M3[%d:%d]:%d s%d  size=%d",
				      M3->head-M3->buf,M3->tail-M3->buf,Queue_Depth(M3),QueueSeq_peek(M3),size);
				sprintf(buf,"M3[%2d:%2d]  ",M3->head-M3->buf,M3->tail-M3->buf);
				DCMSG_HEXB(YELLOW,buf,M3->head,Queue_Depth(M3));

				break;
			} // end of switch
			bytecount-=size;

		    } else { //  size check was not a whole packet
			bytecount=0;	// we need to bail out
		    }
		} // if we have at least 3 bytes 
	    }
	}  // end of MCP_sock
    } // end while 1
}

void print_help(int exval) {
    printf("RFmaster [-h] [-v num] [-t comm] [-p port] [-x delay] \n\n");
    printf("  -h            print this help and exit\n");
    printf("  -p 4000       set listen port\n");
    printf("  -t /dev/ttyS1 set serial port device\n");
    printf("  -x 800        xmit test, xmit a request devices every arg milliseconds (default=disabled=0)\n");
    printf("  -s 150        slottime in ms (5ms granules, 1275 ms max  ONLY WITH -x\n");
    printf("  -d 0x20       Lowest devID to find  ONLY WITH -x\n");
    printf("  -D 0x30       Highest devID to find  ONLY WITH -x\n");
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
    int opt,xmit;
    int slottime,total_slots;
    int low_dev,high_dev;

    verbose=0;
    xmit=0;		// used for testing
    RFmasterport = defaultPORT;
    strcpy(ttyport,"/dev/ttyS0");


    while((opt = getopt(argc, argv, "hv:t:p:s:x:l:d:D:")) != -1) {
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
		slottime = atoi(optarg);
		break;

	    case 'd':
		low_dev = strtoul(optarg,NULL,16);
		break;

	    case 'D':
		high_dev = strtoul(optarg,NULL,16);
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

    //   Okay,   set up the RF modem link here

    RFfd=open_port(ttyport); 


    //  this section is just used for testing   
    if (xmit){	
	LB_packet_t LB;
	LB_request_new_t *RQ;
	int result,size;
	char buf[200];

	total_slots=high_dev-low_dev+2;
	if (!slottime || (high_dev<low_dev)){
	    DCMSG(RED,"\nRFmaster: xmit test: slottime=%d must be set and high_dev=%d cannot be less than low_dev=%d",slottime,high_dev,low_dev);
	    exit(-1);
	} else {

	    DCMSG(GREEN,"RFmaster: xmit test: slottime=%d  high_dev=%d low_dev=%d  total_slots=%d",slottime,high_dev,low_dev,total_slots);
	}

	if (slottime>1275){
	    DCMSG(RED,"\nRFmaster: xmit test: slottime=%d must be set between 50? and 1275 milliseconds",slottime);
	    exit(-1);
	}

	
	RQ=(LB_request_new_t *)&LB;

	RQ->cmd=LBC_REQUEST_NEW;
	RQ->low_dev=low_dev;
	RQ->high_dev=high_dev;
	RQ->slottime=slottime/5;
	// calculates the correct CRC and adds it to the end of the packet payload
	// also fills in the length field
	size = RF_size(RQ->cmd);
	set_crc8(RQ);

	while(1){
	    usleep(total_slots*slottime*1000);
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
