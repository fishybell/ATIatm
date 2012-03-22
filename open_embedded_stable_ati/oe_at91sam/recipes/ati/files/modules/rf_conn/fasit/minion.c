#include "mcp.h"
#include "rf.h"
#include "fasit_c.h"

#define S_set(ITEM,D,ND,F,T) \
    { \
	S->ITEM.data = D; \
	S->ITEM.newdata = ND; \
	S->ITEM.flags = F; \
	S->ITEM.timer = T; \
    }

#undef CLOCK_MONOTONIC_RAW 
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC

int resp_num,resp_seq;		// global for now.  not happy

const int cal_table[16] = {0xFFFFFFFF,333,200,125,75,60,48,37,29,22,16,11,7,4,2,1};

// globals that get inherited from the parent (MCP)
extern int verbose;
extern struct sockaddr_in fasit_addr;


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
void defHeader(FASIT_header *rhdr,int mnum,int seq,int length){
    rhdr->num = htons(mnum);
    rhdr->icd1 = htons(1);
    rhdr->icd2 = htons(1);
    rhdr->rsrvd = htonl(0);
    rhdr->seq = htonl(seq);
    rhdr->length = htons(sizeof(FASIT_header) + length);
    switch (mnum) {
	case 100:
	case 2000:
	case 2004:
	case 2005:
	case 2006:
	    rhdr->icd1 = htons(2);
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

	if (verbose&D_PACKET){	// don't do the sprintf if we don't need to
	    sprintf(hbuf,"minion %d: received %d from RCC        ", minion->mID,msglen);
	    DDCMSG_HEXB(D_PACKET,BLUE,hbuf,buf,msglen);
	}

	header=(FASIT_header *)buf; 
	DDCMSG(D_PACKET,BLUE,"minion %d: FASIT packet, cmd=%d ICD=%d.%d seq=%d len=%d",
	       minion->mID,htons(header->num),htons(header->icd1),htons(header->icd2),htonl(header->seq),htons(header->length));

	// we will return to the caller with the msglen, and they can look at the buffer and parse the message and respond to it
    } else if (msglen<0) {
	if (errno!=EAGAIN){
	    DDCMSG(D_PACKET,RED,"minion %d: read of RCC socket returned %d errno=%d socket to RCC closed",minion->mID, msglen,errno);
	    return(-1);  /* this minion does not die!     */
	} else {
	    DDCMSG(D_PACKET,RED,"minion %d: socket to RCC closed, !!!", minion->mID);
	    return(-1);  /* this minion doesn't die!    */
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
    if ((verbose&D_PACKET)&&(result >= 0)){	// don't do the sprintf if we don't need to
	sprintf(hbuf,"minion %d: sent %d chars header to RCC ",minion->mID,(int)iov[0].iov_len);
	DDCMSG_HEXB(D_PACKET,BLUE,hbuf,iov[0].iov_base,iov[0].iov_len);
	sprintf(hbuf,"minion %d: sent %d chars body to RCC   ",minion->mID,(int)iov[1].iov_len);	
	DDCMSG_HEXB(D_PACKET,BLUE,hbuf,iov[1].iov_base,iov[1].iov_len);

    }
    if (result<0) {
	perror("write_FASIT_msg:   error writing stream message");
    }
    return(result);
}

// create and send a status messsage to the FASIT server
void sendStatus2102(int force, FASIT_header *hdr,thread_data_t *minion) {
    struct iovec iov[2];
    FASIT_header rhdr;
    FASIT_2102 msg;
    int result;

    // sets the sequence number and other data    
    defHeader(&rhdr,2102,minion->seq++, sizeof(FASIT_2102));
    
    // fill message
    // start with zeroes
    memset(&msg, 0, sizeof(FASIT_2102));

    // fill out response
    if (force==1) {
	msg.response.resp_num = hdr->num;	//  pulls the message number from the header  (htons was wrong here)
	msg.response.resp_seq = hdr->seq;
//	msg.response.resp_num = rhdr.num;	//  pulls the message number from the header  (htons was wrong here)
//	msg.response.resp_seq = htonl(minion->seq-1);
    } else if (force==0) {
	    msg.response.resp_num = 0;	// unsolicited
	    msg.response.resp_seq = 0;
    }
    DDCMSG(D_PACKET,RED,"2102 status response hdr->num=%d, hdr->seq=%d",
	   htons(msg.response.resp_num),htonl(msg.response.resp_num));
    // device type
    msg.body.type = minion->S.dev_type; // should be correct by the time it sends this

    //   DCMSG(YELLOW,"before  doHits(-1)   hits = %d",hits) ;    
    //    doHits(-1);  // request the hit count
    //   DCMSG(YELLOW,"retrieved hits with doHits(-1) and setting to %d",hits) ; 

    // individual device type stuff
    switch (msg.body.type) {
        case Type_SIT:
        case Type_SAT:
        case Type_HSAT:
            // exposure
            // instead of obfuscating the 0,45,and 90 values, just use them.

            msg.body.exp = minion->S.exp.data;
            msg.body.speed = 0.0;
            break;
        case Type_MIT:
        case Type_MAT:
            msg.body.speed = minion->S.speed.data;
            msg.body.exp = minion->S.exp.data;
            msg.body.move = minion->S.move.data;
            msg.body.pos = htons(minion->S.pos.data);
            break;
    }

    // left to do:
    msg.body.pstatus = 0; // always "shore" power
    msg.body.fault = htons(minion->S.fault.data);

    // hit record
    msg.body.hit = htons(minion->S.hit.newdata); // care must be taken that this is the correct value, as it is the most important value
    msg.body.hit_conf.on = minion->S.on.newdata;
    msg.body.hit_conf.react = minion->S.react.newdata;
    msg.body.hit_conf.tokill = htons(minion->S.tokill.newdata);
    msg.body.hit_conf.burst = htons(minion->S.burst.newdata);
    msg.body.hit_conf.sens = htons(minion->S.sens.newdata);
    msg.body.hit_conf.mode = minion->S.mode.newdata;

    DDCMSG(D_PACKET,BLUE,"sending a 2102 status response");
    DDCMSG(D_PACKET,BLUE,"M-Num | ICD-v | seq-# | rsrvd | length  R-num  R-seq          <--- Header\n %6d  %d.%d  %6d  %6d %7d %6d %7d "
	  ,htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htonl(rhdr.seq),htonl(rhdr.rsrvd),htons(rhdr.length),htons(msg.response.resp_num),htonl(msg.response.resp_seq));
    DDCMSG(D_PACKET,BLUE,\
	  "PSTAT | Fault | Expos | Aspct |  Dir | Move |  Speed  | POS | Type | Hits | On/Off | React | ToKill | Sens | Mode | Burst\n"\
	  "  %3d    %3d     %3d     %3d     %3d    %3d    %6.2f    %3d   %3d    %3d      %3d     %3d      %3d     %3d    %3d    %3d ",
	  msg.body.pstatus,msg.body.fault,msg.body.exp,msg.body.asp,msg.body.dir,msg.body.move,msg.body.speed,msg.body.pos,msg.body.type,htons(msg.body.hit),
	  msg.body.hit_conf.on,msg.body.hit_conf.react,htons(msg.body.hit_conf.tokill),htons(msg.body.hit_conf.sens),msg.body.hit_conf.mode,htons(msg.body.hit_conf.burst));

    write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&msg,sizeof(FASIT_2102));

}

// create and send a status messsage to the FASIT server
void sendStatus2112(int force, FASIT_header *hdr,thread_data_t *minion) {
    struct iovec iov[2];
    FASIT_header rhdr;
    FASIT_2112 msg;
    int result;

    // sets the sequence number and other data    
    defHeader(&rhdr,2112,minion->seq++, sizeof(FASIT_2112));

    // fill message
    // start with zeroes
    memset(&msg, 0, sizeof(FASIT_2112));

    // fill out as response
    if (force==1) {
	msg.response.resp_num = hdr->num;	//  pulls the message number from the header that we are responding to
	msg.response.resp_seq = hdr->seq;
    } else if (force==0) {
	msg.response.resp_num = 0;	// unsolicited
	msg.response.resp_seq = 0;
    }

    // not sure if this line is right, though
//    msg.response.resp_num = htons(2110);	    // the old code had this and seemed to work better
    
    msg.body.on = minion->S.mfs_on.newdata;
    msg.body.mode = minion->S.mfs_mode.newdata;
    msg.body.idelay = minion->S.mfs_idelay.newdata;
    msg.body.rdelay = minion->S.mfs_rdelay.newdata;

    DDCMSG(D_PACKET,BLUE,"sending a 2112 status response");
    DDCMSG(D_PACKET,BLUE,"M-Num | ICD-v | seq-# | rsrvd | length  R-num  R-seq          <--- Header\n %6d  %d.%d  %6d  %6d %7d %6d %7d "
	   ,htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htonl(rhdr.seq),htonl(rhdr.rsrvd),htons(rhdr.length),htons(msg.response.resp_num),htonl(msg.response.resp_seq));
    DDCMSG(D_PACKET,BLUE,\
	  "   ON | Mode  | idelay| rdelay\n"\
	  "  %3d    %3d     %3d     %3d   ",
	  msg.body.on,msg.body.mode,msg.body.idelay,msg.body.rdelay)
    
    write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&msg,sizeof(FASIT_2112));

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

    DDCMSG(D_PACKET,BLUE,"minion %d: sending 2101 ACK \"%c\"",minion->mID,response);

    // build the response - some CID's just reply 2101 with 'S' for received and complied 
    // and 'F' for Received and Cannot comply
    // other Command ID's send other messages

    // sets the sequence number and other data    
    defHeader(&rhdr,2101,minion->seq++, sizeof(FASIT_2101));
    
    // set response
    rmsg.response.resp_num = hdr->num;	//  pulls the message number from the header  (htons was wrong here)
    rmsg.response.resp_seq = hdr->seq;		

    rmsg.body.resp = response;	// The actual response code 'S'=can do, 'F'=Can't do
    //    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd |
    //    length\n%6d  %d.%d  %6d  %6d  %7d",htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htonl(rhdr.seq),htons(rhdr.rsrvd),htons(rhdr.length));
    //    DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nR-NUM | R-Seq | Response\n%5d  %6d  '%c'",
    //	  htons(rmsg.response.resp_num),htons(rmsg.response.resp_seq),rmsg.body.resp);

    write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&rmsg,sizeof(FASIT_2101));
    return 0;
}

/********
 ********
 ********       moved the message handling to this function so multiple packets can be handled cleanly
 ********
 ********   timestamp is sent in too.
 ********/

int handle_FASIT_msg(thread_data_t *minion,char *buf, int packetlen,struct timespec *elapsed_time){

    int result;
    char hbuf[100];
    
    FASIT_header *header;
    FASIT_header rhdr;
    FASIT_2111	 msg;
    FASIT_2100	 *message_2100;
    FASIT_2110	 *message_2110;
    FASIT_13110	 *message_13110;

    LB_packet_t		LB_buf;
    LB_device_reg_t	*LB_devreg;
    LB_status_req_t	*LB_status_req;
    LB_expose_t		*LB_exp;
    LB_move_t		*LB_move;
    LB_configure_t	*LB_configure;
    LB_power_control_t	*LB_power;
    LB_audio_control_t	*LB_audio;
    LB_pyro_fire_t	*LB_pyro;
    LB_qconceal_t	*LB_qcon;
    

    // map header and body for both message and response
    header = (FASIT_header*)(buf);
    DDCMSG(D_PACKET,BLUE,"MINION %d: Handle_FASIT_msg recieved fasit packet num=%d seq=%d length=%d packetlen=%d",
	   minion->mID,htons(header->num),htonl(header->seq),htons(header->length),packetlen);

    // now we need to parse and respond to the message we just recieved
    // we have received a message from the mcp, process it
    // it is either a command, or it is an RF response from our slave
    
    switch (htons(header->num)) {
	case 100:

	    // message 100 is responded with a 2111 as in sit_client.cpp
	    // build a response here
	    // sets the sequence number and other data    
	    defHeader(&rhdr,2111,minion->seq++, sizeof(FASIT_2111));
	    
	    // set response message and sequence numbers
	    msg.response.resp_num = header->num;
	    msg.response.resp_seq = header->seq;

	    // fill message
	    msg.body.devid = htonll(0x705eaa000000L+ minion->devid); // MAC address  70:5E:AA:xx.xx.xx  devid is the last 3 bytes

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

	    DDCMSG(D_PACKET,RED,"Prepared to send 2111 device capabilites message response to a 100:");
	    DDCMSG(D_PACKET,BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n"\
		   "%4d    %d.%d     %5d    %3d     %3d"
		   ,htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htonl(rhdr.seq),htons(rhdr.rsrvd),htons(rhdr.length));
	    DDCMSG(D_PACKET,BLUE,"\t\t\t2111 message body\n Device ID (mac address backwards)   flag_bits == GPS=4,Muzzle Flash=2,MILES Shootback=1\n0x%8.8llx                    0x%2x"
		   ,(long long int)msg.body.devid,msg.body.flags);
	    write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&msg,sizeof(FASIT_2111));
	    break;

	case 2100:
	    message_2100 = (FASIT_2100*)(buf + sizeof(FASIT_header));

	    DDCMSG(D_PACKET,BLUE,"MINION %d: fasit packet 2100, CID=%d", minion->mID,message_2100->cid);

	    // Just parse out the command for now and print a pretty message
	    switch (message_2100->cid) {
		case CID_No_Event:
		    DDCMSG(D_PACKET,RED,"CID_No_Event") ; 
		    break;

		case CID_Reserved01:
		    DDCMSG(D_PACKET,RED,"CID_Reserved01") ;
		    break;

		case CID_Status_Request:
		    DDCMSG(D_PACKET,RED,"CID_Status_Request") ; 
		    break;

		case CID_Expose_Request:
		    DDCMSG(D_PACKET,RED,"Minion %d:CID_Expose_Request:  message_2100->exp=%d",minion->mID,message_2100->exp) ;
		    break;

		case CID_Reset_Device:
		    DDCMSG(D_PACKET,RED,"CID_Reset_Device  ") ;
		    break;

		case CID_Move_Request:
		    DDCMSG(D_PACKET,RED,"CID_Move_Request  ") ;       
		    break;

		case CID_Config_Hit_Sensor:
		    DDCMSG(D_PACKET,RED,"CID_Config_Hit_Sensor") ;                
		    break;

		case CID_GPS_Location_Request:
		    DDCMSG(D_PACKET,RED,"CID_GPS_Location_Request") ;
		    break;

		case CID_Shutdown:
		    DDCMSG(D_PACKET,RED,"CID_Shutdown") ;
		    break;
	    }

	    DDCMSG(D_VERY,CYAN,"Full message decomposition....");
	    DDCMSG(D_VERY,CYAN,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d"
		  ,htons(header->num),htons(header->icd1),htons(header->icd2),htonl(header->seq),htons(header->rsrvd),htons(header->length));

	    DDCMSG(D_VERY,CYAN,"\t\t\t\t\t\t\tmessage body\n"\
		  "C-ID | Expos | Aspct |  Dir | Move |  Speed | On/Off | Hits | React | ToKill | Sens | Mode | Burst\n"\
		  "%3d    %3d     %3d     %2d    %3d    %7.2f     %4d     %2d     %3d     %3d     %3d    %3d   %5d ",
		  message_2100->cid,message_2100->exp,message_2100->asp,message_2100->dir,message_2100->move,message_2100->speed
		  ,message_2100->on,htons(message_2100->hit),message_2100->react,htons(message_2100->tokill),htons(message_2100->sens)
		  ,message_2100->mode,htons(message_2100->burst));
	    // do the event that was requested

	    switch (message_2100->cid) {
		case CID_No_Event:
		    DDCMSG(D_PACKET,BLUE,"CID_No_Event  send 'S'uccess ack") ; 
		    // send 2101 ack
		    send_2101_ACK(header,'S',minion);
		    break;

		case CID_Reserved01:
		    // send 2101 ack
		    DDCMSG(D_PACKET,BLUE,"CID_Reserved01  send 'F'ailure ack") ;
		    send_2101_ACK(header,'F',minion);
		    break;

		case CID_Status_Request:
		    // send 2102 status
		    DDCMSG(D_PACKET,BLUE,"CID_Status_Request   send 2102 status  hdr->num=%d, hdr->seq=%d",
			   htons(header->num),htonl(header->seq));
			
		    sendStatus2102(1,header,minion); // forces sending of a 2102
		    // AND/OR? send 2115 MILES shootback status if supported
		    //			    if (acc_conf.acc_type == ACC_NES_MFS){
		    //				DCMSG(BLUE,"we also seem to have a MFS Muzzle Flash Simulator - TODO send 2112 status eventually") ; 
		    //			    }
		    if (minion->S.cap&PD_NES){
			DDCMSG(D_PACKET,BLUE,"we also seem to have a MFS Muzzle Flash Simulator - send 2112 status") ; 
			// AND send 2112 Muzzle Flash status if supported   
			sendStatus2112(0,header,minion); // send of a 2112 (0 means unsolicited, 1 copys mnum and seq to resp_mnum and resp_seq)
		    }

//		    also build an LB packet  to send
		    LB_status_req  =(LB_status_req_t *)&LB_buf;	// make a pointer to our buffer so we can use the bits right
		    LB_status_req->cmd=LBC_STATUS_REQ;
		    LB_status_req->addr=minion->RF_addr;
		    

		    minion->S.status.flags=F_told_RCC;
		    minion->S.status.timer=20;
		    // calculates the correct CRC and adds it to the end of the packet payload
		    // also fills in the length field
		    set_crc8(LB_status_req);
		    
		    // now send it to the MCP master
		    result=write(minion->mcp_sock,LB_status_req,RF_size(LB_status_req->cmd));
		    //  sent LB
		    if (verbose&D_RF){	// don't do the sprintf if we don't need to
			sprintf(hbuf,"Minion %d: LB packet to MCP address=%4d cmd=%2d msglen=%d result=%d",
				minion->mID,minion->RF_addr,LB_status_req->cmd,RF_size(LB_status_req->cmd),result);
			DDCMSG2_HEXB(D_RF,YELLOW,hbuf,LB_status_req,RF_size(LB_status_req->cmd));
		    }
		    break;

		case CID_Expose_Request:
		    DDCMSG(D_PACKET,BLACK,"Minion %d: CID_Expose_Request  send 'S'uccess ack.   message_2100->exp=%d",minion->mID,message_2100->exp);
//		    also build an LB packet  to send
		    DDCMSG(D_PACKET,BLACK,"Minion %d: decide if we are going up or going down(exp=%d)",minion->mID,message_2100->exp);
		    if (message_2100->exp==90){
			DDCMSG(D_PACKET,BLUE,"Minion %d: decide if we are going up or going down(exp=%d)",minion->mID,message_2100->exp);
			
			LB_exp =(LB_expose_t *)&LB_buf;	// make a pointer to our buffer so we can use the bits right
			
			DDCMSG(D_PACKET,BLACK,"Minion %d: we seem to work here******************* &LB_buf=%p",minion->mID,&LB_buf);

			LB_exp->cmd=LBC_EXPOSE;
			LB_exp->addr=minion->RF_addr;

//				minion->S.exp.data=0;			// cheat and set the current state to 45
			minion->S.exp.newdata=message_2100->exp;	// set newdata to be the future state
			LB_exp->expose=1;
			minion->S.exp.flags=F_exp_expose_A;	// start (faking FASIT) moving to expose

			minion->S.exp.timer=5;

			minion->S.exp.elapsed_time.tv_sec =elapsed_time->tv_sec;
			minion->S.exp.elapsed_time.tv_nsec=elapsed_time->tv_nsec;

			LB_exp->event=++minion->S.exp.event;	// fill in the event

			//  really need to fill in with the right stuff
			LB_exp->hitmode=0;
			LB_exp->tokill=0;
			LB_exp->react=0;
			LB_exp->mfs=0;
			LB_exp->thermal=0;
			// calculates the correct CRC and adds it to the end of the packet payload
			// also fills in the length field
			set_crc8(LB_exp);
			// now send it to the MCP master
			
		    } else {
			int uptime;
			// we must assume that it is a 'conceal' that followed the last expose.
			//        so now the event is over and we need to send the qconceal command
			//        and the termination of the event time and stuff
			uptime=(elapsed_time->tv_sec - minion->S.exp.elapsed_time.tv_sec)*10;	//  find uptime in tenths of a second
			uptime+=(elapsed_time->tv_nsec - minion->S.exp.elapsed_time.tv_nsec)/100000000L;	//  find uptime in tenths of a second
			
			if (uptime>0x7FF){
			    uptime=0x7ff;		// set time to max (204.7 seconds) if it was too long
			} else {
			    uptime&= 0x7ff;
			}

			DDCMSG(D_VERY,BLACK,"Minion %d: uptime=%d",minion->mID,uptime);

			minion->S.exp.newdata=0;
			minion->S.exp.flags=F_exp_conceal_A;	// start (faking FASIT) moving to conceal
			minion->S.exp.timer=5;

			DDCMSG(D_VERY,BLACK,"Minion %d: &LB_buf=%p",minion->mID,&LB_buf);			
			LB_qcon =(LB_qconceal_t *)&LB_buf;	// make a pointer to our buffer so we can use the bits right
			DDCMSG(D_VERY,BLACK,"Minion %d: map packet in",minion->mID);
			
			LB_qcon->cmd=LBC_QCONCEAL;
			LB_qcon->addr=minion->RF_addr;
			LB_qcon->event=minion->S.exp.event;
			LB_qcon->uptime=uptime;
			set_crc8(LB_qcon);
			DDCMSG(D_VERY,BLUE,"Minion %d: called set_crc8",minion->mID);

			// we also need a report for this event from the target, so we will have to send a request_rep packet soon.
			// but we don't want to send it right yet.
			//  sending the request report would need to happen not much later than the next expose command - at the latest

			minion->S.event.flags|=F_needs_report;
			minion->S.event.data=minion->S.exp.event;
			minion->S.event.timer = 15;	// lets try 1.5 seconds
			
		    }

		    DDCMSG(D_PACKET,BLUE,"Minion %d: send LB up to MCP   ---- or hang on write",minion->mID);
		    
		    result=write(minion->mcp_sock,&LB_buf,RF_size(LB_buf.cmd));
		    if (verbose&D_RF){	// don't do the sprintf if we don't need to
			sprintf(hbuf,"Minion %d:m LB packet to MCP address=%4d cmd=%2d msglen=%d result=%d",
				minion->mID,minion->RF_addr,LB_buf.cmd,RF_size(LB_buf.cmd),result);
			DDCMSG2_HEXB(D_RF,YELLOW,hbuf,&LB_buf,RF_size(LB_buf.cmd));
		    }
//  sent LB

		    // send 2101 ack  (2102's will be generated at start and stop of actuator)
		    send_2101_ACK(header,'S',minion);    // TRACR Cert complains if these are not there

		    // it should happen in this many deciseconds
		    //  - fasit device cert seems to come back immeadiately and ask for status again, and
		    //    that is causing a bit of trouble.
		    break;

		case CID_Reset_Device:
		    // send 2101 ack
		    DDCMSG(D_PACKET,BLUE,"CID_Reset_Device  send 'S'uccess ack.   set lastHitCal.* to defaults") ;
		    send_2101_ACK(header,'S',minion);
          // TODO -- this opens a serious can of worms, should probably handle via minion telling RFmaster to set the forget bit and resetting its own state

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

/* handle emergency stop the same as a move request */		    
      case CID_Stop: 

      case CID_Move_Request:
	      DDCMSG(D_PACKET,BLUE,"CID_Move_Request  send 'S'uccess ack.   message_2100->speed=%f, message_2100->move=%d",message_2100->speed, message_2100->move);
//			    also build an LB packet  to send
	      LB_move =(LB_move_t *)&LB_buf;	// make a pointer to our buffer so we can use the bits right
	      LB_move->cmd=LBC_MOVE;
	      LB_move->addr=minion->RF_addr;

//				minion->S.exp.data=0;			// cheat and set the current state to 45
	      minion->S.speed.newdata=message_2100->speed;	// set newdata to be the future state
	      minion->S.move.newdata=message_2100->move;	// set newdata to be the future state
	      minion->S.speed.timer=10; // will reach speed in this many deciseconds
          // minion->S.move.timer=10; TODO -- do we need this?

          LB_move->speed = ((int)(message_2100->speed * 100)) & 0x7ff;
	  DDCMSG(D_PACKET,BLUE,"CID_Move_Request: speed after: %i", LB_move->speed);
	  if (message_2100->move == 2) {
	      DDCMSG(D_PACKET,BLUE,"CID_Move_Request: direction 0");
	      LB_move->direction = 0;
          } else if (message_2100->move ==1) {
	      DDCMSG(D_PACKET,BLUE,"CID_Move_Request: direction 1");
	      LB_move->direction = 1;
          } else {
	      DDCMSG(D_PACKET,BLUE,"CID_Move_Request: direction 0:2");
	      LB_move->direction = 0;
	      LB_move->speed = 0;
          }
          if (message_2100->cid == CID_Stop) {
	      DDCMSG(D_PACKET,BLUE,"CID_Move_Request: E-Stop");
	      LB_move->speed = 2047; // emergency stop speed
          }
	    // calculates the correct CRC and adds it to the end of the packet payload
	    // also fills in the length field
		    set_crc8(LB_move);
	    // now send it to the MCP master
		    result=write(minion->mcp_sock,LB_move,RF_size(LB_move->cmd));
		    if (verbose&D_RF){	// don't do the sprintf if we don't need to
			sprintf(hbuf,"Minion %d: LB packet to MCP address=%4d cmd=%2d msglen=%d",minion->mID,minion->RF_addr,LB_move->cmd,RF_size(LB_move->cmd));
			DDCMSG2_HEXB(D_RF,YELLOW,hbuf,LB_move,RF_size(LB_move->cmd));
			DDCMSG(D_RF,YELLOW,"  Sent %d bytes to MCP fd=%d\n",RF_size(LB_move->cmd),minion->mcp_sock);
		    }
//  sent LB
    
		    // send 2101 ack  (2102's will be generated at start and stop of actuator)
		    send_2101_ACK(header,'S',minion);    // TRACR Cert complains if these are not there

		    // it should happen in this many deciseconds
		    //  - fasit device cert seems to come back immeadiately and ask for status again, and
		    //    that is causing a bit of trouble.		    break;
         break;

		case CID_Config_Hit_Sensor:
		    DDCMSG(D_PACKET,BLUE,"CID_Config_Hit_Sensor  send a 2102 in response") ;
		    DDCMSG(D_PACKET,BLUE,"Full message decomposition....");
		    DDCMSG(D_PACKET,BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d"
			  ,htons(header->num),htons(header->icd1),htons(header->icd2),htonl(header->seq),htons(header->rsrvd),htons(header->length));

		    DDCMSG(D_PACKET,BLUE,"\t\t\t\t\t\t\tmessage body\n"\
			  "C-ID | Expos | Aspct |  Dir | Move |  Speed | On/Off | Hits | React | ToKill | Sens | Mode | Burst\n"\
			  "%3d    %3d     %3d     %2d    %3d    %7.2f     %4d     %2d     %3d     %3d     %3d    %3d   %5d "
			  ,message_2100->cid,message_2100->exp,message_2100->asp,message_2100->dir,message_2100->move,message_2100->speed
			  ,message_2100->on,htons(message_2100->hit),message_2100->react,htons(message_2100->tokill),htons(message_2100->sens)
			  ,message_2100->mode,htons(message_2100->burst));

//		    also build an LB packet  to send
		    LB_configure =(LB_configure_t *)&LB_buf;	// make a pointer to our buffer so we can use the bits right
		    LB_configure->cmd=LBC_CONFIGURE_HIT;
		    LB_configure->addr=minion->RF_addr;

          LB_configure->hitmode = (message_2100->mode == 2 ? 1 : 0); // burst / single
          LB_configure->tokill =  htons(message_2100->tokill);
          LB_configure->react = message_2100->react & 0x07;
          LB_configure->sensitivity = htons(message_2100->sens);
          LB_configure->timehits = (htons(message_2100->burst) / 5) & 0x0f; // convert
          // find hitcountset value
          if (htons(message_2100->hit) == htons(message_2100->tokill)) {
             LB_configure->hitcountset = 3; // set to hits-to-kill value
          } else if (minion->S.hit.data == (htons(message_2100->hit)) - 1) {
             LB_configure->hitcountset = 2; // incriment by one
          } else if (htons(message_2100->hit) == 0) {
             LB_configure->hitcountset = 1; // reset to zero
          } else {
             LB_configure->hitcountset = 0; // no change
          }

	    // calculates the correct CRC and adds it to the end of the packet payload
	    // also fills in the length field
		    set_crc8(LB_configure);
	    // now send it to the MCP master
		    result=write(minion->mcp_sock,LB_configure,RF_size(LB_configure->cmd));
		    if (verbose&D_RF){	// don't do the sprintf if we don't need to
			sprintf(hbuf,"Minion %d: LB packet to MCP address=%4d cmd=%2d msglen=%d",minion->mID,minion->RF_addr,LB_configure->cmd,RF_size(LB_configure->cmd));
			DDCMSG2_HEXB(D_RF,YELLOW,hbuf,LB_configure,RF_size(LB_configure->cmd));
			DDCMSG(D_RF,YELLOW,"  Sent %d bytes to MCP fd=%d\n",RF_size(LB_configure->cmd),minion->mcp_sock);
	    }  
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
		    DDCMSG(D_PACKET,BLUE,"calling doHitCal after setting values") ;

		    // send 2102 status or change the hit count (which will send the 2102 later)
		    if (1 /*hits == htons(message_2100->hit)*/) {
			sendStatus2102(1,header,minion);  // sends a 2102 as we won't if we didn't change the the hit count
			DDCMSG(D_PACKET,BLUE,"We will send 2102 status in response to the config hit sensor command");
			
		    } else {
			//	doHits(htons(message_2100->hit));    // set hit count to something other than zero
			DDCMSG(D_PACKET,BLUE,"after doHits(%d) ",htons(message_2100->hit)) ;
		    }

		    break;

		case CID_GPS_Location_Request:
		    DDCMSG(D_PACKET,BLUE,"CID_GPS_Location_Request  send 'F'ailure ack  - because we don't support it") ;
		    send_2101_ACK(header,'F',minion);

		    // send 2113 GPS Location
		    break;

		case CID_Sleep:
		case CID_Wake:
		case CID_Shutdown:
//		    also build an LB packet  to send
		    LB_power =(LB_power_control_t *)&LB_buf;	// make a pointer to our buffer so we can use the bits right
		    LB_power->cmd=LBC_POWER_CONTROL;
		    LB_power->addr=minion->RF_addr;

          switch (message_2100->cid) {
             case CID_Sleep:
                DDCMSG(D_PACKET,BLUE,"CID_Sleep...sleeping") ; 
                LB_power->pcmd = 1;
                break;
             case CID_Wake:
                DDCMSG(D_PACKET,BLUE,"CID_Wake...waking") ; 
                LB_power->pcmd = 2;
                break;
             case CID_Shutdown:
                DDCMSG(D_PACKET,BLUE,"CID_Shutdown...shutting down") ; 
                LB_power->pcmd = 3;
                break;
          }

	    // calculates the correct CRC and adds it to the end of the packet payload
	    // also fills in the length field
		    set_crc8(LB_power);
	    // now send it to the MCP master
		    result=write(minion->mcp_sock,LB_power,RF_size(LB_power->cmd));
		    if (verbose&D_RF){	// don't do the sprintf if we don't need to
			sprintf(hbuf,"Minion %d: LB packet to MCP address=%4d cmd=%2d msglen=%d",minion->mID,minion->RF_addr,LB_power->cmd,RF_size(LB_power->cmd));
			DDCMSG2_HEXB(D_RF,YELLOW,hbuf,LB_power,RF_size(LB_power->cmd));
			DDCMSG(D_RF,YELLOW,"  Sent %d bytes to MCP fd=%d\n",RF_size(LB_power->cmd),minion->mcp_sock);
		    }
//  sent LB
    
		    // send 2101 ack  (2102's will be generated at start and stop of actuator)
		    send_2101_ACK(header,'S',minion);    // TRACR Cert complains if these are not there

		    // it should happen in this many deciseconds
		    //  - fasit device cert seems to come back immeadiately and ask for status again, and
		    //    that is causing a bit of trouble.		    break;

		    break;
      }
      break;

	case 2110:
	    message_2110 = (FASIT_2110*)(buf + sizeof(FASIT_header));

	    DDCMSG(D_PACKET,BLUE,"MINION %d: fasit packet 2110 Configure_Muzzle_Flash, seq=%d  on=%d  mode=%d  idelay=%d  rdelay=%d"
		  , minion->mID,htonl(header->seq),message_2110->on,message_2110->mode,message_2110->idelay,message_2110->rdelay);

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
		sendStatus2112(1,header,minion); // forces sending of a 2112
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


void *minion_thread(thread_data_t *minion){
    minion_time_t mt;
    minion_bufs_t mb;
    int sock_ready,error;
    int oldtimer;

    uint8 crc;
    char *tbuf;
    char mbuf[BufSize];
    int i,msglen,result,seq,length;
    fd_set rcc_or_mcp;

    struct iovec iov[2];

    FASIT_header rhdr;
    FASIT_2111	 msg;
    FASIT_2100	 *message_2100;
    FASIT_2110	 *message_2110;
    FASIT_13110	 *message_13110;

    LB_packet_t *LB;
    LB_device_reg_t *LB_devreg;
    LB_assign_addr_t *LB_addr;
    LB_expose_t *LB_exp;

    
    initialize_state( &minion->S);

/*** actually, the capabilities and the devid's will come from the MCP -
 ***   it gets them when it sends out the request new devices over the RF
 *** so when we are spawned, those state fields are already filled in 
 ***/

    minion->S.cap|=PD_NES;	// add the NES capability
    
    DCMSG(BLUE,"minion %d: state is initialized as devid 0x%06X", minion->mID,minion->devid);
    DCMSG(BLUE,"minion %d: RF_addr = %d", minion->mID,minion->RF_addr);
    
    if (minion->S.cap&PD_NES) {
	DCMSG(BLUE,"minion %d: has Night Effects Simulator (NES) capability", minion->mID);
    }
    if (minion->S.cap&PD_MILES) {
	DCMSG(BLUE,"minion %d: has MILES (shootback) capability", minion->mID);
    }
    if (minion->S.cap&PD_GPS) {
	DCMSG(BLUE,"minion %d: has Global Positioning System (GPS) capability", minion->mID);
    }

    i=0;

    // main loop 
    //   respond to the mcp commands
    // respond to FASIT commands
    //     loop and reconnect to FASIT if it disconnects.
    //  feed the MCP packets to send out the RF transmitter
    // update our state when MCP commands are RF packets back from our slave(s)
    //

    // setup the timeout for the first select.   subsequent selects have the timout set
    // in the timer related bits at the end of the loop
    //  the goal is to update all our internal state timers with tenth second resolution

    minion->S.state_timer=900;	// every 10.0 seconds, worst case
    
    clock_gettime(CLOCK_MONOTONIC_RAW,&mt.istart_time);	// get the intial current time
    minion->rcc_sock=-1;	// mark the socket so we know to open again
    while(1) {

	if (minion->rcc_sock<0) {
    // now we must get a connection or new connection to the range control
    // computer (RCC) using fasit.

	    do {
		minion->rcc_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		if(minion->rcc_sock < 0)   {
		    perror("socket() failed");
		}

		result=connect(minion->rcc_sock,(struct sockaddr *) &fasit_addr, sizeof(struct sockaddr_in));
		if (result<0){
		    strerror_r(errno,mb.buf,BufSize);
		    DCMSG(RED,"minion %d: fasit server not found! connect(...,%s:%d,...) error : %s  ", minion->mID,inet_ntoa(fasit_addr.sin_addr),htons(fasit_addr.sin_port),mb.buf);
		    DCMSG(RED,"minion %d:   waiting for a bit and then re-trying ", minion->mID);
		    minion->rcc_sock=-1;	// make sure it is marked as closed
		    sleep(2);		// adding a little extra wait
		}
		
	    } while (result<0);
// actually we really shouldn't stay in the above loop if disconnected, we need to keep updating minion states
//	    and communicating with the RF slaves.
//   also we need to loop through because if we are stuck in a loop above we will not see that the
//   MCP has died, and we would become zombies.  
// we now have a socket.
	    DDCMSG(D_MINION,BLUE,"minion %d: has a socket to a RCC", minion->mID);
	}
	

	/* create a fd_set so we can monitor both the mcp and the connection to the RCC*/
	FD_ZERO(&rcc_or_mcp);
	FD_SET(minion->mcp_sock,&rcc_or_mcp);		// we are interested hearing the mcp
	FD_SET(minion->rcc_sock,&rcc_or_mcp);		// we also want to hear from the RCC

	/* block for up to state_timer deciseconds waiting for RCC or MCP 
	 * and we may drop through much faster if we have a MCP or RCC communication
	 * that we have to process, otherwise we have to update state
	 * stuff
	 */
	
	mt.timeout.tv_sec=minion->S.state_timer/10;
	mt.timeout.tv_usec=100000*(minion->S.state_timer%10);
	
	timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);
	
	DDCMSG(D_TIME,CYAN,"MINION %d: before Select... state_timer=%d (%d.%03d) at %ld.%03ldet",
	       minion->mID,minion->S.state_timer,(int)mt.timeout.tv_sec,(int)mt.timeout.tv_usec,mt.elapsed_time.tv_sec, mt.elapsed_time.tv_nsec/1000000L);
	
	sock_ready=select(FD_SETSIZE,&rcc_or_mcp,(fd_set *) 0,(fd_set *) 0, &mt.timeout);	
	// if we are running on linux, mt.timeout wll have a remaining time left in it if one of the file descriptors was ready.
	// we are going to use that for now

	oldtimer=minion->S.state_timer;		// copy our old time for debugging
	minion->S.state_timer=(mt.timeout.tv_sec*10)+(mt.timeout.tv_usec/100000L);
	
	if (sock_ready<0){
	    perror("NOTICE!  select error : ");
	    exit(-1);
	}

	timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);
	DDCMSG(D_TIME,CYAN,"MINION %d: After Select: sock_ready=%d FD_ISSET(mcp)=%d FD_ISSET(rcc)=%d oldtimer=%d state_timer=%d at %ld.%03ldet, delta=%ld.%03ld",
	       minion->mID,sock_ready,FD_ISSET(minion->mcp_sock,&rcc_or_mcp),FD_ISSET(minion->rcc_sock,&rcc_or_mcp),oldtimer,minion->S.state_timer,
	       mt.elapsed_time.tv_sec, mt.elapsed_time.tv_nsec/1000000L,mt.delta_time.tv_sec, mt.delta_time.tv_nsec/100000L);

	
	//check to see if the MCP has any commands for us
	if (FD_ISSET(minion->mcp_sock,&rcc_or_mcp)){
	    msglen=read(minion->mcp_sock, mb.buf, 1023);
	    if (msglen > 0) {
		mb.buf[msglen]=0;
		if (verbose&D_PACKET){	// don't do the sprintf if we don't need to		
		    sprintf(mb.hbuf,"Minion %d: received %d from MCP (RF) ", minion->mID,msglen);
		    DDCMSG_HEXB(D_PACKET,YELLOW,mb.hbuf,mb.buf,msglen);
		}
		// we have received a message from the mcp, process it
		// it is either a command, or it is an RF response from our slave
		//		process_MCP_cmds(&state,mb.buf,msglen);

		LB=(LB_packet_t *)mb.buf;	// map the header in
		crc= crc8(LB);
		DDCMSG(D_RF,YELLOW,"MINION %d: LB packet (cmd=%2d addr=%d crc=%d)", minion->mID,LB->cmd,LB->addr,crc);
		switch (LB->cmd){
		    case LBC_REQUEST_NEW:
			DDCMSG(D_NEW,YELLOW,"Recieved 'request new devices' packet.");
		// the response was created when the MCP spawned us, so we do nothing for this packet

			break;

		    case LBC_ASSIGN_ADDR:
			LB_addr =(LB_assign_addr_t *)(LB);	// map our bitfields in

			minion->RF_addr=LB_addr->new_addr;	// set our new address
			DDCMSG(D_NEW,YELLOW,"Minion %d: parsed 'device address' packet.  new minion->RF_addr=%d",minion->mID,minion->RF_addr);
			
			break;

		    case LBC_EVENT_REPORT:
		    {
			LB_event_report_t *L=(LB_event_report_t *)(LB);	// map our bitfields in
			minion->S.hit.newdata+=L->hits;	// we need to add it to hit.newdata, it will be reset by SR or TRACR when they want to
			DDCMSG(D_NEW,YELLOW,"Minion %d: (Rf_addr=%d) parsed 'Event_report'. hits=%d  sending 2102 status",
			       minion->mID,minion->RF_addr,L->hits);
			sendStatus2102(0, NULL,minion);
			
		    }
			break;

		    case LBC_STATUS_NO_RESP:
 // Do nothing on the "no response" -- TODO -- reset timeout timers
			DDCMSG(D_NEW,YELLOW,"Minion %d: (Rf_addr=%d) parsed 'NO_RESP'...not sending 2102 status",minion->mID,minion->RF_addr);
         break;

		    case LBC_STATUS_RESP_EXT:
		    {
			LB_status_resp_ext_t *L=(LB_status_resp_ext_t *)(LB);	// map our bitfields in
			minion->S.hit.newdata+=L->hits;	// we need to add it to hit.newdata, it will be reset by SR or TRACR when they want to
			minion->S.exp.data=L->expose ? 90 : 0; // convert to only up or down
			minion->S.speed.data = (float)(L->speed / 100.0); // convert back to correct float
         minion->S.move.data = L->dir;
         minion->S.react.newdata = L->react;
         minion->S.tokill.newdata = L->tokill;
         minion->S.mode.newdata = L->hitmode ? 2 : 1; // back to burst/single
         minion->S.sens.newdata = L->sensitivity;
         minion->S.pos.data = L->location;
         minion->S.burst.newdata = L->timehits * 5; // convert back
         minion->S.fault.data = L->fault;
			DDCMSG(D_NEW,YELLOW,"Minion %d: (Rf_addr=%d) parsed 'RESP_EXT'. hits=%d fault=%02X sending 2102 status",
			       minion->mID,minion->RF_addr,L->hits,L->fault);
			sendStatus2102(0, NULL,minion);
			
		    }
			break;

		    case LBC_STATUS_RESP_MOVER:
		    {
			LB_status_resp_ext_t *L=(LB_status_resp_ext_t *)(LB);	// map our bitfields in
			minion->S.hit.newdata+=L->hits;	// we need to add it to hit.newdata, it will be reset by SR or TRACR when they want to
			minion->S.exp.data=L->expose ? 90 : 0; // convert to only up or down
			minion->S.speed.data = (float)(L->speed / 100.0); // convert back to correct float
         minion->S.move.data = L->dir;
         minion->S.pos.data = L->location;
			DDCMSG(D_NEW,YELLOW,"Minion %d: (Rf_addr=%d) parsed 'RESP_MOVER'. hits=%d  sending 2102 status",
			       minion->mID,minion->RF_addr,L->hits);
			sendStatus2102(0, NULL,minion);
			
		    }
			break;

		    case LBC_STATUS_RESP_LIFTER:
		    {
			LB_status_resp_ext_t *L=(LB_status_resp_ext_t *)(LB);	// map our bitfields in
			minion->S.hit.newdata+=L->hits;	// we need to add it to hit.newdata, it will be reset by SR or TRACR when they want to
			minion->S.exp.data=L->expose ? 90 : 0; // convert to only up or down
			DDCMSG(D_NEW,YELLOW,"Minion %d: (Rf_addr=%d) parsed 'RESP_LIFTER'. hits=%d  sending 2102 status",
			       minion->mID,minion->RF_addr,L->hits);
			sendStatus2102(0, NULL,minion);
			
		    }
			break;


			
		    default:
			DDCMSG(D_RF,YELLOW,"Minion %d:  recieved a cmd=%d    don't do anything",minion->mID,LB->cmd);

			break;
		}  // switch LB cmd

		
	    } else if (msglen<0) {
		if (errno!=EAGAIN){
		    DCMSG(RED,"minion %d: read returned %d errno=%d socket to MCP closed, we are FREEEEEeeee!!!",minion->mID,msglen,errno);
		    exit(-2);  /* this minion dies!   it should do something else maybe  */
		}
	    }else {
		DCMSG(RED,"minion %d: socket to MCP closed, we are FREEEEEeeee!!!", minion->mID);
		exit(-2);  /* this minion dies!  possibly it should do something else - but maybe it dies of happyness  */
	    }
	    
	    timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);	
	    DDCMSG(D_TIME,CYAN,"MINION %d: End of MCP Parse at %ld.%03ldet, delta=%ld.%03ld"
		   ,minion->mID,mt.elapsed_time.tv_sec, mt.elapsed_time.tv_nsec/1000000L,mt.delta_time.tv_sec, mt.delta_time.tv_nsec/1000000L);
	}

	/**********************************    end of reading and processing the mcp command  ***********************/
	
	/*************** check to see if there is something to read from the rcc   **************************/
	if (FD_ISSET(minion->rcc_sock,&rcc_or_mcp)){
	    // now read it using the new routine    
	    result = read_FASIT_msg(minion,mb.buf, BufSize);
	    if (result>0){		
		mb.header = (FASIT_header*)(mb.buf);	// find out how long of message we have
		length=htons(mb.header->length);	// set the length for the handle function
		if (result>length){
			DDCMSG(D_PACKET,BLUE,"MINION %d: Multiple Packet  num=%d  result=%d seq=%d mb.header->length=%d",
			       minion->mID,htons(mb.header->num),result,htonl(mb.header->seq),length);
		} else {
			DDCMSG(D_PACKET,BLUE,"MINION %d: num=%d  result=%d seq=%d mb.header->length=%d",
			       minion->mID,htons(mb.header->num),result,htonl(mb.header->seq),length);
		}
		tbuf=mb.buf;			// use our temp pointer so we can step ahead
		// loop until result reaches 0
		while((result>=length)&&(length>0)) {
		    timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);	
		    DDCMSG(D_TIME,CYAN,"MINION %d:  Packet %d recieved at %ld.%03ldet, delta=%ld.%03ld"
			   ,minion->mID,htons(mb.header->num),mt.elapsed_time.tv_sec, mt.elapsed_time.tv_nsec/1000000L,mt.delta_time.tv_sec, mt.delta_time.tv_nsec/1000000L);
		    handle_FASIT_msg(minion,tbuf,length,&mt.elapsed_time);
		    result-=length;			// reset the length to handle a possible next message in this packet
		    tbuf+=length;			// step ahead to next message
		    mb.header = (FASIT_header*)(tbuf);	// find out how long of message we have
		    length=htons(mb.header->length);	// set the length for the handle function
		    if (result){
			DDCMSG(D_PACKET,BLUE,"MINION %d: Continue processing the rest of the BIG fasit packet num=%d  result=%d seq=%d  length=%d",
			       minion->mID,htons(mb.header->num),result,htonl(mb.header->seq),length);
		    }
		}
	    } else {
		strerror_r(errno,mb.buf,BufSize);
		DDCMSG(D_PACKET,RED,"MINION %d: read_FASIT_msg returned %d and Error: %s", minion->mID,result,mb.buf);
		DDCMSG(D_PACKET,RED,"MINION %d: which means it likely has closed!", minion->mID);
		minion->rcc_sock=-1;	// mark the socket so we know to open again

	    }
	    timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);	
	    DDCMSG(D_TIME,CYAN,"MINION %d: End of RCC Parse at %ld.%03ldet, delta=%ld.%03ld"
		   ,minion->mID,mt.elapsed_time.tv_sec, mt.elapsed_time.tv_nsec/1000000L,mt.delta_time.tv_sec, mt.delta_time.tv_nsec/1000000L);
	}
	/**************   end of rcc command parsing   ****************/
	/**  first we just update our counter, and if less then a tenth of a second passed,
	 **  skip the timer updates and reloop, using a new select value that is a fraction of a tenth
	 **
	 **/

    minion_state(minion, &mt, &mb);
	
#if 0
	timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);
	DDCMSG(D_TIME,CYAN,"MINION %d: End timer updates at %6ld.%09ld timestamp, delta=%1ld.%09ld"
	       ,minion->mID,mt.elapsed_time.tv_sec, mt.elapsed_time.tv_nsec,mt.delta_time.tv_sec, mt.delta_time.tv_nsec);
#endif
    }  // while forever
}  // end of minon thread




