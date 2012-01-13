#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include "mcp.h"
#include "fasit_c.h"


#define BufSize 1024

#define S_set(ITEM,D,ND,F,T) \
    { \
	S->ITEM.data = D; \
	S->ITEM.newdata = ND; \
	S->ITEM.flags = F; \
	S->ITEM.timer = T; \
    }

// initialize our state to default values
void initialize_state(minion_state_t *S){

    
    S_set(exp,0,0,0,0);
    S_set(asp,0,0,0,0);
    S_set(dir,0,0,0,0);
    S_set(move,0,0,0,0);
    S_set(speed,0,0,0,0);
    S_set(on,0,0,0,0);
    S_set(hit,0,0,0,0);
    S_set(react,0,0,0,0);
    S_set(tokill,0,0,0,0);
    S_set(sens,0,0,0,0);
    S_set(mode,0,0,0,0);
    S_set(burst,0,0,0,0);

    S_set(pos,0,0,0,0);
    S_set(type,0,0,0,0);
    S_set(hit_config,0,0,0,0);
    S_set(blanking,0,0,0,0);

    S_set(miles_code,0,0,0,0);
    S_set(miles_ammo,0,0,0,0);
    S_set(miles_player,0,0,0,0);
    S_set(miles_delay,0,0,0,0);

    S_set(mgs_on,0,0,0,0);
    S_set(phi_on,0,0,0,0);
}

// fill out default header information
void defHeader(int mnum, FASIT_header *fhdr,int seq) {
    fhdr->num = htons(mnum);
    fhdr->icd1 = htons(1);
    fhdr->icd2 = htons(1);
    fhdr->rsrvd = htonl(0);
    fhdr->seq = htonl(seq);
    switch (mnum) {
	case 100:
	case 2000:
	case 2004:
	case 2005:
	case 2006:
	    fhdr->icd1 = htons(2);
	    break;
    }
}

int read_FASIT_msg(thread_data_t *minion,char *buf, int bufsize){
    int msglen;
    FASIT_header *header;
    FASIT_header rhdr;
    FASIT_2111	 msg;
    
	//read the RCC
    msglen=read(minion->rcc_sock, buf, bufsize);
    if (msglen > 0) {
	buf[msglen]=0;
	DCMSG(BLUE,"MINION %d received %d chars from RCC", minion->mID,msglen);
	CPRINT_HEXB(BLUE,buf,msglen);

	header=(FASIT_header *)buf; 
	minion->seq=htonl(header->seq);
	DCMSG(BLUE,"MINION %d FASIT packet, cmd=%d ICD=%d.%d seq=%d len=%d", minion->mID,htons(header->num),htons(header->icd1),htons(header->icd2),minion->seq,htons(header->length));

	// we will return to the caller with the msglen, and they can look at the buffer and parse the message and respond to it
    } else if (msglen<0) {
	if (errno!=EAGAIN){
	    DCMSG(BLUE,"MINION %d read of RCC socket returned %d errno=%d socket to RCC closed",minion->mID, msglen,errno);
	    exit(-2);  /* this minion dies!   it should do something else maybe  */
	} else {
	    DCMSG(BLUE,"MINION %d socket to RCC closed, !!!", minion->mID);
	    exit(-2);  /* this minion dies!  possibly it should do something else - but maybe it dies of happyness  */
	}
    }
    return(msglen);
}

int write_FASIT_msg(thread_data_t *minion,void *hdr,int hlen,void *msg,int mlen){
struct iovec iov[2];
int result;
		// the goal here is to use writev so the header and body don't get seperated
    iov[0].iov_base=hdr;
    iov[0].iov_len=hlen;
    iov[1].iov_base=msg;
    iov[1].iov_len=mlen;

    result=writev(minion->rcc_sock,iov,2);
    if (result >= 0){
	DCMSG(BLUE,"MINION %d sent %d chars to RCC",minion->mID,iov[0].iov_len+iov[1].iov_len);
	CPRINT_HEXB(BLUE,iov[0].iov_base,iov[0].iov_len);
	CPRINT_HEXB(BLUE,iov[1].iov_base,iov[1].iov_len);

    } else {
	perror("writing stream message");
    }
    return(result);
}

// create and send a status messsage to the FASIT server
void sendStatus2102(int force, FASIT_header *hdr,thread_data_t *minion) {
    struct iovec iov[2];
    FASIT_2102 msg;
    int result;
    
    defHeader(2102, hdr,minion->seq); // sets the sequence number and other data
    hdr->length = htons(sizeof(FASIT_header) + sizeof(FASIT_2102));

    // fill message
    // start with zeroes
    memset(&msg, 0, sizeof(FASIT_2102));

    // fill out as response
    if (force) {
	msg.response.rnum = htons(hdr->num);	//  pulls the message number from the header  (htons was wrong here)
	msg.response.rseq = htons(hdr->seq);
    } else {
	msg.response.rnum = 0;	//  pulls the message number from the header  (htons was wrong here)
	msg.response.rseq = 0;
    }
    // exposure
    // instead of obfuscating the 0,45,and 90 values, just use them.

    msg.body.exp = minion->S.exp.data;

    // device type
    msg.body.type = 1; // SIT. TODO -- SIT vs. SAT vs. HSAT

    //   DCMSG(YELLOW,"before  doHits(-1)   hits = %d",hits) ;    
//    doHits(-1);  // request the hit count
    //   DCMSG(YELLOW,"retrieved hits with doHits(-1) and setting to %d",hits) ; 

    // hit record
    msg.body.hit = htons(minion->S.hit.data);
    /*
    switch (lastHitCal.enable_on) {
	case BLANK_ON_CONCEALED: msg->body.hit_conf.on = 1; break; // on
	case ENABLE_ALWAYS: msg->body.hit_conf.on = 1; break; // on
	case ENABLE_AT_POSITION: msg->body.hit_conf.on = 2; break; // on at
	case DISABLE_AT_POSITION: msg->body.hit_conf.on = 3; break; // off at
	case BLANK_ALWAYS: msg->body.hit_conf.on = 0; break; // off
    }
    msg->body.hit_conf.react = lastHitCal.after_kill;
    msg->body.hit_conf.tokill = htons(lastHitCal.hits_to_kill);

    // use lookup table, as our sensitivity values don't match up to FASIT's
    for (int i=15; i>=0; i--) { // count backwards from most sensitive to least
	if (lastHitCal.sensitivity <= cal_table[i]) { // map our cal value to theirs
	    msg->body.hit_conf.sens = htons(i); // found sensitivity value
	    break; // done looking
	}
    }
    */
    // use remembered value rather than actual value
    if (msg.body.hit_conf.sens == htons(15)) {
	msg.body.hit_conf.sens = htons(minion->S.sens.data);
    }

//    msg->body.hit_conf.burst = htons(lastHitCal.seperation); // burst seperation
//    msg->body.hit_conf.mode = lastHitCal.type; // single, etc.
    
    DCMSG(BLUE,"Preparing to send 2102 status packet:");
    DCMSG(BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n %6d  %d.%d  %6d  %6d  %7d"
	  ,htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htonl(hdr->seq),htonl(hdr->rsrvd),htons(hdr->length));
    DCMSG(BLUE,"R-Num = %4d  R-seq-#=%4d ",htons(msg.response.rnum),htonl(msg.response.rseq));
    DCMSG(BLUE,"\t\t\t\t\t\t\tmessage body\n "\
	  "PSTAT | Fault | Expos | Aspct |  Dir | Move |  Speed  | POS | Type | Hits | On/Off | React | ToKill | Sens | Mode | Burst\n"\
	  "  %3d    %3d     %3d     %3d     %3d    %3d    %6.2f    %3d   %3d    %3d      %3d     %3d      %3d     %3d    %3d    %3d ",
	  msg.body.pstatus,msg.body.fault,msg.body.exp,msg.body.asp,msg.body.dir,msg.body.move,msg.body.speed,msg.body.pos,msg.body.type,htons(msg.body.hit),
	  msg.body.hit_conf.on,msg.body.hit_conf.react,htons(msg.body.hit_conf.tokill),htons(msg.body.hit_conf.sens),msg.body.hit_conf.mode,htons(msg.body.hit_conf.burst));

    // send

    write_FASIT_msg(minion,hdr,sizeof(FASIT_header),&msg,sizeof(FASIT_2102));

}
//
//   Command Acknowledge
//
//   Since we seem to ack from a bunch of places, better to have a funciton
//
int send_2101_ACK(FASIT_header *hdr,int response,thread_data_t *minion) {
   // do handling of message
    FASIT_header rhdr;
    FASIT_2101 rmsg;

    DCMSG( MAGENTA,"Minion %d sending 2101 ACK\n",minion->mID);
    
   // build the response - some CID's just reply 2101 with 'S' for received and complied 
   // and 'F' for Received and Cannot comply
   // other Command ID's send other messages

    defHeader(2101, &rhdr,minion->seq); // sets the sequence number and other data
    rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2101));

   // set response
    rmsg.response.rnum = hdr->num;	//  pulls the message number from the header  (htons was wrong here)
    rmsg.response.rseq = hdr->seq;		

    rmsg.body.resp = response;	// The actual response code 'S'=can do, 'F'=Can't do
    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htons(rhdr.seq),htons(rhdr.rsrvd),htons(rhdr.length));
    DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nR-NUM | R-Seq | Response\n%5d  %6d  '%c'",
	  htons(rmsg.response.rnum),htons(rmsg.response.rseq),rmsg.body.resp);

    write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&rmsg,sizeof(FASIT_2101));

    DCMSG( MAGENTA,"2101 ACK  all queued up - someplace to go? \n");
    return 0;
}


//void  process_MCP_cmds(minion_state_t *minion,char *buf,int msglen){
//
//}
/* shared data between threads */
//double shared_x;
//pthread_mutex_t lock_x;

void *minion_thread(thread_data_t *minion){
    struct timespec elapsed_time;
    struct timespec start_time;
    struct timeval timeout;
    struct sockaddr_in address;
    int sock_ready;
    long elapsed_tenths;

    char buf[BufSize];
    char mbuf[BufSize];
    int i,msglen,result,seq;
    fd_set rcc_or_mcp;

    struct iovec iov[2];
    
    FASIT_header *header;
    FASIT_header rhdr;
    FASIT_2111	 msg;
    FASIT_2100	 *message_2100;
    FASIT_2110	 *message_2110;
    FASIT_13110	 *message_13110;

    initialize_state( &minion->S);
    
    DCMSG(BLUE,"MINION %d state is initialized as devid %lld", minion->mID,htonll(minion->devid));

    i=0;

#ifdef TEST
    i++;
    sprintf(mbuf,"msg %d I am minion %d with devid %lld", i,minion->mID,htonll(minion->devid));

    result=write(minion->mcp, mbuf, strlen(mbuf));
    if (result >= 0){
	DCMSG(BLUE,"MINION %d sent %d chars to MCP   --%s--",minion->mID,strlen(mbuf),mbuf);
    } else {
	perror("writing stream message");
    }

    msglen=read(minion->mcp, buf, 1023);
    if (msglen > 0) {
	buf[msglen]=0;
	DCMSG(BLUE,"MINION %d received from MCP %d chars-->%s<--", minion->mID,msglen,buf);
    } else if (!msglen) {
	perror("reading stream message");
    } else {
	DCMSG(BLUE,"MINION %d socket to MCP closed, we are FREEEEEeeee!!!", minion->mID);
    }
#endif

    // set the MCP socket to be non-blocking - we have other work to do
    // besides listening to CLU!
    fcntl(minion->mcp_sock, F_SETFL, O_NONBLOCK);

    // now we must get a connection to the range control
    // computer (RCC) using fasit.   make it non-blocking
    
    minion->rcc_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(minion->rcc_sock < 0)   {
	perror("socket() failed");
    }

 /* start with a clean address structure */
    memset(&address, 0, sizeof(struct sockaddr_in));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("192.168.10.203");
    address.sin_port = htons(4000);

    result=connect(minion->rcc_sock,(struct sockaddr *) &address, sizeof(struct sockaddr_in));
    if (result==-1){
	perror("connect() failed");
    }

// we now have a socket.
    
    DCMSG(BLUE,"MINION %d has a socket to a RCC", minion->mID);

// check to see if there is something to read using select() , before actually reading

// now read it using the new routine    
    result = read_FASIT_msg(minion, buf, BufSize);
    
    DCMSG(BLUE,"MINION %d should respond to the presumed device capabilites request", minion->mID);

// message 100 is responded with a 2111 as in st_client.cpp
// build a response here

    defHeader(2111,&rhdr,minion->seq++);	// fills in the default values for the response header 
    rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2111));

    // set response
    msg.response.rnum = htons(100);
    msg.response.rseq = rhdr.seq;

    // fill message
    msg.body.devid = minion->devid; // devid = Mac address

    //retrieve the PD_NES flag from the kernel like Nate described -
    //now that I used a kludge to get those options into the device when
    //the sit_client was constructed during startup
    msg.body.flags = 0;//PD_NES; // TODO -- find actual capabilities from command line
    write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&msg,sizeof(FASIT_2111));

    sprintf(mbuf,"MINION %d msg %d I am connected to an RCC",minion->mID,i);
    result=write(minion->mcp_sock, mbuf, strlen(mbuf));
    if (result >= 0){
	DCMSG(BLUE,"MINION %d sent %d chars to MCP   --%s--",minion->mID,strlen(mbuf),mbuf);
    } else {
	perror("writing stream message");
    }

    // main loop 
    //   respond to the mcp commands
    // respond to FASIT commands
    //  feed the MCP packets to send out the RF transmitter
    // update our state when MCP commands are RF packets back from our slave(s)
    //

    // setup the timeout for the first select.   subsequent selects have the timout set
    // in the timer related bits at the end of the loop
    //  the goal is to update all our internal state timers with tenth second resolution
    timeout.tv_sec=0;
    timeout.tv_usec=100000;
    
    while(1) {
	clock_gettime(CLOCK_MONOTONIC_RAW,&start_time);	// mark the start time s owe can run the timers
	
	/* create a fd_set so we can monitor both the mcp and the connection to the RCC*/
	FD_ZERO(&rcc_or_mcp);
	FD_SET(minion->mcp_sock,&rcc_or_mcp);		// we are interested hearing the mcp
	FD_SET(minion->rcc_sock,&rcc_or_mcp);		// we also want to hear from the RCC
	/* block for up to 100 milliseconds waiting for RCC or MCP 
	 * this is what we will use for the maximum increment on our timers 
	 * and we may drop through much faster if we have a MCP or RCC communication
	 * that we have to process
	 */

	timeout.tv_sec=0;
	timeout.tv_usec=100000;	
	sock_ready=select(FD_SETSIZE,&rcc_or_mcp,(fd_set *) 0,(fd_set *) 0, &timeout);	

	if (sock_ready<0){
	    perror("NOTICE!  select error : ");
	    return EXIT_FAILURE;
	}

	/////////////////

	//check to see if the MCP has any commands for us
	if (FD_ISSET(minion->mcp_sock,&rcc_or_mcp)){
	    msglen=read(minion->mcp_sock, buf, 1023);
	    if (msglen > 0) {
		buf[msglen]=0;
		DCMSG(BLUE,"MINION %d received %d chars-->%s<--", minion->mID,msglen,buf);
		// we have received a message from the mcp, process it
		// it is either a command, or it is an RF response from our slave
//		process_MCP_cmds(&state,buf,msglen);
	    } else if (msglen<0) {
		if (errno!=EAGAIN){
		    DCMSG(BLUE,"MINION %d read returned %d errno=%d socket to MCP closed, we are FREEEEEeeee!!!",minion->mID,msglen,errno);
		    exit(-2);  /* this minion dies!   it should do something else maybe  */
		}
	    }else {
		DCMSG(BLUE,"MINION %d socket to MCP closed, we are FREEEEEeeee!!!", minion->mID);
		exit(-2);  /* this minion dies!  possibly it should do something else - but maybe it dies of happyness  */
	    }
	}

	/**********************************    end of reading and processing the mcp command  ***********************/


	/*************** check to see if there is something to read from the rcc   **************************/
	if (FD_ISSET(minion->rcc_sock,&rcc_or_mcp)){
	    // now read it using the new routine    
	    result = read_FASIT_msg(minion,buf, BufSize);
	    if (result>0){
		// map header and body for both message and response
		header = (FASIT_header*)(buf);

		DCMSG(BLUE,"MINION %d: Process the recieved fasit packet num=%d", minion->mID,htons(header->num));

		// now we need to parse and respond to the message we just recieved
		// we have received a message from the mcp, process it
		// it is either a command, or it is an RF response from our slave

		switch (htons(header->num)) {
		    case 100:

		// message 100 is responded with a 2111 as in sit_client.cpp
		// build a response here

			defHeader(2111,&rhdr,seq++);	// fills in the default values for the response header 
			rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2111));

		// set response
			msg.response.rnum = htons(100);
			msg.response.rseq = rhdr.seq;

		// fill message
			msg.body.devid = minion->devid; // devid = MAC address

		//retrieve the PD_NES flag from the kernel like Nate described -
		//now that I used a kludge to get those options into the device when
		//the sit_client was constructed during startup
			msg.body.flags = 0;//PD_NES; // TODO -- find actual capabilities from command line
			write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&msg,sizeof(FASIT_2111));
			break;

		    case 2100:
			message_2100 = (FASIT_2100*)(buf + sizeof(FASIT_header));
			
			DCMSG(BLUE,"MINION %d: fasit packet 2100, CID=%d", minion->mID,message_2100->cid);

			// save response numbers
			//		    resp_num = header->num; //  pulls the message number from the header  (htons was wrong here)
			//		    resp_seq = header->seq;

			// Just parse out the command for now and print a pretty message
			switch (message_2100->cid) {
			    case CID_No_Event:
				DCMSG(RED,"CID_No_Event") ; 
				break;

			    case CID_Reserved01:
				DCMSG(RED,"CID_Reserved01") ;
				break;

			    case CID_Status_Request:
				DCMSG(RED,"CID_Status_Request") ; 
				break;

			    case CID_Expose_Request:
				DCMSG(RED,"CID_Expose_Request:  message_2100->exp=%d",message_2100->exp) ;
				break;

			    case CID_Reset_Device:
				DCMSG(RED,"CID_Reset_Device  ") ;
				break;

			    case CID_Move_Request:
				DCMSG(RED,"CID_Move_Request  ") ;       
				break;

			    case CID_Config_Hit_Sensor:
				DCMSG(RED,"CID_Config_Hit_Sensor") ;                
				break;

			    case CID_GPS_Location_Request:
				DCMSG(RED,"CID_GPS_Location_Request") ;
				break;

			    case CID_Shutdown:
				DCMSG(RED,"CID_Shutdown") ;
				break;
			}
#if 0			
			DCMSG(CYAN,"Full message decomposition....");
			DCMSG(CYAN,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d"
			      ,htons(header->num),htons(header->icd1),htons(header->icd2),htons(header->seq),htons(header->rsrvd),htons(header->length));
			
			DCMSG(CYAN,"\t\t\t\t\t\t\tmessage body\n"\
			      "C-ID | Expos | Aspct |  Dir | Move |  Speed | On/Off | Hits | React | ToKill | Sens | Mode | Burst\n"\
			      "%3d    %3d     %3d     %2d    %3d    %7.2f     %4d     %2d     %3d     %3d     %3d    %3d   %5d ",
			      message_2100->cid,message_2100->exp,message_2100->asp,message_2100->dir,message_2100->move,message_2100->speed
			      ,message_2100->on,htons(message_2100->hit),message_2100->react,htons(message_2100->tokill),htons(message_2100->sens)
			      ,message_2100->mode,htons(message_2100->burst));
#endif
			// do the event that was requested

			switch (message_2100->cid) {
			    case CID_No_Event:
				DCMSG(BLUE,"CID_No_Event  send 'S'uccess ack") ; 
				// send 2101 ack
				send_2101_ACK(header,'S',minion);
				break;

			    case CID_Reserved01:
				// send 2101 ack
				DCMSG(BLUE,"CID_Reserved01  send 'F'ailure ack") ;
				send_2101_ACK(header,'F',minion);
				break;

			    case CID_Status_Request:
				// send 2102 status
				DCMSG(BLUE,"CID_Status_Request   send 2102 status") ; 
				sendStatus2102(1,header,minion); // forces sending of a 2102
				// AND/OR? send 2115 MILES shootback status if supported
				//			    if (acc_conf.acc_type == ACC_NES_MFS){
				//				DCMSG(BLUE,"we also seem to have a MFS Muzzle Flash Simulator - TODO send 2112 status eventually") ; 
				//			    }
				// AND/OR? send 2112 Muzzle Flash status if supported   

				break;

			    case CID_Expose_Request:
				DCMSG(BLUE,"CID_Expose_Request  send 'S'uccess ack.   message_2100->exp=%d",message_2100->exp) ;       

				minion->S.exp.data=0;			// cheat and set the current state to 45
				minion->S.exp.newdata=message_2100->exp;	// set newdata to be the future state
				if (message_2100->exp==90){
				    minion->S.exp.flags=F_exp_expose_A;	// start it moving to expose
				} else if (message_2100->exp==00){
				    minion->S.exp.flags=F_exp_conceal_A;	// start it moving to conceal
				}
				minion->S.exp.timer=0;
				
				// send 2101 ack  (2102's will be generated at start and stop of actuator)
				send_2101_ACK(header,'S',minion);    // TRACR Cert complains if these are not there
				
				// it should happen in this many deciseconds
				//  - fasit device cert seems to come back immeadiately and ask for status again, and
				//    that is causing a bit of trouble.   
				    
				break;

			    case CID_Reset_Device:
				// send 2101 ack
				DCMSG(BLUE,"CID_Reset_Device  send 'S'uccess ack.   set lastHitCal.* to defaults") ;
				send_2101_ACK(header,'S',minion);
				// also supposed to reset all values to the 'initial exercise step value'
				//  which I am not sure if it is different than ordinary inital values 
				//			    fake_sens = 1;
				/*
				lastHitCal.seperation = 250;   //250;
				lastHitCal.sensitivity = cal_table[13]; // fairly sensitive, but not max
				lastHitCal.blank_time = 50; // half a second blanking
				lastHitCal.enable_on = BLANK_ALWAYS; // hit sensor off
				lastHitCal.hits_to_kill = 1; // kill on first hit
				lastHitCal.after_kill = 0; // 0 for stay down
				lastHitCal.type = 1; // mechanical sensor
				lastHitCal.invert = 0; // don't invert sensor input line
				lastHitCal.set = HIT_OVERWRITE_ALL;    // nothing will change without this
				doHitCal(lastHitCal); // tell kernel
				doHits(0); // set hit count to zero
*/
				break;

			    case CID_Move_Request:
				// send 2101 ack  (2102's will be generated at start and stop of actuator)
				DCMSG(BLUE,"CID_Move_Request  send 'S'uccess ack.   TODO send the move to the kernel?") ;        
				send_2101_ACK(header,'S',minion);
				break;

			    case CID_Config_Hit_Sensor:
				DCMSG(BLUE,"CID_Config_Hit_Sensor  send 'S'uccess ack.   TODO add sending a 2102?") ;
				//      actually Riptide says that the FASIT spec is wrong and should not send an ACK here       
				send_2101_ACK(header,'S',minion); // FASIT Cert seems to complain about this ACK

				// send 2102 status - after doing what was commanded
				// which is setting the values in the hit_calibration structure
				// uses the lastHitCal so what we set is recorded
				// there are fields that don't match up

				// TODO I believe a 2100 config hit sensor is supposed to set the hit count
				/*			    
				switch (message_2100->on) {
				case 0: lastHitCal.enable_on = BLANK_ALWAYS; break; // hit sensor Off
				case 1: lastHitCal.enable_on = BLANK_ON_CONCEALED; break; // hit sensor On Immediately
				case 2: lastHitCal.enable_on = ENABLE_AT_POSITION; break; // hit sensor On at Position
				case 3: lastHitCal.enable_on = DISABLE_AT_POSITION; break; // hit sensor Off at Position
				}

				if (htons(message_2100->burst)) lastHitCal.seperation = htons(message_2100->burst);      // spec says we only set if non-Zero
				if (htons(message_2100->sens)) {
				if (htons(message_2100->sens) > 15) {
				lastHitCal.sensitivity = cal_table[15];
				} else {
				lastHitCal.sensitivity = cal_table[htons(message_2100->sens)];
				}
				// remember told value for later
				//				fake_sens = htons(message_2100->sens);
				}
				if (htons(message_2100->tokill))  lastHitCal.hits_to_kill = htons(message_2100->tokill); 
				lastHitCal.after_kill = message_2100->react;    // 0 for stay down
				lastHitCal.type = message_2100->mode;           // mechanical sensor
				lastHitCal.set = HIT_OVERWRITE_ALL;    // nothing will change without this
				doHitCal(lastHitCal); // tell kernel by calling SIT_Clients version of doHitCal
				DCMSG(BLUE,"calling doHitCal after setting values") ;

*/
				// send 2102 status or change the hit count (which will send the 2102 later)
				if (1 /*hits == htons(message_2100->hit)*/) {
				    sendStatus2102(1,header,minion);  // sends a 2102 as we won't if we didn't change the the hit count
				    DCMSG(BLUE,"We will send 2102 status in response to the config hit sensor command"); 
				} else {
				    //				doHits(htons(message_2100->hit));    // set hit count to something other than zero
				    DCMSG(BLUE,"after doHits(%d) ",htons(message_2100->hit)) ;
				}

				break;

			    case CID_GPS_Location_Request:
				DCMSG(BLUE,"CID_GPS_Location_Request  send 'F'ailure ack  - because we don't support it") ;
				send_2101_ACK(header,'F',minion);

				// send 2113 GPS Location
				break;

			    case CID_Shutdown:
				DCMSG(BLUE,"CID_Shutdown...shutting down") ; 
				//			    doShutdown();
				break;
			    case CID_Sleep:
				DCMSG(BLUE,"CID_Sleep...sleeping") ; 
				//			    doSleep();
				break;
			    case CID_Wake:
				DCMSG(BLUE,"CID_Wake...waking") ; 
				//			    doWake();
				break;
			}   

			break;
		}

	    } else {
		perror("socket closed?");
		DCMSG(BLUE,"MINION %d: read_FASIT_msg returned %d", minion->mID,result);
		DCMSG(BLUE,"MINION %d: which means it likely has closed!", minion->mID);
		exit(-1);
	    }
	    
	}

	/**************   end of rcc command parsing   ****************/

	/**  first we just update our counter, and if less then a tenth of a second passed,
	 **  skip the timer updates and reloop, using a new select value that is a fraction of a tenth
	 **
	 **/

	clock_gettime(CLOCK_MONOTONIC_RAW,&elapsed_time);	// get a current time

	elapsed_time.tv_sec-=start_time.tv_sec;	// get the seconds right
	if (elapsed_time.tv_nsec<start_time.tv_nsec){
	    elapsed_time.tv_sec--;		// carry a second over for subtracting
	    elapsed_time.tv_nsec+=1000000000L;	// carry a second over for subtracting
	}
	elapsed_time.tv_nsec-=start_time.tv_nsec;	// get the useconds right
//	DCMSG( MAGENTA," %08ld.%09ld   timestamp" ,elapsed_time.tv_sec, elapsed_time.tv_nsec);

	/***   if the elapsed_time is greater than a tenth of a second,
	 ***   subtract out the number of tenths to use in updating our
	 ***   timers.    otherwise set up the timeout for the next select
	 ***   to be the remaining portion of a whole tenth of a second and loop
	 ***/

	elapsed_tenths = elapsed_time.tv_sec*10+(elapsed_time.tv_nsec/100000000L);

	if (elapsed_tenths==0){
	    // set the next timeout
	    timeout.tv_sec=0;
	    timeout.tv_usec=(100000000L-elapsed_time.tv_nsec)*1000;
	    
	} else {
	    // set the next timeout
	    timeout.tv_sec=0;
	    timeout.tv_usec=100000;

	    /**********    now update all the time related bits
	     **
	     **  we reached this point because either there was a RCC or MCP communication,
	     **  or a timeout on that select which started at 100ms (one tenth of a second)
	     **
	     **  so we now run through the flags of all the state varibles,
	     **  and update them if they were waiting on a timer
	     **
	     **  each state variable has a timer for physical reaction time simulation.
	     **    each one of those is up to 64k (tenths of a second)
	     **
	     **  and the entire minion should have a timer to keep track of when the actual
	     **  slave it is simulating last responded - or something
	     **
	     **  */

	    
	    // if there is a flag set, we then need to do something
	    if (minion->S.exp.flags!=F_exp_ok) {

		if (minion->S.exp.timer>elapsed_tenths){
		    minion->S.exp.timer-=elapsed_tenths;	// not timed out.  but decrement out timer

		} else {	// timed out,  move to the next state, do what it should.

		    switch (minion->S.exp.flags) {
			
			case F_exp_expose_A:
			    minion->S.exp.data=45;	// make the current positon in movement
			    minion->S.exp.flags=F_exp_expose_B;	// something has to happen
			    minion->S.exp.timer=5;	// it should happen in this many deciseconds
			    DCMSG( MAGENTA,"exp_A %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n"
				   ,elapsed_time.tv_sec, (int)(elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
			    sendStatus2102(0,header,minion); // forces sending of a 2102

			    break;

			case F_exp_expose_B:

			    minion->S.exp.data=90;	// make the current positon in movement
			    minion->S.exp.flags=F_exp_expose_C;	// we have reached the exposed position, now wait for confirmation from the RF slave
			    minion->S.exp.timer=9000;	// lots of time to wait for a RF reply
			    DCMSG( MAGENTA,"exp_B %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n"
				   ,elapsed_time.tv_sec, (int)(elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
//			    sendStatus2102(0,header,minion); // forces sending of a 2102

			    break;

			case F_exp_expose_C:

			    // it timed

			    break;

			case F_exp_conceal_A:
			    
			    minion->S.exp.data=45;	// make the current positon in movement
			    minion->S.exp.flags=F_exp_conceal_B;	// something has to happen
			    minion->S.exp.timer=5;	// it should happen in this many deciseconds
			    DCMSG( MAGENTA,"conceal_A %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n"
				   ,elapsed_time.tv_sec, (int)(elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
			    sendStatus2102(0,header,minion); // forces sending of a 2102

			    break;

			case F_exp_conceal_B:

			    minion->S.exp.data=0;	// make the current positon in movement
			    minion->S.exp.flags=F_exp_conceal_C;	// we have reached the exposed position, now wait for confirmation from the RF slave
			    minion->S.exp.timer=9000;	// lots of time to wait for a RF reply
			    DCMSG( MAGENTA,"conceal_B %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n"
				   ,elapsed_time.tv_sec, (int)(elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
			    sendStatus2102(0,header,minion); // forces sending of a 2102

			    break;

			case F_exp_conceal_C:

			    break;

		    }
		}

	    }
	}
    }
}