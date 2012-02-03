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

int resp_num,resp_seq;		// global for now.  not happy
struct timespec istart_time;

const int cal_table[16] = {0xFFFFFFFF,333,200,125,75,60,48,37,29,22,16,11,7,4,2,1};


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
    char hbuf[200];

    //read the RCC
    msglen=read(minion->rcc_sock, buf, bufsize);
    if (msglen > 0) {
	buf[msglen]=0;

	sprintf(hbuf,"MINION %d received %d from RCC       ", minion->mID,msglen);
	DCMSG_HEXB(BLUE,hbuf,buf,msglen);

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
    char hbuf[100];
    // the goal here is to use writev so the header and body don't get seperated
    iov[0].iov_base=hdr;
    iov[0].iov_len=hlen;
    iov[1].iov_base=msg;
    iov[1].iov_len=mlen;

    result=writev(minion->rcc_sock,iov,2);
    if (result >= 0){
	sprintf(hbuf,"MINION %d sent %d chars header to RCC",minion->mID,iov[0].iov_len);
	DCMSG_HEXB(BLUE,hbuf,iov[0].iov_base,iov[0].iov_len);
	sprintf(hbuf,"MINION %d sent %d chars body to RCC  ",minion->mID,iov[1].iov_len);	
	DCMSG_HEXB(BLUE,hbuf,iov[1].iov_base,iov[1].iov_len);

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
    if (force==1) {
//	msg.response.rnum = htons(hdr->num);	//  pulls the message number from the header  (htons was wrong here)
	msg.response.rnum = hdr->num;	//  pulls the message number from the header  (htons was wrong here)
	msg.response.rseq = hdr->seq;
    } else if (force==2) {
	msg.response.rnum = resp_num;	//  pulls the message number from the header  (htons was wrong here)
	msg.response.rseq = resp_seq;
    } else if (force==0) {
	msg.response.rnum = 0;	// unsolicited
	msg.response.rseq = 0;
    }
    // exposure
    // instead of obfuscating the 0,45,and 90 values, just use them.

    msg.body.exp = minion->S.exp.data;	// think we should be sending newdata in some cases

    // device type
    msg.body.type = 1; // SIT. TODO -- SIT vs. SAT vs. HSAT

    //   DCMSG(YELLOW,"before  doHits(-1)   hits = %d",hits) ;    
    //    doHits(-1);  // request the hit count
    //   DCMSG(YELLOW,"retrieved hits with doHits(-1) and setting to %d",hits) ; 

    // hit record
    msg.body.hit = htons(minion->S.hit.newdata);    
    msg.body.hit_conf.on = minion->S.on.newdata;
    msg.body.hit_conf.react = minion->S.react.newdata;
    msg.body.hit_conf.tokill = htons(minion->S.tokill.newdata);

    // use lookup table, as our sensitivity values don't match up to FASIT's
    //    for (int i=15; i>=0; i--) { // count backwards from most sensitive to least
    //	if (lastHitCal.sensitivity <= cal_table[i]) { // map our cal value to theirs
    msg.body.hit_conf.sens = htons(minion->S.sens.newdata);	// htons(i); // found sensitivity value
    msg.body.hit_conf.mode = minion->S.mode.newdata;

    //    msg->body.hit_conf.burst = htons(lastHitCal.seperation); // burst seperation
    //    msg->body.hit_conf.mode = lastHitCal.type; // single, etc.

    DCMSG(BLUE,"sending a 2102 status response");
    DCMSG(BLUE,"M-Num | ICD-v | seq-# | rsrvd | length  R-num  R-seq          <--- Header\n %6d  %d.%d  %6d  %6d %7d %6d %7d "
	  ,htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htonl(hdr->seq),htonl(hdr->rsrvd),htons(hdr->length),htons(msg.response.rnum),htonl(msg.response.rseq));
    DCMSG(BLUE,\
	  "PSTAT | Fault | Expos | Aspct |  Dir | Move |  Speed  | POS | Type | Hits | On/Off | React | ToKill | Sens | Mode | Burst\n"\
	  "  %3d    %3d     %3d     %3d     %3d    %3d    %6.2f    %3d   %3d    %3d      %3d     %3d      %3d     %3d    %3d    %3d ",
	  msg.body.pstatus,msg.body.fault,msg.body.exp,msg.body.asp,msg.body.dir,msg.body.move,msg.body.speed,msg.body.pos,msg.body.type,htons(msg.body.hit),
	  msg.body.hit_conf.on,msg.body.hit_conf.react,htons(msg.body.hit_conf.tokill),htons(msg.body.hit_conf.sens),msg.body.hit_conf.mode,htons(msg.body.hit_conf.burst));

    write_FASIT_msg(minion,hdr,sizeof(FASIT_header),&msg,sizeof(FASIT_2102));

}

// create and send a status messsage to the FASIT server
void sendStatus2112(int force, FASIT_header *hdr,thread_data_t *minion) {
    struct iovec iov[2];
    FASIT_2112 msg;
    int result;

    defHeader(2112, hdr,minion->seq); // sets the sequence number and other data
    hdr->length = htons(sizeof(FASIT_header) + sizeof(FASIT_2112));

    // fill message
    // start with zeroes
    memset(&msg, 0, sizeof(FASIT_2112));

    // fill out as response
    if (force==1) {
//	msg.response.rnum = htons(hdr->num);	//  pulls the message number from the header  (htons was wrong here)
	msg.response.rnum = hdr->num;	//  pulls the message number from the header  (htons was wrong here)
	msg.response.rseq = hdr->seq;
    } else if (force==2) {
	msg.response.rnum = resp_num;	//  pulls the message number from the header  (htons was wrong here)
	msg.response.rseq = resp_seq;
    } else if (force==0) {
	msg.response.rnum = 0;	// unsolicited
	msg.response.rseq = 0;
    }

    msg.response.rnum = htons(2110);	// we are responding to 2110
    
    msg.body.on = minion->S.mfs_on.newdata;
    msg.body.mode = minion->S.mfs_mode.newdata;
    msg.body.idelay = minion->S.mfs_idelay.newdata;
    msg.body.rdelay = minion->S.mfs_rdelay.newdata;

    DCMSG(BLUE,"sending a 2112 status response");
    DCMSG(BLUE,"M-Num | ICD-v | seq-# | rsrvd | length  R-num  R-seq          <--- Header\n %6d  %d.%d  %6d  %6d %7d %6d %7d "
	  ,htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htonl(hdr->seq),htonl(hdr->rsrvd),htons(hdr->length),htons(msg.response.rnum),htonl(msg.response.rseq));
    DCMSG(BLUE,\
	  "   ON | Mode  | idelay| rdelay\n"\
	  "  %3d    %3d     %3d     %3d   ",
	  msg.body.on,msg.body.mode,msg.body.idelay,msg.body.rdelay)
    
    write_FASIT_msg(minion,hdr,sizeof(FASIT_header),&msg,sizeof(FASIT_2112));

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

    DCMSG(BLUE,"Minion %d sending 2101 ACK \"%c\"",minion->mID,response);

    // build the response - some CID's just reply 2101 with 'S' for received and complied 
    // and 'F' for Received and Cannot comply
    // other Command ID's send other messages

    defHeader(2101, &rhdr,minion->seq); // sets the sequence number and other data
    rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2101));

    // set response
    rmsg.response.rnum = hdr->num;	//  pulls the message number from the header  (htons was wrong here)
    rmsg.response.rseq = hdr->seq;		

    rmsg.body.resp = response;	// The actual response code 'S'=can do, 'F'=Can't do
    //    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htons(rhdr.seq),htons(rhdr.rsrvd),htons(rhdr.length));
    //    DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nR-NUM | R-Seq | Response\n%5d  %6d  '%c'",
    //	  htons(rmsg.response.rnum),htons(rmsg.response.rseq),rmsg.body.resp);

    write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&rmsg,sizeof(FASIT_2101));
    return 0;
}

/********
 ********
 ********
 ********       moved the message handling to this function so multiple packets can be handled cleanly
 ********
 ********
 ********/

int handle_FASIT_msg(thread_data_t *minion,char *buf, int packetlen){

    FASIT_header *header;
    FASIT_header rhdr;
    FASIT_2111	 msg;
    FASIT_2100	 *message_2100;
    FASIT_2110	 *message_2110;
    FASIT_13110	 *message_13110;

    // map header and body for both message and response
    header = (FASIT_header*)(buf);
    DCMSG(BLUE,"MINION %d: Handle_FASIT_msg recieved fasit packet num=%d length=%d packetlen=%d",minion->mID,htons(header->num),htons(header->length),packetlen);

    // now we need to parse and respond to the message we just recieved
    // we have received a message from the mcp, process it
    // it is either a command, or it is an RF response from our slave

    switch (htons(header->num)) {
	case 100:

	    // message 100 is responded with a 2111 as in sit_client.cpp
	    // build a response here

	    defHeader(2111,&rhdr,minion->seq++);	// fills in the default values for the response header 
	    rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2111));

	    // set response
	    msg.response.rnum = htons(100);
	    msg.response.rseq = rhdr.seq;

	    // fill message
	    msg.body.devid = minion->devid; // devid = MAC address

	    // get the capability flags from the cap field

	    /** we could just copy them all over, but this is obvious  **/
	    msg.body.flags = 0; // Zero the flags
	    if (minion->S.cap&PD_NES){
		msg.body.flags |= PD_NES;
	    }
	    if (minion->S.cap&PD_GPS){
		msg.body.flags |= PD_GPS;
	    }
	    if (minion->S.cap&PD_MILES){
		msg.body.flags |= PD_MILES;
	    }

	    DCMSG(RED,"Prepared to send 2111 device capabilites message response to a 100:");
	    DCMSG(BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n"\
		  "%4d    %d.%d     %5d    %3d     %3d"
		  ,htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htons(rhdr.seq),htons(rhdr.rsrvd),htons(rhdr.length));
	    DCMSG(BLUE,"\t\t\t\t\t\t\tmessage body\n Device ID (mac address backwards) | flag_bits == GPS=4,Muzzle Flash=2,MILES Shootback=1\n0x%8.8llx           0x%2x"
		  ,msg.body.devid,msg.body.flags);

	    write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&msg,sizeof(FASIT_2111));
	    break;

	case 2100:
	    message_2100 = (FASIT_2100*)(buf + sizeof(FASIT_header));

	    DCMSG(BLUE,"MINION %d: fasit packet 2100, CID=%d", minion->mID,message_2100->cid);

	    // save response numbers
	    resp_num = header->num; //  pulls the message number from the header  (htons was wrong here)
	    resp_seq = header->seq;

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
#if 1
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
		    // save response numbers
		    resp_num = header->num; //  pulls the message number from the header  (htons was wrong here)
		    resp_seq = header->seq;

		    sendStatus2102(1,header,minion); // forces sending of a 2102
		    // AND/OR? send 2115 MILES shootback status if supported
		    //			    if (acc_conf.acc_type == ACC_NES_MFS){
		    //				DCMSG(BLUE,"we also seem to have a MFS Muzzle Flash Simulator - TODO send 2112 status eventually") ; 
		    //			    }
		    if (minion->S.cap&PD_NES){
			DCMSG(RED,"we also seem to have a MFS Muzzle Flash Simulator - send 2112 status") ; 
			// AND/OR? send 2112 Muzzle Flash status if supported   
			sendStatus2112(2,header,minion); // forces sending of a 2112, 2 means use saved resp and seq numbers
		    }
		    break;

		case CID_Expose_Request:
		    DCMSG(BLUE,"CID_Expose_Request  send 'S'uccess ack.   message_2100->exp=%d",message_2100->exp) ;       

		    //				minion->S.exp.data=0;			// cheat and set the current state to 45
		    minion->S.exp.newdata=message_2100->exp;	// set newdata to be the future state
		    if (message_2100->exp==90){
			minion->S.exp.flags=F_exp_expose_A;	// start it moving to expose
		    } else if (message_2100->exp==00){
			minion->S.exp.flags=F_exp_conceal_A;	// start it moving to conceal
		    }
		    minion->S.exp.timer=5;

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
#if 0
		    //				lastHitCal.seperation = 250;   //250;
		    minion->S.burst.newdata = htons(250);
		    minion->S.burst.flags |= F_tell_RF;	// just note it was set

		    minion->S.sens.newdata = cal_table[13]; // fairly sensitive, but not max
		    minion->S.sens.flags |= F_tell_RF;	// just note it was set

		    //lastHitCal.blank_time = 50; // half a second blanking

		    //lastHitCal.enable_on = BLANK_ALWAYS; // hit sensor off
		    minion->S.on.newdata  = 0;	// set the new value for 'on'
		    minion->S.on.flags  |= F_tell_RF;	// just note it was set

		    //lastHitCal.hits_to_kill = 1; // kill on first hit
		    minion->S.tokill.newdata = htons(1);
		    minion->S.tokill.flags |= F_tell_RF;	// just note it was set

		    //lastHitCal.after_kill = 0; // 0 for stay down
		    minion->S.react.newdata = 0; // 0 for stay down
		    minion->S.react.flags |= F_tell_RF;	// just note it was set

		    //lastHitCal.type = 1; // mechanical sensor
		    //lastHitCal.invert = 0; // don't invert sensor input line
		    //lastHitCal.set = HIT_OVERWRITE_ALL;    // nothing will change without this
		    //doHitCal(lastHitCal); // tell kernel
		    //doHits(0); // set hit count to zero

		    //minion->S.mode.newdata = message_2100->mode;
		    //minion->S.mode.flags |= F_tell_RF;	// just note it was set
#endif
		    break;

		case CID_Move_Request:
		    // send 2101 ack  (2102's will be generated at start and stop of actuator)
		    DCMSG(BLUE,"CID_Move_Request  send 'S'uccess ack.   TODO send the move to the kernel?") ;        
		    send_2101_ACK(header,'S',minion);
		    break;

		case CID_Config_Hit_Sensor:
		    DCMSG(BLUE,"CID_Config_Hit_Sensor  send a 2102 in response") ;
#if 1		
		    DCMSG(CYAN,"Full message decomposition....");
		    DCMSG(CYAN,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d"
			  ,htons(header->num),htons(header->icd1),htons(header->icd2),htons(header->seq),htons(header->rsrvd),htons(header->length));

		    DCMSG(CYAN,"\t\t\t\t\t\t\tmessage body\n"\
			  "C-ID | Expos | Aspct |  Dir | Move |  Speed | On/Off | Hits | React | ToKill | Sens | Mode | Burst\n"\
			  "%3d    %3d     %3d     %2d    %3d    %7.2f     %4d     %2d     %3d     %3d     %3d    %3d   %5d "
			  ,message_2100->cid,message_2100->exp,message_2100->asp,message_2100->dir,message_2100->move,message_2100->speed
			  ,message_2100->on,htons(message_2100->hit),message_2100->react,htons(message_2100->tokill),htons(message_2100->sens)
			  ,message_2100->mode,htons(message_2100->burst));
#endif				
		    //      actually Riptide says that the FASIT spec is wrong and should not send an ACK here
		    //				send_2101_ACK(header,'S',minion); // FASIT Cert seems to complain about this ACK

		    // send 2102 status - after doing what was commanded
		    // which is setting the values in the hit_calibration structure
		    // uses the lastHitCal so what we set is recorded
		    // there are fields that don't match up with the RF slaves, but we will match up our states
		    // TODO I believe a 2100 config hit sensor is supposed to set the hit count
		    // BDR:   I think the lastHitCal won't be needed as that is stuff on the lifter board, and we
		    //        won't need to simulate at that low of a level.

		    minion->S.on.newdata  = message_2100->on;	// set the new value for 'on'
		    minion->S.on.flags  |= F_tell_RF;	// just note it was set

		    if (htons(message_2100->burst)) {
			minion->S.burst.newdata = htons(message_2100->burst);      // spec says we only set if non-Zero
			minion->S.burst.flags |= F_tell_RF;	// just note it was set
		    }
		    if (htons(message_2100->sens)) {
			minion->S.sens.newdata = htons(message_2100->sens);
			minion->S.sens.flags |= F_tell_RF;	// just note it was set
		    }
		    // remember stated value for later
		    //				fake_sens = htons(message_2100->sens);

		    if (htons(message_2100->tokill)) {
			minion->S.tokill.newdata = htons(message_2100->tokill);
			minion->S.tokill.flags |= F_tell_RF;	// just note it was set
		    }

		    //				if (htons(message_2100->hit)) {
		    minion->S.hit.newdata = htons(message_2100->hit);
		    minion->S.hit.flags |= F_tell_RF;
		    //				}
		    minion->S.react.newdata = message_2100->react;
		    minion->S.react.flags |= F_tell_RF;	// just note it was set

		    minion->S.mode.newdata = message_2100->mode;
		    minion->S.mode.flags |= F_tell_RF;	// just note it was set

		    minion->S.burst.newdata = htons(message_2100->burst);
		    minion->S.burst.flags |= F_tell_RF;	// just note it was set

		    //				doHitCal(lastHitCal); // tell kernel by calling SIT_Clients version of doHitCal
		    DCMSG(BLUE,"calling doHitCal after setting values") ;

		    // send 2102 status or change the hit count (which will send the 2102 later)
		    if (1 /*hits == htons(message_2100->hit)*/) {
			sendStatus2102(2,header,minion);  // sends a 2102 as we won't if we didn't change the the hit count
			DCMSG(BLUE,"We will send 2102 status in response to the config hit sensor command");
			
		    } else {
			//	doHits(htons(message_2100->hit));    // set hit count to something other than zero
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

	case 2110:
	    message_2110 = (FASIT_2110*)(buf + sizeof(FASIT_header));
	    resp_num = header->num; //  pulls the message number from the header  (htons was wrong here)
	    resp_seq = header->seq;

	    DCMSG(BLUE,"MINION %d: fasit packet 2110 Configure_Muzzle_Flash, seq=%d  on=%d  mode=%d  idelay=%d  rdelay=%d"
		  , minion->mID,htonl(header->seq),message_2110->on,message_2110->mode,message_2110->idelay,message_2110->rdelay);

	    // save response numbers
	    resp_num = header->num; //  pulls the message number from the header  (htons was wrong here)
	    resp_seq = header->seq;

	    // check to see if we have muzzle flash capability -  or just pretend
	    if (minion->S.cap&PD_NES){

		minion->S.mfs_on.newdata  = message_2110->on;	// set the new value for 'on'
		minion->S.mfs_on.flags  |= F_tell_RF;	// just note it was set
		minion->S.mfs_mode.newdata  = message_2110->mode;	// set the new value for 'on'
		minion->S.mfs_mode.flags  |= F_tell_RF;	// just note it was set
		minion->S.mfs_idelay.newdata  = message_2110->idelay;	// set the new value for 'on'
		minion->S.mfs_idelay.flags  |= F_tell_RF;	// just note it was set
		minion->S.mfs_rdelay.newdata  = message_2110->rdelay;	// set the new value for 'on'
		minion->S.mfs_rdelay.flags  |= F_tell_RF;	// just note it was set

		//doMFS(msg->on,msg->mode,msg->idelay,msg->rdelay);
		// when the didMFS happens fill in the 2112, force a 2112 message to be sent
		sendStatus2112(2,header,minion); // forces sending of a 2112
		//			    sendStatus2102(0,header,minion); // forces sending of a 2102
		//sendMFSStatus = 1; // force
	    } else {
		send_2101_ACK(header,'F',minion);  // no muzzle flash capability, so send a negative ack
	    }


    }  /**   end of 'switch (packet_num)'   **/
}


/*********************                                  minion_thread                               *************
 *********************
 *********************   Each minion thread is started by the MCP, and is pretending to be a lifter
 *********************   or other PD (presentation device)
 *********************      it has it's own connection to the RCC (Range Control Computer) which is
 *********************   used for the FASIT communicaitons, and another connection to the MCP which is
 *********************   used to pass communicaitons on down through the RF to the PD
 *********************
 *********************   All RF communicaitons will pass through the MCP, which is really just forwarding them to the RF process
 *********************
 *********************   The RF process will group and ungroup the messages automatically
 *********************
 *********************   When the minion recieves a FASIT command and sets up a response that simulates the real PD's
 *********************   state, it also passes a message up to the MCP which will then send it on to the RF Process
 *********************   which in turn radios to the slave [bosses eventually].
 *********************   
 *********************/

void *minion_thread(thread_data_t *minion, int verbose){
    struct timespec elapsed_time, start_time,delta_time;
    struct timeval timeout;
    struct sockaddr_in address;
    int sock_ready,error;
    long elapsed_tenths;

    char buf[BufSize];
    char *tbuf;
    char mbuf[BufSize];
    int i,msglen,result,seq,length;
    fd_set rcc_or_mcp;

    struct iovec iov[2];

    FASIT_header *header;
    FASIT_header rhdr;
    FASIT_2111	 msg;
    FASIT_2100	 *message_2100;
    FASIT_2110	 *message_2110;
    FASIT_13110	 *message_13110;


    initialize_state( &minion->S);


/*** actually, somehow the capabilities and the devid's will come from the MCP -
 *** probably after it looks at a setup file or does a global RF capability message
 ***
 ***   For now we just make up the capability  - the devid was already passed down by the MCP
 ***   and really just matches the minion ID right now
 ***
 ***/

    
    minion->S.cap|=PD_NES;	// add the NES capability
    
    DCMSG(BLUE,"MINION %d state is initialized as devid %lld", minion->mID,htonll(minion->devid));
    if (minion->S.cap&PD_NES) {
	DCMSG(BLUE,"MINION %d has Night Effects Simulator (NES) capability", minion->mID);
    }
    if (minion->S.cap&PD_MILES) {
	DCMSG(BLUE,"MINION %d has MILES (shootback) capability", minion->mID);
    }
    if (minion->S.cap&PD_GPS) {
	DCMSG(BLUE,"MINION %d has Global Positioning System (GPS) capability", minion->mID);
    }

    i=0;

    // the mcp is expecting a response
#if 1
    i++;
    sprintf(mbuf,"msg %d I am minion %d with devid %lld", i,minion->mID,htonll(minion->devid));

    result=write(minion->mcp_sock, mbuf, strlen(mbuf));
    if (result >= 0){
	DCMSG(BLUE,"MINION %d sent %d chars to MCP   --%s--",minion->mID,strlen(mbuf),mbuf);
    } else {
	perror("writing stream message");
    }

    msglen=read(minion->mcp_sock, buf, 1023);
    if (msglen > 0) {
	buf[msglen]=0;
	DCMSG(BLUE,"MINION %d received from MCP %d chars-->%s<--", minion->mID,msglen,buf);
    } else if (!msglen) {
	perror("reading stream message");
    } else {
	DCMSG(BLUE,"MINION %d socket to MCP closed, we are FREEEEEeeee!!!", minion->mID);
    }
#endif

    // now we must get a connection to the range control
    // computer (RCC) using fasit.

    minion->rcc_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(minion->rcc_sock < 0)   {
	perror("socket() failed");
    }

    /* start with a clean address structure */
    memset(&address, 0, sizeof(struct sockaddr_in));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("192.168.10.203");	// these need to be arguments, or something other than hard code
    address.sin_port = htons(4000);				// same here

    result=connect(minion->rcc_sock,(struct sockaddr *) &address, sizeof(struct sockaddr_in));
    if (result<0){
	strerror_r(errno,buf,BufSize);
	DCMSG(BLUE,"MINION %d fasit server not found! connect(...,%s:%d,...) error : %s  ", minion->mID,inet_ntoa(address.sin_addr),htons(address.sin_port),buf);
	exit(-1);
    }

    // we now have a socket.

    DCMSG(BLUE,"MINION %d has a socket to a RCC", minion->mID);


    //   this is a handshake message sent back to the MCP informing it that we have a FASIT connection
#if 0
    sprintf(mbuf,"MINION %d msg %d I am connected to an RCC",minion->mID,i);
    result=write(minion->mcp_sock, mbuf, strlen(mbuf));
    error=errno;
    if (result >= 0){
	DCMSG(BLUE,"MINION %d sent %d chars to MCP   --%s--",minion->mID,strlen(mbuf),mbuf);
    } else {
	strerror_r(errno,buf,BufSize);
	DCMSG(BLUE,"MINION %d   Error writing message to MCP : %s  ", minion->mID,buf);
	exit(-1);
    }

#endif

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

    clock_gettime(CLOCK_MONOTONIC_RAW,&istart_time);	// get the intial current time

    while(1) {

	timestamp(&elapsed_time,&istart_time,&delta_time);	
	if (verbose&TIME_DEBUG){
	    DCMSG(CYAN,"MINION %d: Top of main loop at %08ld.%09ld timestamp, delta=%08ld.%09ld",minion->mID,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
	}
	
	clock_gettime(CLOCK_MONOTONIC_RAW,&start_time);	// mark the start time so we can run the timers

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
	    exit(-1);
	}

	timestamp(&elapsed_time,&istart_time,&delta_time);	
	if (verbose&TIME_DEBUG){
	    DCMSG(CYAN,"MINION %d:  After Select at %08ld.%09ld timestamp, delta=%08ld.%09ld",minion->mID,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
	}
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

	timestamp(&elapsed_time,&istart_time,&delta_time);	
	if (verbose&TIME_DEBUG){
	    DCMSG(CYAN,"MINION %d: End of MCP Parse at %08ld.%09ld timestamp, delta=%08ld.%09ld",minion->mID,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
	}
	/**********************************    end of reading and processing the mcp command  ***********************/
	/*************** check to see if there is something to read from the rcc   **************************/
	if (FD_ISSET(minion->rcc_sock,&rcc_or_mcp)){
	    // now read it using the new routine    
	    result = read_FASIT_msg(minion,buf, BufSize);
	    if (result>0){		
		header = (FASIT_header*)(buf);	// find out how long of message we have
		length=htons(header->length);	// set the length for the handle function
		if (result>length){
		    DCMSG(BLUE,"MINION %d: Multiple Packet  num=%d  result=%d seq=%d header->length=%d",minion->mID,htons(header->num),result,htons(header->seq),length);
		} else {
		    DCMSG(BLUE,"MINION %d:  num=%d  result=%d seq=%d header->length=%d",minion->mID,htons(header->num),result,htons(header->seq),length);
		}
		tbuf=buf;			// use our temp pointer so we can step ahead
		// loop until result reaches 0
		while((result>=length)&&(length>0)) {
		    timestamp(&elapsed_time,&istart_time,&delta_time);	
		    if (verbose&TIME_DEBUG){
			DCMSG(CYAN,"MINION %d:  Packet %d recieved at %08ld.%09ld timestamp, delta=%08ld.%09ld"
			      ,minion->mID,htons(header->num),elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
		    }
		    handle_FASIT_msg(minion,tbuf,length);
		    result-=length;			// reset the length to handle a possible next message in this packet
		    tbuf+=length;			// step ahead to next message
		    header = (FASIT_header*)(tbuf);	// find out how long of message we have
		    length=htons(header->length);	// set the length for the handle function
		    if (result){
			DCMSG(BLUE,"MINION %d: Continue processing the rest of the BIG fasit packet num=%d  result=%d seq=%d  length=%d",minion->mID,htons(header->num),result,htons(header->seq),length);
		    }
		}
	    } else {
		strerror_r(errno,buf,BufSize);
		DCMSG(BLUE,"MINION %d: read_FASIT_msg returned %d and Error: %s", minion->mID,result,buf);
		DCMSG(BLUE,"MINION %d: which means it likely has closed!", minion->mID);
		exit(-1);
	    }
	}
	timestamp(&elapsed_time,&istart_time,&delta_time);	
	if (verbose&TIME_DEBUG){
	    DCMSG(CYAN,"MINION %d: End of RCC Parse at %08ld.%09ld timestamp, delta=%08ld.%09ld"
		  ,minion->mID,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
	}
	/**************   end of rcc command parsing   ****************/
	/**  first we just update our counter, and if less then a tenth of a second passed,
	 **  skip the timer updates and reloop, using a new select value that is a fraction of a tenth
	 **
	 **/
	
	timestamp(&elapsed_time,&istart_time,&delta_time);	
	if (verbose&TIME_DEBUG){
	    DCMSG(CYAN,"MINION %d: Begin timer updates at %08ld.%09ld timestamp, delta=%08ld.%09ld"
		  ,minion->mID,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
	}
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
	}
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
			DCMSG(MAGENTA,"exp_A %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n"
			      ,elapsed_time.tv_sec, (int)(elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
			sendStatus2102(0,header,minion); // forces sending of a 2102

			break;

		    case F_exp_expose_B:

			minion->S.exp.data=90;	// make the current positon in movement
			minion->S.exp.flags=F_exp_expose_C;	// we have reached the exposed position, now wait for confirmation from the RF slave
			minion->S.exp.timer=9000;	// lots of time to wait for a RF reply
			DCMSG(MAGENTA,"exp_B %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n"
			      ,elapsed_time.tv_sec, (int)(elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
			sendStatus2102(0,header,minion); // forces sending of a 2102

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

	timestamp(&elapsed_time,&istart_time,&delta_time);	
	if (verbose&TIME_DEBUG){
	    DCMSG(CYAN,"MINION %d: End timer updates at %08ld.%09ld timestamp, delta=%08ld.%09ld"
		  ,minion->mID,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
	}
    }
}


