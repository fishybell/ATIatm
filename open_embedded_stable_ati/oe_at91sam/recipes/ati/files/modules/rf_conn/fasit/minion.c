#include "mcp.h"
#include "rf.h"
#include "fasit_c.h"
#include "RFslave.h"

#define S_set(ITEM,D,ND,F,T) \
    { \
       S->ITEM.data = D; \
       S->ITEM.newdata = ND; \
       S->ITEM.flags = F; \
       S->ITEM.timer = T; \
    }

#undef CLOCK_MONOTONIC_RAW 
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC

int resp_num,resp_seq;          // global for now.  not happy

const int cal_table[16] = {0xFFFFFFFF,333,200,125,75,60,48,37,29,22,16,11,7,4,2,1};

// globals that get inherited from the parent (MCP)
extern int verbose;
extern struct sockaddr_in fasit_addr;
extern int close_nicely;


// initialize our state to default values
void initialize_state(minion_state_t *S){

////  these initial settings are passed up by the device registration packet
// but there should still be the timers and stuff zero'ed   
//   S_set(exp,0,0,0,0);
//   S_set(dir,0,0,0,0);
//   S_set(move,0,0,0,0);
//   S_set(speed,0,0,0,0);
//   S_set(hit,0,0,0,0);
//   S_set(react,0,0,0,0);
//   S_set(mode,0,0,0,0);
//   S_set(position,0,0,0,0);

   // timers (above and rf_t) might be set from mcp before the minion was forked off; leave them be
   //S_set(rf_t,0,0,0,0);
   
   S_set(asp,0,0,0,0);
   S_set(on,0,0,0,0);
   S_set(tokill,0,0,0,0);
   S_set(sens,0,0,0,0);
   S_set(burst,0,0,0,0);

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
   FASIT_2111    msg;
   char hbuf[200];

   //read the RCC
   msglen=read(minion->rcc_sock, buf, bufsize);
   if (msglen > 0) {
      buf[msglen]=0;

      if (verbose&D_PACKET){    // don't do the sprintf if we don't need to
         sprintf(hbuf,"minion %i: received %i from RCC        ", minion->mID,msglen);
         DDCMSG_HEXB(D_PACKET,BLUE,hbuf,buf,msglen);
      }

      header=(FASIT_header *)buf; 
      DDCMSG(D_PACKET,BLUE,"minion %i: FASIT packet, cmd=%i ICD=%i.%i seq=%i len=%i",
             minion->mID,htons(header->num),htons(header->icd1),htons(header->icd2),htonl(header->seq),htons(header->length));

      // we will return to the caller with the msglen, and they can look at the buffer and parse the message and respond to it
   } else if (msglen<0) {
      if (errno!=EAGAIN){
         DDCMSG(D_PACKET,RED,"minion %i: read of RCC socket returned %i errno=%i socket to RCC closed",minion->mID, msglen,errno);
         return(-1);  /* this minion does not die!     */
      } else {
         DDCMSG(D_PACKET,RED,"minion %i: socket to RCC closed, !!!", minion->mID);
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
   if ((verbose&D_PACKET)&&(result >= 0)){      // don't do the sprintf if we don't need to
      sprintf(hbuf,"minion %i: sent %i chars header to RCC ",minion->mID,(int)iov[0].iov_len);
      DDCMSG_HEXB(D_PACKET,BLUE,hbuf,iov[0].iov_base,iov[0].iov_len);
      sprintf(hbuf,"minion %i: sent %i chars body to RCC   ",minion->mID,(int)iov[1].iov_len);  
      DDCMSG_HEXB(D_PACKET,BLUE,hbuf,iov[1].iov_base,iov[1].iov_len);

   }
   if (result<0) {
      PERROR("write_FASIT_msg:   error writing stream message");
   }
   return(result);
}

// create and send a hit time message to the FASIT server
void sendStatus16000(thread_data_t *minion, int hits, int time) {
   // do handling of message
   FASIT_header hdr;
   FASIT_16000 msg;

   DDCMSG(D_PACKET,BLUE,"minion %i: sending 16000 hit time %i %i",minion->mID, hits, time);

   // sets the sequence number and other data    
   defHeader(&hdr,16000,minion->seq++, sizeof(FASIT_16000));
   msg.hits = htons(hits);
   msg.msecs = htonl(time);

   write_FASIT_msg(minion,&hdr,sizeof(FASIT_header),&msg,sizeof(FASIT_16000));
}

// create and send a status message to the FASIT server
void sendStatus2102(int force, FASIT_header *hdr,thread_data_t *minion, minion_time_t *mt) {
   struct iovec iov[2];
   FASIT_header rhdr;
   FASIT_2102 msg;
   int result;
   timestamp(&mt->elapsed_time,&mt->istart_time,&mt->delta_time);
   DDCMSG(D_POINTER|D_NEW, YELLOW, "sendStatus2102 (%i) called @ %3i.%03i", minion->S.exp.data, DEBUG_TS(mt->elapsed_time));

   // sets the sequence number and other data    
   defHeader(&rhdr,2102,minion->seq++, sizeof(FASIT_2102));

   // fill message
   // start with zeroes
   memset(&msg, 0, sizeof(FASIT_2102));

   // fill out response
   if (force==1) {
      DDCMSG(D_NEW, black, "Forced to send 2102 with response %i %i", hdr->num, hdr->seq);
      msg.response.resp_num = hdr->num; //  pulls the message number from the header  (htons was wrong here)
      msg.response.resp_seq = hdr->seq;
      //        msg.response.resp_num = rhdr.num;       //  pulls the message number from the header  (htons was wrong here)
      //        msg.response.resp_seq = htonl(minion->seq-1);
   } else if (force==0) {
      msg.response.resp_num = 0;        // unsolicited
      msg.response.resp_seq = 0;
   }
   DDCMSG(D_PACKET,RED,"Minion %i: 2102 status response hdr->num=%i, hdr->seq=%i",minion->mID,
          htons(msg.response.resp_num),htonl(msg.response.resp_num));
   // device type
   msg.body.type = minion->S.dev_type; // should be correct by the time it sends this

   //   DCMSG(YELLOW,"before  doHits(-1)   hits = %i",hits) ;    
   //    doHits(-1);  // request the hit count
   //   DCMSG(YELLOW,"retrieved hits with doHits(-1) and setting to %i",hits) ; 

   #define CACHE_CHECK(S) { \
      if (S.data != S.lastdata) { /* see if we've changed */ \
         DDCMSG(D_NEW, black, "Changed data for " #S " %d := %d", S.lastdata, S.data); \
         force=1; /* force it if it's not already forced */ \
         S.lastdata = S.data; /* save this change */ \
      } \
   }

   // individual device type stuff
   switch (msg.body.type) {
      case Type_SIT:
      case Type_SAT:
      case Type_HSAT:
         // exposure
         // instead of obfuscating the 0,45,and 90 values, just use them.

         CACHE_CHECK(minion->S.exp);
         msg.body.exp = minion->S.exp.data;
         DCMSG(GRAY, "Might send 2102 expose: msg.body.exp=%i @ %i", msg.body.exp, __LINE__);
         msg.body.speed = 0.0;
         break;
      case Type_MIT:
      case Type_MAT:
         CACHE_CHECK(minion->S.speed);
         msg.body.speed = htonf(minion->S.speed.data);
         CACHE_CHECK(minion->S.exp);
         msg.body.exp = minion->S.exp.data;
         DCMSG(GRAY, "Might send 2102 expose: msg.body.exp=%i @ %i", msg.body.exp, __LINE__);
         CACHE_CHECK(minion->S.move);
         if (minion->S.speed.data != 0.0) {
            msg.body.move = minion->S.move.data ? 2 : 1; // convert rf to fasit by changing 0 to 1, 1 to 2
         } else {
            msg.body.move = 0;
         }
         CACHE_CHECK(minion->S.position);
         msg.body.pos = htons(minion->S.position.data);
         DDCMSG(D_PACKET,BLUE,"Minion %i: building 2102 status.   S.position.data=%i msg.body.pos=%i  htons(...)=%i ",
                minion->mID,minion->S.position.data,msg.body.pos,htons(msg.body.pos));
         break;
   }

   // left to do:
   msg.body.pstatus = 0; // always "shore" power
   CACHE_CHECK(minion->S.fault);
   msg.body.fault = htons(minion->S.fault.data);

   // hit record
   // CACHE_CHECK(minion->S.exp); always zero with new hit time message
   msg.body.hit = htons(minion->S.hit.newdata); // care must be taken that this is the correct value, as it is the most important value (will always be zero with the new hit time message)
   msg.body.hit_conf.on = minion->S.on.newdata;
   msg.body.hit_conf.react = minion->S.react.newdata;
   msg.body.hit_conf.tokill = htons(minion->S.tokill.newdata);
   msg.body.hit_conf.burst = htons(minion->S.burst.newdata);
   msg.body.hit_conf.sens = htons(minion->S.sens.newdata);
   msg.body.hit_conf.mode = minion->S.mode.newdata;

   if (force == 1) {
      DCMSG(GRAY, "Sending 2102 expose: msg.body.exp=%i @ %i", msg.body.exp, __LINE__);
      DDCMSG(D_PACKET,BLUE,"Minion %i: sending a 2102 status response - %i byte header %i byte body",minion->mID,sizeof(FASIT_header),sizeof(FASIT_2102));
      DDCMSG(D_PARSE,BLUE,"M-Num | ICD-v | seq-# | rsrvd | length  R-num  R-seq          <--- Header\n %6i  %i.%i  %6i  %6i %7i %6i %7i "
             ,htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htonl(rhdr.seq),htonl(rhdr.rsrvd),htons(rhdr.length),htons(msg.response.resp_num),htonl(msg.response.resp_seq));
      DDCMSG(D_PARSE,BLUE,\
             "PSTAT | Fault | Expos | Aspct |  Dir | Move |  Speed  | POS | Type | Hits | On/Off | React | ToKill | Sens | Mode | Burst\n"\
             "       %3i    %3i     %3i     %3i     %3i    %3i    %6.2f    %3i   %3i    %3i      %3i     %3i      %3i     %3i    %3i    %3i ",
             msg.body.pstatus,msg.body.fault,msg.body.exp,msg.body.asp,msg.body.dir,msg.body.move,msg.body.speed,htons(msg.body.pos),msg.body.type,htons(msg.body.hit),
             msg.body.hit_conf.on,msg.body.hit_conf.react,htons(msg.body.hit_conf.tokill),htons(msg.body.hit_conf.sens),msg.body.hit_conf.mode,htons(msg.body.hit_conf.burst));

      write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&msg,sizeof(FASIT_2102));
   } else {
      DDCMSG(D_PACKET,CYAN,"Minion %i didn't send 2102 status response as nothing much had changed and we weren't forced", minion->mID);
   }
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
      msg.response.resp_num = hdr->num; //  pulls the message number from the header that we are responding to
      msg.response.resp_seq = hdr->seq;
   } else if (force==0) {
      msg.response.resp_num = 0;        // unsolicited
      msg.response.resp_seq = 0;
   }

   // not sure if this line is right, though
   //    msg.response.resp_num = htons(2110);       // the old code had this and seemed to work better

   msg.body.on = minion->S.mfs_on.newdata;
   msg.body.mode = minion->S.mfs_mode.newdata;
   msg.body.idelay = minion->S.mfs_idelay.newdata;
   msg.body.rdelay = minion->S.mfs_rdelay.newdata;

   DDCMSG(D_PACKET,BLUE,"Minion %i: sending a 2112 status response - %i byte header %i byte body",minion->mID,sizeof(FASIT_header),sizeof(FASIT_2112));
   DDCMSG(D_PARSE,BLUE,"M-Num | ICD-v | seq-# | rsrvd | length  R-num  R-seq          <--- Header\n %6i  %i.%i  %6i  %6i %7i %6i %7i "
          ,htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htonl(rhdr.seq),htonl(rhdr.rsrvd),htons(rhdr.length),htons(msg.response.resp_num),htonl(msg.response.resp_seq));
   DDCMSG(D_PARSE,BLUE,\
          "   ON | Mode  | idelay| rdelay\n"\
          "  %3i    %3i     %3i     %3i   ",
          msg.body.on,msg.body.mode,msg.body.idelay,msg.body.rdelay);

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

   DDCMSG(D_PACKET,BLUE,"minion %i: sending 2101 ACK \"%c\"",minion->mID,response);

   // build the response - some CID's just reply 2101 with 'S' for received and complied 
   // and 'F' for Received and Cannot comply
   // other Command ID's send other messages

   // sets the sequence number and other data    
   defHeader(&rhdr,2101,minion->seq++, sizeof(FASIT_2101));

   // set response
   rmsg.response.resp_num = hdr->num;   //  pulls the message number from the header  (htons was wrong here)
   rmsg.response.resp_seq = hdr->seq;           

   rmsg.body.resp = response;   // The actual response code 'S'=can do, 'F'=Can't do
   //    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd |
   //    length\n%6i  %i.%i  %6i  %6i  %7i",htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htonl(rhdr.seq),htons(rhdr.rsrvd),htons(rhdr.length));
   //    DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nR-NUM | R-Seq | Response\n%5i  %6i  '%c'",
   //     htons(rmsg.response.resp_num),htons(rmsg.response.resp_seq),rmsg.body.resp);

   write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&rmsg,sizeof(FASIT_2101));
   return 0;
}
                  // now send it to the MCP master
int psend_mcp(thread_data_t *minion,void *Lc){
   int result;
   char hbuf[100];
   LB_packet_t *L=(LB_packet_t *)Lc;    // so we can call it with different types and not puke.

   // calculates the correct CRC and adds it to the end of the packet payload
   set_crc8(L);

   result=write(minion->mcp_sock,L,RF_size(L->cmd));
   //  sent LB
   if (verbose&D_RF){       // don't do the sprintf if we don't need to
      sprintf(hbuf,"Minion %i: psend LB packet to MCP sock=%i   RF address=%4i cmd=%2i msglen=%i result=%i",
              minion->mID,minion->mcp_sock,minion->RF_addr,L->cmd,RF_size(L->cmd),result);
      DDCMSG2_HEXB(D_RF,YELLOW,hbuf,L,RF_size(L->cmd));
      DDpacket(Lc,RF_size(L->cmd));
   }
   return result;
}

void send_LB_exp(thread_data_t *minion, int expose, minion_time_t *mt) {
   char qbuf[32];      // more packet buffers
   LB_expose_t *LB_exp;

   timestamp(&mt->elapsed_time,&mt->istart_time,&mt->delta_time);
   DDCMSG(D_POINTER|D_NEW, black, "send_LB_exp (%i) called @ %3i.%03i\n____________________________________", expose, DEBUG_TS(mt->elapsed_time));

   LB_exp =(LB_expose_t *)qbuf;       // make a pointer to our buffer so we can use the bits right
   LB_exp->cmd=LBC_EXPOSE;
   LB_exp->addr=minion->RF_addr;
   LB_exp->expose=expose; // not expose vs. conceal, but a flag saying whether to actually expose or just change event number internally
#if 0 /* start of old state timer code */
   // change state
   LB_exp->event=++minion->S.exp.event;  // fill in the event
   minion->S.exp.flags=F_exp_expose_A;   // start (faking FASIT) moving to expose
   minion->S.exp.timer=5;
   minion->S.exp.start_time.tv_sec =mt->elapsed_time.tv_sec;
   minion->S.exp.start_time.tv_nsec=mt->elapsed_time.tv_nsec;
#endif /* end of old state timer code */

   // new state timer code
   if (minion->S.exp.data == 45 && expose) {
      // if we are sending a new command before receiving an update, send status back to SR now
      if (minion->S.exp.newdata == 90) {
         minion->S.exp.lastdata = 0; // force sending of status by messing with cache
         minion->S.exp.data = 90; // we're now exposed
      } else {
         minion->S.exp.lastdata = 0; // force sending of status by messing with cache
         minion->S.exp.data = 90; // we're now exposed
      }
      DCMSG(GRAY, "Sending fake 2102 expose: S.exp.newdata=%i, S.exp.data=%i @ %i", minion->S.exp.newdata, minion->S.exp.data, __LINE__);
                        DDCMSG(D_POINTER|D_NEW, YELLOW, "calling sendStatus2102(%i) @ %i", minion->S.exp.data, __LINE__);
      sendStatus2102(0, NULL,minion, mt);
      // end old event on the target (no this won't recurse more than once)
                        DDCMSG(D_POINTER|D_NEW, black, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\ncalling send_LB_exp @ %i", __LINE__);
      send_LB_exp(minion, 0, mt); // don't expose, just change events and state (switches exp.newdata = 45, fix if necessary below)
      // we'll change to transistioning again below
   }
   // change state
   minion->S.exp.newdata=90; // moving to exposed
   minion->S.exp.recv_dec = 0;
   if (expose) {
      LB_exp->event=++minion->S.exp.event;  // fill in the event/move to next event
      // reset our times (we'll set our start time when we receive our first status response)
      minion->S.exp.cmd_start_time[minion->S.exp.event] = ts2ms(&mt->elapsed_time); // set command start time
      minion->S.exp.log_start_time[minion->S.exp.event] = 0; // reset log start time
      minion->S.exp.cmd_end_time[minion->S.exp.event] = 0; // reset cmd end time
      minion->S.exp.log_end_time[minion->S.exp.event] = 0; // reset log end time
   } else {
      LB_exp->event=minion->S.exp.event;  // just fill in the event
      // change our event end to match the event times on the target (delay notwithstanding)
      DDCMSG(D_POINTER, GREEN, "Expose (0) changing end to %3i.%03i from %3i.%03i)", DEBUG_MS(minion->S.exp.cmd_end_time[minion->S.exp.event]), DEBUG_TS(mt->elapsed_time));
      minion->S.exp.cmd_end_time[minion->S.exp.event] = ts2ms(&mt->elapsed_time);
   }
   minion->S.exp.data=45;  // make the current positon in movement
   DCMSG(GRAY, "Sending LB expose: S.exp.newdata=%i, S.exp.data=%i @ %i", minion->S.exp.newdata, minion->S.exp.data, __LINE__);
   START_EXPOSE_TIMER(minion->S); // new state timer code

   //  really need to fill in with the right stuff
   LB_exp->hitmode = (minion->S.mode.newdata == 2 ? 1 : 0); // burst / single
   LB_exp->tokill =  minion->S.tokill.newdata;
   LB_exp->react = minion->S.react.newdata;
   LB_exp->mfs = minion->S.mfs_on.newdata;
   LB_exp->thermal=0;
   DDCMSG(D_NEW,BLUE,"Minion %i:  LB_exp cmd=%i", minion->mID,LB_exp->cmd);
   psend_mcp(minion,LB_exp);
}

/********
 ********
 ********       moved the message handling to this function so multiple packets can be handled cleanly
 ********
 ********   timestamp is sent in too.
 ********/

int handle_FASIT_msg(thread_data_t *minion,char *buf, int packetlen, minion_time_t *mt){

   int result,psize;
   char hbuf[100];

   FASIT_header *header;
   FASIT_header rhdr;
   FASIT_2111    msg;
   FASIT_2100    *message_2100;
   FASIT_2110    *message_2110;
   FASIT_13110   *message_13110;

   char qbuf[32],rbuf[32];      // more packet buffers
   
   LB_packet_t          LB_buf;
   LB_device_reg_t      *LB_devreg;
   LB_status_req_t      *LB_status_req;
//   LB_expose_t          *LB_exp;
   LB_move_t            *LB_move;
   LB_configure_t       *LB_configure;
   LB_power_control_t   *LB_power;
   LB_audio_control_t   *LB_audio;
   LB_pyro_fire_t       *LB_pyro;
   LB_qconceal_t        *LB_qcon;


#if 0 /* start of old state timer code */
   // stop quick lookup of RF status on command from FASIT server
   minion->S.rf_t.flags = F_rf_t_waiting_long;
   minion->S.rf_t.timer = 3000; // 5 minutes in deciseconds
   switch (minion->S.exp.flags) { /* look for lookup-later states */
      case F_exp_expose_C:
      case F_exp_expose_D:
      case F_exp_conceal_C:
         minion->S.exp.flags=0;
         minion->S.exp.timer=0;
         break;
   }
#endif /* end of old state timer code */

   // new state timer code
   // there is no new timer code on receipt of FASIT message, only on receipt of RF message

   // map header and body for both message and response
   header = (FASIT_header*)(buf);
   DDCMSG(D_PACKET,BLUE,"Minion %i: Handle_FASIT_msg recieved fasit packet num=%i seq=%i length=%i packetlen=%i",
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

         DDCMSG(D_PACKET,RED,"Minion %i: Prepared to send 2111 device capabilites message response to a 100:",minion->mID);
         DDCMSG(D_PARSE,BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n"\
                "%4i    %i.%i     %5i    %3i     %3i"
                ,htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htonl(rhdr.seq),htons(rhdr.rsrvd),htons(rhdr.length));
         DDCMSG(D_PARSE,BLUE,"\t\t\t2111 message body\n Device ID (mac address backwards)   flag_bits == GPS=4,Muzzle Flash=2,MILES Shootback=1\n0x%8.8llx                    0x%2x"
                ,(long long int)msg.body.devid,msg.body.flags);
         write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&msg,sizeof(FASIT_2111));
         break;

      case 2100:
         message_2100 = (FASIT_2100*)(buf + sizeof(FASIT_header));

         DDCMSG(D_PACKET,BLUE,"Minion %i: fasit packet 2100, CID=%i\n\n***************\n\n", minion->mID,message_2100->cid);

         // Just parse out the command for now and print a pretty message
         switch (message_2100->cid) {
            case CID_No_Event:
               DDCMSG(D_PACKET,RED,"Minion %i: CID_No_Event",minion->mID) ; 
               break;

            case CID_Reserved01:
               DDCMSG(D_PACKET,RED,"Minion %i: CID_Reserved01",minion->mID) ;
               break;

            case CID_Status_Request:
               DDCMSG(D_PACKET,RED,"Minion %i: CID_Status_Request",minion->mID) ; 
               break;

            case CID_Expose_Request:
               DDCMSG(D_PACKET,RED,"Minion %i:CID_Expose_Request:  message_2100->exp=%i",minion->mID,message_2100->exp) ;
               break;

            case CID_Reset_Device:
               DDCMSG(D_PACKET,RED,"Minion %i: CID_Reset_Device  ",minion->mID) ;
               break;

            case CID_Move_Request:
               DDCMSG(D_PACKET,RED,"Minion %i: CID_Move_Request  ",minion->mID) ;       
               break;

            case CID_Config_Hit_Sensor:
               DDCMSG(D_PACKET,RED,"Minion %i: CID_Config_Hit_Sensor",minion->mID) ;                
               break;

            case CID_GPS_Location_Request:
               DDCMSG(D_PACKET,RED,"Minion %i: CID_GPS_Location_Request",minion->mID) ;
               break;

            case CID_Shutdown:
               DDCMSG(D_PACKET,RED,"Minion %i: CID_Shutdown",minion->mID) ;
               break;
         }

         DDCMSG(D_PARSE,CYAN,"Minion %i: Full message decomposition....",minion->mID);
         DDCMSG(D_PARSE,CYAN,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6i  %i.%i  %6i  %6i  %7i"
                ,htons(header->num),htons(header->icd1),htons(header->icd2),htonl(header->seq),htons(header->rsrvd),htons(header->length));

         DDCMSG(D_PARSE,CYAN,"\t\t\t\t\t\t\tmessage body\n"\
                "C-ID | Expos | Aspct |  Dir | Move |  Speed | On/Off | Hits | React | ToKill | Sens | Mode | Burst\n"\
                "%3i    %3i     %3i     %2i    %3i    %7.2f     %4i     %2i     %3i     %3i     %3i    %3i   %5i ",
                message_2100->cid,message_2100->exp,message_2100->asp,message_2100->dir,message_2100->move,message_2100->speed
                ,message_2100->on,htons(message_2100->hit),message_2100->react,htons(message_2100->tokill),htons(message_2100->sens)
                ,message_2100->mode,htons(message_2100->burst));
         // do the event that was requested

         switch (message_2100->cid) {
            case CID_No_Event:
               DDCMSG(D_PACKET,BLUE,"Minion %i: CID_No_Event  send 'S'uccess ack",minion->mID) ; 
               // send 2101 ack
               send_2101_ACK(header,'S',minion);
               break;

            case CID_Reserved01:
               // send 2101 ack
               DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Reserved01  send 'F'ailure ack",minion->mID) ;
               send_2101_ACK(header,'F',minion);
               break;

            case CID_Status_Request:
               // send 2102 status
               DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Status_Request   send 2102 status  hdr->num=%i, hdr->seq=%i",minion->mID,
                      htons(header->num),htonl(header->seq));

                        DDCMSG(D_POINTER|D_NEW, YELLOW, "calling sendStatus2102(%i) @ %i", minion->S.exp.data, __LINE__);
               sendStatus2102(1,header,minion,mt); // forces sending of a 2102
               // AND/OR? send 2115 MILES shootback status if supported
               //                           if (acc_conf.acc_type == ACC_NES_MFS){
               //                               DCMSG(BLUE,"we also seem to have a MFS Muzzle Flash Simulator - TODO send 2112 status eventually") ; 
               //                           }
               if (minion->S.cap&PD_NES){
                  DDCMSG(D_PACKET,BLUE,"Minion %i: we also seem to have a MFS Muzzle Flash Simulator - send 2112 status",minion->mID) ; 
                  // AND send 2112 Muzzle Flash status if supported   
                  sendStatus2112(0,header,minion); // send of a 2112 (0 means unsolicited, 1 copys mnum and seq to resp_mnum and resp_seq)
               }


//               this might be the one place that LBC_status_req is still needed or should happen outside RFmaster.
#if 0
               //                   also build an LB packet  to send
               LB_status_req  =(LB_status_req_t *)&LB_buf;      // make a pointer to our buffer so we can use the bits right
               LB_status_req->cmd=LBC_STATUS_REQ;
               LB_status_req->addr=minion->RF_addr;

               minion->S.status.flags=F_told_RCC;
               minion->S.status.timer=20;
               // now send it to the MCP master
               DDCMSG(D_PACKET,BLUE,"Minion %i:  LB_status_req cmd=%i", minion->mID,LB_status_req->cmd);

               psend_mcp(minion,LB_status_req);
               
#if 0 /* start of old state timer code */
               // I'm expecting a response within 2 seconds
               minion->S.rf_t.flags=F_rf_t_waiting_short;
               minion->S.rf_t.timer=20; // give it two seconds
#endif /* end of old state timer code */
#endif
               break;

            case CID_Expose_Request:
               DDCMSG(D_PACKET,BLACK,"Minion %i: CID_Expose_Request  send 'S'uccess ack.   message_2100->exp=%i",minion->mID,message_2100->exp);
               //                   also build an LB packet  to send
               if (message_2100->exp==90){
                  DDCMSG(D_PACKET,BLUE,"Minion %i:  going up (exp=%i)",minion->mID,message_2100->exp);
                  DDCMSG(D_PACKET,BLACK,"Minion %i: we seem to work here******************* &LB_buf=%p",minion->mID,&LB_buf);
                        DDCMSG(D_POINTER|D_NEW, black, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\ncalling send_LB_exp @ %i", __LINE__);
                  send_LB_exp(minion, 1, mt); // do an expose, don't just change states and send "event" message
               } else {
                  int uptime;
                  DDCMSG(D_PACKET,BLACK,"Minion %i: going down(exp=%i)",minion->mID,message_2100->exp);

                  // we must assume that this 'conceal' followed the last expose.
                  //        so now the event is over and we need to send the qconceal command
                  //        and the termination of the event time and stuff
                  // calculate uptime based on the time between commands
                  // TODO -- fix uptime based on something smarter than "difference from now" ???
                  uptime=(mt->elapsed_time.tv_sec - (minion->S.exp.cmd_start_time[minion->S.exp.event]%1000))*10; //  find uptime in tenths of a second
                  uptime+=(mt->elapsed_time.tv_nsec - (minion->S.exp.cmd_start_time[minion->S.exp.event]*1000000l))/100000000L;      //  find uptime in tenths of a second

                  if (uptime>0x7FF){
                     uptime=0x7ff;              // set time to max (204.7 seconds) if it was too long
                  } else {
                     uptime&= 0x7ff;
                  }

                  // ... furthermore, we also assume that this command will be received on
                  //         the downstream end and responded to from that end in the same
                  //         relative timeframe as the original expose message. if there
                  //         are significant delays at either end of the spectrum, we can
                  //         only assume that the unqualified hits we receive will be logged
                  //         correctly when we use them
                  // log the information on when we sent the conceal message to the target
                  DDCMSG(D_POINTER, GREEN, "CID_expose_request changing end to %3i.%03i from %3i.%03i)", DEBUG_MS(minion->S.exp.cmd_end_time[minion->S.exp.event]), DEBUG_TS(mt->elapsed_time));
                  minion->S.exp.cmd_end_time[minion->S.exp.event] = ts2ms(&mt->elapsed_time);

                  DDCMSG(D_VERY,BLACK,"Minion %i: uptime=%i",minion->mID,uptime);

#if 0 // probably shouldn't send here as it won't change event numbers, just end the existing one
                  if (minion->S.exp.data == 45) {
                     // if we are sending a new command before receiving an update, send status back to SR now
                     if (minion->S.exp.newdata == 90) {
                        minion->S.exp.lastdata = 0; // force sending of status by messing with cache
                        minion->S.exp.data = 90; // we're now exposed
                     } else {
                        minion->S.exp.lastdata = 0; // force sending of status by messing with cache
                        minion->S.exp.data = 90; // we're now exposed
                     }
                     DCMSG(GRAY, "Sending fake 2102 expose: S.exp.newdata=%i, S.exp.data=%i @ %i", minion->S.exp.newdata, minion->S.exp.data, __LINE__);
                     sendStatus2102(0, NULL,minion,&mt);
                     // change event numbers on the target
                     send_LB_exp(minion, 0, &mt); // don't expose, just change events and state (switches exp.newdata = 45, fix if necessary below)
                     // we'll change to transistioning again below
                  }
#endif // probably shouldn't send here as it won't change event numbers, just end the existing one
                  minion->S.exp.newdata=0; // moving to concealed
                  minion->S.exp.recv_dec = 0;
#if 0 /* start of old state timer code */
                  minion->S.exp.flags=F_exp_conceal_A;  // start (faking FASIT) moving to conceal
                  minion->S.exp.timer=5;
#endif /* end of old state timer code */

                  DDCMSG(D_VERY,BLACK,"Minion %i: &LB_buf=%p",minion->mID,&LB_buf);                     
                  LB_qcon =(LB_qconceal_t *)qbuf;    // make a pointer to our buffer so we can use the bits right
                  DDCMSG(D_VERY,BLACK,"Minion %i: map packet in",minion->mID);

                  LB_qcon->cmd=LBC_QCONCEAL;
                  LB_qcon->addr=minion->RF_addr;
                  LB_qcon->event=minion->S.exp.event;
                  LB_qcon->uptime=uptime;
                  
                  DDCMSG(D_PACKET,BLUE,"Minion %i:  LB_qcon cmd=%i", minion->mID,LB_qcon->cmd);
                  psend_mcp(minion,LB_qcon);

                  // we also need a report for this event from the target, so we will have to send a request_rep packet soon.
                  // just go ahead and send the report request now, too
                  //                  build an LB packet  report_req packet
#if 0 /* start of old state timer code */
                  {
                     LB_report_req_t *L=(LB_report_req_t *) rbuf;

                     L->cmd=LBC_REPORT_REQ;
                     L->addr=minion->RF_addr;
                     L->event=minion->S.exp.event;
                     minion->S.exp.last_event=minion->S.exp.event; // save this event

                     DDCMSG(D_PACKET,BLUE,"Minion %i:  LB_report_req cmd=%i", minion->mID,L->cmd);
                     psend_mcp(minion,L);

                     minion->S.event.flags = 0;
                     minion->S.event.timer = 0;
                        // TODO -- what do we do when we don't receive a report?
                        //minion->S.event.flags=F_waiting_for_report;
                        //minion->S.event.timer = 20;   // lets try 5.0 seconds
                  }
#endif /* end of old state timer code */

                  // new state timer code
                  minion->S.exp.data=45;  // make the current positon in movement
                  DCMSG(GRAY, "Send 2100 expose: S.exp.newdata=%i, S.exp.data=%i @ %i", minion->S.exp.newdata, minion->S.exp.data, __LINE__);
                  START_CONCEAL_TIMER(minion->S);
               }

               // send 2101 ack  (2102's will be generated at start and stop of actuator)
               send_2101_ACK(header,'S',minion);    // TRACR Cert complains if these are not there

               // it should happen in this many deciseconds
               //  - fasit device cert seems to come back immeadiately and ask for status again, and
               //    that is causing a bit of trouble.
               break;

            case CID_Reset_Device: {
               LB_reset_t LBr;
               // send 2101 ack
               DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Reset_Device  send 'S'uccess ack.   set lastHitCal.* to defaults",minion->mID) ;
               send_2101_ACK(header,'S',minion);
               // this opens a serious can of worms, should probably handle via minion telling RFmaster to set the forget bit and resetting its own state
               LBr.cmd = LBC_RESET;
               LBr.addr=minion->RF_addr;
               psend_mcp(minion,&LBr);
               DISCONNECT; // will reset minion state properly

               // also supposed to reset all values to the 'initial exercise step value'
               //  which I am not sure if it is different than ordinary inital values 
               //                           fake_sens = 1;
#if 0
               //                               lastHitCal.seperation = 250;   //250;
               minion->S.burst.newdata = htons(250);
               minion->S.burst.flags |= F_tell_RF;      // just note it was set

               minion->S.sens.newdata = cal_table[13]; // fairly sensitive, but not max
               minion->S.sens.flags |= F_tell_RF;       // just note it was set

               //lastHitCal.blank_time = 50; // half a second blanking

               //lastHitCal.enable_on = BLANK_ALWAYS; // hit sensor off
               minion->S.on.newdata  = 0;       // set the new value for 'on'
               minion->S.on.flags  |= F_tell_RF;        // just note it was set

               //lastHitCal.hits_to_kill = 1; // kill on first hit
               minion->S.tokill.newdata = htons(1);
               minion->S.tokill.flags |= F_tell_RF;     // just note it was set

               //lastHitCal.after_kill = 0; // 0 for stay down
               minion->S.react.newdata = 0; // 0 for stay down
               minion->S.react.flags |= F_tell_RF;      // just note it was set

               //lastHitCal.type = 1; // mechanical sensor
               //lastHitCal.invert = 0; // don't invert sensor input line
               //lastHitCal.set = HIT_OVERWRITE_ALL;    // nothing will change without this
               //doHitCal(lastHitCal); // tell kernel
               //doHits(0); // set hit count to zero

               //minion->S.mode.newdata = message_2100->mode;
               //minion->S.mode.flags |= F_tell_RF;     // just note it was set
#endif
            }   break;

               /* handle emergency stop the same as a move request */               
            case CID_Stop: 

            case CID_Move_Request:
               DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request  send 'S'uccess ack.   message_2100->speed=%f, message_2100->move=%i",minion->mID,message_2100->speed, message_2100->move);
               //                           also build an LB packet  to send
               LB_move =(LB_move_t *)&LB_buf;   // make a pointer to our buffer so we can use the bits right
               LB_move->cmd=LBC_MOVE;
               LB_move->addr=minion->RF_addr;

               //                               minion->S.exp.data=0;                   // cheat and set the current state to 45
               minion->S.speed.newdata=message_2100->speed;     // set newdata to be the future state
               minion->S.move.newdata=message_2100->move;       // set newdata to be the future state
#if 0 /* start of old state timer code */
               minion->S.move.flags=1;
               minion->S.move.timer=10;
#endif /* end of old state timer code */

               LB_move->speed = ((int)(max(0.0,min(htonf(message_2100->speed), 20.0)) * 100)) & 0x7ff;
               DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: speed after: %i",minion->mID, LB_move->speed);
               if (message_2100->move == 2) {
                  DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: direction 2",minion->mID);
                  LB_move->move = 2;
               } else if (message_2100->move ==1) {
                  DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: direction 1",minion->mID);
                  LB_move->move = 1;
               } else {
                  DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: direction 0",minion->mID);
                  LB_move->move = 0;
                  LB_move->speed = 0;
               }
               if (message_2100->cid == CID_Stop) {
                  DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: E-Stop",minion->mID);
                  LB_move->speed = 2047; // emergency stop speed
               }

               // now send it to the MCP master
               DDCMSG(D_PACKET,BLUE,"Minion %i:  LB_move cmd=%i", minion->mID,LB_move->cmd);
               psend_mcp(minion,LB_move);

               // send 2101 ack  (2102's will be generated at start and stop of actuator)
               send_2101_ACK(header,'S',minion);    // TRACR Cert complains if these are not there

               // it should happen in this many deciseconds
               //  - fasit device cert seems to come back immeadiately and ask for status again, and
               //    that is causing a bit of trouble.              break;
               break;

            case CID_Config_Hit_Sensor:
               DDCMSG(D_PARSE,BLUE,"Minion %i: CID_Config_Hit_Sensor  send a 2102 in response",minion->mID);
               DDCMSG(D_PARSE,BLUE,"Full message decomposition....");
               DDCMSG(D_PARSE,BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6i  %i.%i  %6i  %6i  %7i"
                      ,htons(header->num),htons(header->icd1),htons(header->icd2),htonl(header->seq),htons(header->rsrvd),htons(header->length));

               DDCMSG(D_PARSE,BLUE,"\t\t\t\t\t\t\tmessage body\n"\
                      "C-ID | Expos | Aspct |  Dir | Move |  Speed | On/Off | Hits | React | ToKill | Sens | Mode | Burst\n"\
                      "%3i    %3i     %3i     %2i    %3i    %7.2f     %4i     %2i     %3i     %3i     %3i    %3i   %5i "
                      ,message_2100->cid,message_2100->exp,message_2100->asp,message_2100->dir,message_2100->move,message_2100->speed
                      ,message_2100->on,htons(message_2100->hit),message_2100->react,htons(message_2100->tokill),htons(message_2100->sens)
                      ,message_2100->mode,htons(message_2100->burst));

               //                   also build an LB packet  to send
               LB_configure =(LB_configure_t *)&LB_buf; // make a pointer to our buffer so we can use the bits right
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

               // now send it to the MCP master
               DDCMSG(D_PACKET,BLUE,"Minion %i:  LB_configure cmd=%i", minion->mID,LB_configure->cmd);
               psend_mcp(minion,LB_configure);

               //      actually Riptide says that the FASIT spec is wrong and should not send an ACK here
               //                               send_2101_ACK(header,'S',minion); // FASIT Cert seems to complain about this ACK

               // send 2102 status - after doing what was commanded
               // which is setting the values in the hit_calibration structure
               // uses the lastHitCal so what we set is recorded
               // there are fields that don't match up with the RF slaves, but we will match up our states
               // TODO I believe a 2100 config hit sensor is supposed to set the hit count
               // BDR:   I think the lastHitCal won't be needed as that is stuff on the lifter board, and we
               //        won't need to simulate at that low of a level.

               minion->S.on.newdata  = message_2100->on;        // set the new value for 'on'
//               minion->S.on.flags  |= F_tell_RF;        // just note it was set

               if (htons(message_2100->burst)) {
                  minion->S.burst.newdata = htons(message_2100->burst);      // spec says we only set if non-Zero
//                  minion->S.burst.flags |= F_tell_RF;   // just note it was set
               }
               if (htons(message_2100->sens)) {
                  minion->S.sens.newdata = htons(message_2100->sens);
//                  minion->S.sens.flags |= F_tell_RF;    // just note it was set
               }
               // remember stated value for later
               //                               fake_sens = htons(message_2100->sens);

               if (htons(message_2100->tokill)) {
                  minion->S.tokill.newdata = htons(message_2100->tokill);
//                  minion->S.tokill.flags |= F_tell_RF;  // just note it was set
               }

               //                               if (htons(message_2100->hit)) {
               // minion->S.hit.newdata = htons(message_2100->hit); -- TODO -- new hit time message
//               minion->S.hit.flags |= F_tell_RF;
               //                               }
               minion->S.react.newdata = message_2100->react;
//               minion->S.react.flags |= F_tell_RF;      // just note it was set

               minion->S.mode.newdata = message_2100->mode;
//               minion->S.mode.flags |= F_tell_RF;       // just note it was set

               minion->S.burst.newdata = htons(message_2100->burst);
//               minion->S.burst.flags |= F_tell_RF;      // just note it was set

               //                               doHitCal(lastHitCal); // tell kernel by calling SIT_Clients version of doHitCal
               DDCMSG(D_PACKET,BLUE,"Minion %i: calling doHitCal after setting values",minion->mID) ;

               // send 2102 status or change the hit count (which will send the 2102 later)
               if (1 /*hits == htons(message_2100->hit)*/) {
                        DDCMSG(D_POINTER|D_NEW, YELLOW, "calling sendStatus2102(%i) @ %i", minion->S.exp.data, __LINE__);
                  sendStatus2102(1,header,minion,mt);  // sends a 2102 as we won't if we didn't change the the hit count
                  DDCMSG(D_PACKET,BLUE,"Minion %i: We will send 2102 status in response to the config hit sensor command",minion->mID);

               } else {
                  //    doHits(htons(message_2100->hit));    // set hit count to something other than zero
                  DDCMSG(D_PACKET,BLUE,"Minion %i: after doHits(%i) ",minion->mID,htons(message_2100->hit)) ;
               }

               break;

            case CID_GPS_Location_Request:
               DDCMSG(D_PACKET,BLUE,"Minion %i: CID_GPS_Location_Request  send 'F'ailure ack  - because we don't support it",minion->mID) ;
               send_2101_ACK(header,'F',minion);

               // send 2113 GPS Location
               break;

            case CID_Sleep:
            case CID_Wake:
            case CID_Shutdown:
               //                   also build an LB packet  to send
               LB_power =(LB_power_control_t *)&LB_buf; // make a pointer to our buffer so we can use the bits right
               LB_power->cmd=LBC_POWER_CONTROL;
               LB_power->addr=minion->RF_addr;

               switch (message_2100->cid) {
                  case CID_Sleep:
                     DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Sleep...sleeping",minion->mID) ; 
                     LB_power->pcmd = 1;
                     break;
                  case CID_Wake:
                     DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Wake...waking",minion->mID) ; 
                     LB_power->pcmd = 2;
                     break;
                  case CID_Shutdown:
                     DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Shutdown...shutting down",minion->mID) ; 
                     LB_power->pcmd = 3;
                     break;
               }

               // now send it to the MCP master
               DDCMSG(D_PACKET,BLUE,"Minion %i:  LB_power cmd=%i", minion->mID,LB_power->cmd);
               psend_mcp(minion,LB_power);

               //  sent LB

               // send 2101 ack  (2102's will be generated at start and stop of actuator)
               send_2101_ACK(header,'S',minion);    // TRACR Cert complains if these are not there

               // it should happen in this many deciseconds
               //  - fasit device cert seems to come back immeadiately and ask for status again, and
               //    that is causing a bit of trouble.              break;

               break;
         }
         break;

      case 2110:
         message_2110 = (FASIT_2110*)(buf + sizeof(FASIT_header));

         DDCMSG(D_PACKET,BLUE,"Minion %i: fasit packet 2110 Configure_Muzzle_Flash, seq=%i  on=%i  mode=%i  idelay=%i  rdelay=%i"
                , minion->mID,htonl(header->seq),message_2110->on,message_2110->mode,message_2110->idelay,message_2110->rdelay);

         // check to see if we have muzzle flash capability -  or just pretend
         if (minion->S.cap&PD_NES){

            minion->S.mfs_on.newdata  = message_2110->on;       // set the new value for 'on'
//            minion->S.mfs_on.flags  |= F_tell_RF;       // just note it was set
            minion->S.mfs_mode.newdata  = message_2110->mode;   // set the new value for 'on'
//            minion->S.mfs_mode.flags  |= F_tell_RF;     // just note it was set
            minion->S.mfs_idelay.newdata  = message_2110->idelay;       // set the new value for 'on'
//            minion->S.mfs_idelay.flags  |= F_tell_RF;   // just note it was set
            minion->S.mfs_rdelay.newdata  = message_2110->rdelay;       // set the new value for 'on'
//            minion->S.mfs_rdelay.flags  |= F_tell_RF;   // just note it was set

            //doMFS(msg->on,msg->mode,msg->idelay,msg->rdelay);
            // when the didMFS happens fill in the 2112, force a 2112 message to be sent
            sendStatus2112(1,header,minion); // forces sending of a 2112
            //                      sendStatus2102(0,header,minion,&mt); // forces sending of a 2102
            //sendMFSStatus = 1; // force
         } else {
            send_2101_ACK(header,'F',minion);  // no muzzle flash capability, so send a negative ack
         }

   }  /**   end of 'switch (packet_num)'   **/
}

void finishTransition(thread_data_t *minion) {
   if (minion->S.exp.data == 45 && minion->S.exp.newdata == 90) {
      // transitioned from conceal to expose
      START_EXPOSE_TIMER(minion->S); // will do non-45 based timer stuff
   } else if (minion->S.exp.data == 45 && minion->S.exp.newdata == 0) {
      // transitioned from expose to conceal
      START_CONCEAL_TIMER(minion->S); // will do non-45 based timer stuff
   } else if (minion->S.exp.data == 0) {
      // continues to be concealed
      //START_CONCEAL_TIMER(minion->S); // will do non-45 based timer stuff
   } else if (minion->S.exp.data == 90) {
      // continues to be exposed
      //START_EXPOSE_TIMER(minion->S); // will do non-45 based timer stuff
   } else {
      // invalid state -- shouldn't be able to get here
      DCMSG(RED, "ERROR - Should not be able to be from transition to nowhere: %i %i", minion->S.exp.data, minion->S.exp.newdata);
   }
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
   int i,msglen,result,seq,length,yes=1;
   fd_set rcc_or_mcp;

   struct iovec iov[2];

   FASIT_header rhdr;
   FASIT_2111    msg;
   FASIT_2100    *message_2100;
   FASIT_2110    *message_2110;
   FASIT_13110   *message_13110;

   LB_packet_t *LB;
   LB_device_reg_t *LB_devreg;
   LB_assign_addr_t *LB_addr;
   LB_expose_t *LB_exp;




   /*** actually, the capabilities and the devid's will come from the MCP -
    ***   it gets them when it sends out the request new devices over the RF
    *** so when we are spawned, those state fields are already filled in 
    ***/
   // now the mcp has already initialized a bunch of state...  initialize_state just has to init other things
   initialize_state( &minion->S);
//   minion->S.cap|=PD_NES;       // add the NES capability   this should be percolating through by itself now 

   DCMSG(BLUE,"Minion %i: state is initialized as devid 0x%06X", minion->mID,minion->devid);
   DCMSG(BLUE,"Minion %i: RF_addr = %i capabilities=%i  device_type=%i", minion->mID,minion->RF_addr,minion->S.cap,minion->S.dev_type);

   if (minion->S.cap&PD_NES) {
      DCMSG(BLUE,"Minion %i: has Night Effects Simulator (NES) capability", minion->mID);
   }
   if (minion->S.cap&PD_MILES) {
      DCMSG(BLUE,"Minion %i: has MILES (shootback) capability", minion->mID);
   }
   if (minion->S.cap&PD_GPS) {
      DCMSG(BLUE,"Minion %i: has Global Positioning System (GPS) capability", minion->mID);
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

   minion->S.state_timer=900;   // every 10.0 seconds, worst case

   clock_gettime(CLOCK_MONOTONIC,&mt.istart_time);  // get the intial current time
   minion->rcc_sock=-1; // mark the socket so we know to open again
   while(!close_nicely) {

      if (minion->rcc_sock<0) {
         // now we must get a connection or new connection to the range control
         // computer (RCC) using fasit.

         do {
            minion->rcc_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
            if(minion->rcc_sock < 0)   {
               PERROR("socket() failed");
            }

            result=connect(minion->rcc_sock,(struct sockaddr *) &fasit_addr, sizeof(struct sockaddr_in));
            if (result<0){
               close(minion->rcc_sock);
               strerror_r(errno,mb.buf,BufSize);
               DCMSG(RED,"Minion %i: fasit server not found! connect(...,%s:%i,...) error : %s  ", minion->mID,inet_ntoa(fasit_addr.sin_addr),htons(fasit_addr.sin_port),mb.buf);
               DCMSG(RED,"Minion %i:   waiting for a bit and then re-trying ", minion->mID);
               minion->rcc_sock=-1;     // make sure it is marked as closed
               sleep(10);               // adding a little extra wait
            } else {
               setsockopt(minion->rcc_sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int)); // set keepalive so we disconnect on link failure or timeout
            }

         } while (result<0);
         // actually we really shouldn't stay in the above loop if disconnected, we need to keep updating minion states
         //         and communicating with the RF slaves.
         //   also we need to loop through because if we are stuck in a loop above we will not see that the
         //   MCP has died, and we would become zombies.  
         // we now have a socket.
         DDCMSG(D_MINION,BLUE,"Minion %i: has a socket to a RCC", minion->mID);
      }


      /* create a fd_set so we can monitor both the mcp and the connection to the RCC*/
      FD_ZERO(&rcc_or_mcp);
      FD_SET(minion->mcp_sock,&rcc_or_mcp);             // we are interested hearing the mcp
      FD_SET(minion->rcc_sock,&rcc_or_mcp);             // we also want to hear from the RCC

      /* block for up to state_timer deciseconds waiting for RCC or MCP 
       * and we may drop through much faster if we have a MCP or RCC communication
       * that we have to process, otherwise we have to update state
       * stuff
       */

      mt.timeout.tv_sec=minion->S.state_timer/10;
      mt.timeout.tv_usec=100000*(minion->S.state_timer%10);

      timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);

      DDCMSG(D_TIME,CYAN,"Minion %i: before Select... state_timer=%i (%i.%03i) at %i.%03iet",
             minion->mID,minion->S.state_timer,(int)mt.timeout.tv_sec,(int)mt.timeout.tv_usec/1000,DEBUG_TS(mt.elapsed_time));

      sock_ready=select(FD_SETSIZE,&rcc_or_mcp,(fd_set *) 0,(fd_set *) 0, &mt.timeout); 
      // if we are running on linux, mt.timeout wll have a remaining time left in it if one of the file descriptors was ready.
      // we are going to use that for now

      oldtimer=minion->S.state_timer;           // copy our old time for debugging
      minion->S.state_timer=(mt.timeout.tv_sec*10)+(mt.timeout.tv_usec/100000L); // convert to deciseconds

      if (sock_ready<0){
         PERROR("NOTICE!  select error : ");
         exit(-1);
      }

      timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);
      DDCMSG(D_TIME,CYAN,"Minion %i: After Select: sock_ready=%i FD_ISSET(mcp)=%i FD_ISSET(rcc)=%i oldtimer=%i state_timer=%i at %i.%03iet, delta=%i.%03i",
             minion->mID,sock_ready,FD_ISSET(minion->mcp_sock,&rcc_or_mcp),FD_ISSET(minion->rcc_sock,&rcc_or_mcp),oldtimer,minion->S.state_timer,
             DEBUG_TS(mt.elapsed_time), DEBUG_TS(mt.delta_time));


      //check to see if the MCP has any commands for us
      if (FD_ISSET(minion->mcp_sock,&rcc_or_mcp)){
         msglen=read(minion->mcp_sock, mb.buf, 1); // grab first byte so we know how many to grab
         if (msglen == 1) {
            LB=(LB_packet_t *)mb.buf;   // map the header in
            msglen+=read(minion->mcp_sock, (mb.buf+1), RF_size(LB->cmd)-1); // grab the rest, but leave the rest to be read again later
            DDCMSG(D_NEW, RED, "Minion %i read %i bytes with %i cmd", minion->mID, msglen, LB->cmd);
            DDpacket(mb.buf,msglen);
         }
         if (msglen > 0) {
            mb.buf[msglen]=0;
            if (verbose&D_PACKET){      // don't do the sprintf if we don't need to             
               sprintf(mb.hbuf,"Minion %i: received %i from MCP (RF) ", minion->mID,msglen);
               DDCMSG_HEXB(D_PACKET,YELLOW,mb.hbuf,mb.buf,msglen);
            }

            // we received something over RF, so we haven't timed out: reset the rf timer to look in 5 minutes -- TODO -- make this timeout smarter, or at the very least, user configurable
#if 0 /* start of old state timer code */
            minion->S.rf_t.flags = F_rf_t_waiting_long;
            minion->S.rf_t.timer = 3000; // 5 minutes in deciseconds
#endif /* end of old state timer code */

            // new state timer code
            // we received something over RF, so we haven't timed out: reset the rf timers -- TODO -- make this timeout smarter, or at the very least, user configurable
            if (minion->S.rf_t.slow_flags != F_slow_none) {
               // reset the slow timer, if we're doing the slow timer, as we just got a response
               setTimerTo(minion->S.rf_t, slow_timer, slow_flags, SLOW_TIME, F_slow_start);
               minion->S.rf_t.slow_missed = 0;
            }
            if (minion->S.rf_t.fast_flags != F_fast_none) {
               // reset the fast timer miss counter, keep going on the same timer interval so they burst
               minion->S.rf_t.fast_missed = 0;
            }

            // we have received a message from the mcp, process it
            // it is either a command, or it is an RF response from our slave
            //          process_MCP_cmds(&state,mb.buf,msglen);

            LB=(LB_packet_t *)mb.buf;   // map the header in
            crc= crc8(LB);
            DDCMSG(D_RF,YELLOW,"Minion %i: LB packet (cmd=%2i aidr=%i crc=%i)", minion->mID,LB->cmd,LB->addr,crc);
            switch (LB->cmd){
               case LBC_REQUEST_NEW:
                  DDCMSG(D_NEW,YELLOW,"Minion %i: Recieved 'request new devices' packet.",minion->mID);
                  // the response was created when the MCP spawned us, so we do nothing for this packet

                  break;

               case LBC_ASSIGN_ADDR:
                  LB_addr =(LB_assign_addr_t *)(LB);    // map our bitfields in

                  minion->RF_addr=LB_addr->new_addr;    // set our new address
                  DDCMSG(D_NEW,YELLOW,"Minion %i: parsed 'device address' packet.  new minion->RF_addr=%i",minion->mID,minion->RF_addr);

                  break;

               case LBC_EVENT_REPORT:
               {
                  LB_packet_t ib;
                  report_memory_t *this_r=NULL, *last_r=NULL; // pointers to use with the report memory chain
                  LB_event_report_t *L=(LB_event_report_t *)(LB);       // map our bitfields in

                  // test that we can clear this from queue
                  LB_status_req_t Ls;
                  Ls.cmd = LBC_STATUS_REQ;
                  Ls.addr = minion->RF_addr;
                  psend_mcp(minion,&Ls);

                  //minion->S.hit.newdata+=L->hits;       // we need to add it to hit.newdata, it will be reset by SR or TRACR when they want to -- TODO -- new hit time message
                  DDCMSG(D_PARSE,YELLOW,"Minion %i: (Rf_addr=%i) parsed 'Event_report'. hits=%i  sending 2102 status",
                         minion->mID,minion->RF_addr,L->hits);

                  //DCMSG(RED, "****************\nFound expose: before:exp.data=%i, after:exp.newdata=%i\n**************** @ %i", minion->S.exp.data, minion->S.exp.newdata, __LINE__);
                  //minion->S.exp.data = minion->S.exp.newdata; // finish transition
                  //sendStatus2102(0, NULL,minion,&mt);

#if 0 /* bad idea, poorly implimented...don't clear status requests, just let the process work as designed */
                  // clear status requests ...
                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i", __FILE__, __LINE__);
                  ib.cmd = LBC_ILLEGAL_CANCEL;
                  ib.addr = minion->RF_addr;
                  DDCMSG(D_PACKET,BLUE,"Minion %i:  LBC_ILLEGAL_CANCEL cmd=%i addr=%i", minion->mID,ib.cmd, ib.addr);
                  psend_mcp(minion, &ib);
                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i", __FILE__, __LINE__);

                  // ... and reset the timing on future ones
                  if (minion->S.rf_t.slow_flags) {
                  DDCMSG(D_POINTER, GREEN, "Reset slow...%s:%i", __FILE__, __LINE__);
                     stopTimer(minion->S.rf_t, slow_timer, slow_flags);
                     setTimerTo(minion->S.rf_t, slow_timer, slow_flags, SLOW_TIME, F_slow_start);
                     minion->S.rf_t.slow_missed = 0;
                  }
                  if (minion->S.rf_t.fast_flags) {
                     DDCMSG(D_POINTER, GREEN, "Reset fast...%s:%i", __FILE__, __LINE__);
                     stopTimer(minion->S.rf_t, fast_timer, fast_flags);
                     setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_TIME, F_fast_start);
                     minion->S.rf_t.fast_missed = 0;
                  }
#endif /* ...end of bad idea code */

                  // new state timer code
                  // find existing report
                  this_r = minion->S.report_chain;
                  while (this_r != NULL) {
//                     DDCMSG(D_POINTER, GREEN, "Checking existing...%s:%i with %p r:%i:%i e:%i:%i", __FILE__, __LINE__, this_r, this_r->report, L->report, this_r->event, L->event);
                     if (this_r->report == L->report &&
                         this_r->event == L->event &&
                         this_r->hits == L->hits) { /* if we didn't actually send the hits before -- for instance, unqualified turning into qualified -- we set this_r->hits to 0, so this won't match and we'll send 16000 below */
                        DDCMSG(D_POINTER, GREEN, "Exists...%s:%i with %p", __FILE__, __LINE__, this_r);
                        break; // found it!
                     }
                     last_r = this_r;
                     this_r = this_r->next;
//                     DDCMSG(D_POINTER, GREEN, "Next...%s:%i with %p", __FILE__, __LINE__, this_r);
                  }
//                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i", __FILE__, __LINE__);

                  // did I find one? then don't send hit time message to FASIT server
                  if (this_r == NULL) {
                     int now_m, start_time_m, end_time_m, delta_m; // times in milliseconds for easy calculations
                     int send_hit = 0;
                     DDCMSG(D_POINTER, GREEN, "Allocing...%s:%i", __FILE__, __LINE__);
                     // none found, create one ...
                     this_r = malloc(sizeof(report_memory_t));
                     if (last_r != NULL) {
                        DDCMSG(D_POINTER, GREEN, "Chaining...%s:%i", __FILE__, __LINE__);
                        // place in chain correctly
                        last_r->next = this_r;
                     } else {
                        DDCMSG(D_POINTER, GREEN, "Heading...%s:%i", __FILE__, __LINE__);
                        // is head of chain
                        minion->S.report_chain = this_r;
                     }

                     // find start time and end times (calculate using when the FASIT server logged the times)
                     if (minion->S.exp.log_start_time[L->event] == 0) {
                        // this implies we never logged the start time on the FASIT server...which is bad
                        minion->S.exp.log_start_time[L->event] = minion->S.exp.cmd_start_time[L->event];
                        DDCMSG(D_POINTER, GREEN, "Start time never got logged...bad juju");
                     }
                     timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);      
                     now_m = ts2ms(&mt.elapsed_time);
                     start_time_m = minion->S.exp.log_start_time[L->event];
                     DDCMSG(D_POINTER, GREEN, "Now time ...%s:%i (changing %3i.%03i to %i)", __FILE__, __LINE__, DEBUG_TS(mt.elapsed_time), now_m);
                     DDCMSG(D_POINTER, GREEN, "Start time ...%s:%i (changing %3i.%03i to %i)", __FILE__, __LINE__, DEBUG_MS(minion->S.exp.log_start_time[L->event]), start_time_m);
                     if (L->qualified) {
                        // qualified hits occured between valid expose and conceal commands, use those times
                        end_time_m = minion->S.exp.cmd_end_time[L->event];
                        DDCMSG(D_POINTER, GREEN, "Qual hit...%s:%i cmd end time (changing %3i.%03i to %i)", __FILE__, __LINE__, DEBUG_MS(minion->S.exp.cmd_end_time[L->event]), end_time_m);
                        send_hit = 1; // yes, send the hit time message
                     } else {
                        DDCMSG(D_POINTER, GREEN, "Checking qual...%s:%i", __FILE__, __LINE__);
                        // unqualified hits occurred after a valid expose, but the conceal hasn't been sent yet...
                        end_time_m = now_m; // end time for unqualified is now
                        // ... or has it? if it has, don't do anything now...wait for the hit to change to qualified
                        if (minion->S.exp.cmd_end_time[L->event] == 0) {
                           DDCMSG(D_POINTER, GREEN, "Unqual hit...%s:%i", __FILE__, __LINE__);
                           // no conceal sent, this hit is now qualified
                           send_hit = 1;
                        } else {
                           // conceal sent, let the target decide if the hit was qualified or not
                           DDCMSG(D_POINTER, GREEN, "Requal unhit...%s:%i", __FILE__, __LINE__);
                           send_hit = 0;
                        }
                     }

                     // change start/end times to deltas from now
                     DDCMSG(D_NEW, GREEN, "Minion %i: Non-hit time: s:%3i.%03i event:%i", minion->mID, DEBUG_MS(minion->S.exp.cmd_start_time[L->event]), L->event);
                     DDCMSG(D_NEW, GREEN, "Minion %i: Hit times: n:%i s:%i e:%i", minion->mID, now_m, start_time_m, end_time_m);
                     start_time_m = now_m - start_time_m;
                     end_time_m = now_m - end_time_m;
                     DDCMSG(D_NEW, GREEN, "Minion %i: Hit times: n:%i s:%i e:%i", minion->mID, now_m, start_time_m, end_time_m);

                     // send if good hit
                     if (send_hit) {
                        if (L->qualified) {
                           // half way between original start and end...
                           //        (which are now reversed in size as they are "time since now,"
                           //         and start happened further away from now)
                           // ... which is the end delta + the average of the two deltas
                           delta_m = end_time_m + (start_time_m-end_time_m)/2;
                        } else {
                           // unqualified with send_hit set = we're still up, count as "now"
                           delta_m = 0;
                        }
                        DDCMSG(D_NEW, GREEN, "Minion %i: Hit times: n:%i s:%i e:%i d:%i", minion->mID, now_m, start_time_m, end_time_m, delta_m);
                        // send hit time message
                        sendStatus16000(minion, L->hits, delta_m);
                        DDCMSG(D_NEW, GREEN, "Minion %i: Sent %i hits", minion->mID, L->hits);
                     } else {
                        DDCMSG(D_NEW, GREEN, "Minion %i: Didn't send %i hits", minion->mID, L->hits);
                        L->hits = 0; // don't "ack" these hits
                     }
                  }
//                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i", __FILE__, __LINE__);
                  
                  // set data, reset the counters
                  this_r->report = L->report;
                  this_r->event = L->event;
                  this_r->hits = L->hits;
                  this_r->unreported = 0;

                  // only send the ack if we have anything to acknowledge
                  if (this_r->hits > 0) {
                     setTimerTo(this_r->s, timer, flags, EVENT_SOON_TIME, F_event_ack); // send an ack back for this report
                     DDCMSG(D_POINTER, GREEN, "Going to ack...%s:%i", __FILE__, __LINE__);
                  }
                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i", __FILE__, __LINE__);

                  // move forward counters on *all* remembered reports, vacuuming as needed
                  //   -- NOTE -- the MAX value is set so that we should move these forward *each* time we get
                  //              any report message as there will be a maximum number per burst
                  this_r = minion->S.report_chain;
                  while (this_r != NULL) {
//                  DDCMSG(D_POINTER, GREEN, "Incr...%s:%i with %p", __FILE__, __LINE__, this_r);
                     // check to see if this link needs to be vacuumed
                     if (++this_r->unreported >= EVENT_MAX_UNREPORT) {
//                  DDCMSG(D_POINTER, GREEN, "Too much...%s:%i with %p", __FILE__, __LINE__, this_r);
                        // connect the chain links and free the memory of the old one
                        report_memory_t *temp = this_r->next;
                        free(this_r);
                        this_r = temp;
                        continue; // loop again
                     }
                     // move to next link in chain
                     last_r = this_r;
                     this_r = this_r->next;
//                  DDCMSG(D_POINTER, GREEN, "Next...%s:%i with %p", __FILE__, __LINE__, this_r);
                  }
//                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i with %p", __FILE__, __LINE__, this_r);
               }
               break;

               case LBC_STATUS_RESP:
               {
                  LB_status_resp_t *L=(LB_status_resp_t *)(LB); // map our bitfields in
                  // double check that the exposure is correct (it might be an old response coming in later than our exposure command, but before the change in exposure finished)
                  if (minion->S.exp.data == 45 && /* to be ignored we must be currently transistioning... */
                      ((L->expose == 1 && minion->S.exp.newdata == 0) || /* and either up when we should be down... */
                      (L->expose == 0 && minion->S.exp.newdata == 90)) && /* or down when we should be up... */
                      L->fault == 0 && /* and have no faults... */
                      !(L->did_exp_cmd && !minion->S.exp.recv_dec)) { /* and not have transitioned completely between status responses */
                     DDCMSG(D_NEW, GRAY, "Ignoring status response with strange exposure value %i. Should be %i.",
                            (L->expose?90:0), minion->S.exp.newdata);
                     // check again soon
                     if (minion->S.rf_t.slow_flags) {
                        setTimerTo(minion->S.rf_t, slow_timer, slow_flags, SLOW_SOON_TIME, F_slow_start);
                     } else if (minion->S.rf_t.fast_flags) {
                        setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_SOON_TIME, F_fast_start);
                     } else {
                        START_STANDARD_LOOKUP(minion->S);
                     }
                     // ignore out-of-sequence responses
                     break;
                  }
#if 0 /* start of old state timer code */
                  minion->S.exp.flags=F_exp_ok;
#endif /* end of old state timer code */
                  // is this our first response for our current event? this is when the FASIT server logged the start time
                  timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);      
                  if (minion->S.exp.log_start_time[L->event] == 0) {
                     DDCMSG(D_POINTER|D_NEW, YELLOW, "Getting around to starting event %i @ %3i.%03i", L->event, DEBUG_TS(mt.elapsed_time));
                     // change our event start to match the event times on the target (delay notwithstanding)
                     minion->S.exp.log_start_time[L->event] = ts2ms(&mt.elapsed_time); // remember "up" time for event
                  }

                  if (L->did_exp_cmd && !minion->S.exp.recv_dec) {
                     DDCMSG(D_NEW, MAGENTA, "Abnormal status response...doing abnormal stuff...%i %i %i", minion->S.exp.data, minion->S.exp.newdata, L->expose);
                     // abnormal message, let's no abnormal stuff
                        DDCMSG(D_POINTER|D_NEW, black, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\ncalling send_LB_exp @ %i", __LINE__);
                     send_LB_exp(minion, 0, &mt); // don't expose, just end current event and state (switches exp.newdata = 45, fix if necessary below)
                     if (L->expose == 1) { /* ended up exposed */
                        // we transitioned up-down-up or down-up-down-up...etc.
                        // send "down" status to FASIT server and do standard "down" stuff, standard "up" stuff is below, and will happen after our next status response
                        minion->S.exp.lastdata = 90; // force sending of status by messing with cache
                        minion->S.exp.data = 0; // we're now exposed
                        DCMSG(GRAY, "Sending fake 2102 expose: S.exp.newdata=%i, S.exp.data=%i @ %i", minion->S.exp.newdata, minion->S.exp.data, __LINE__);
                        DDCMSG(D_POINTER|D_NEW, YELLOW, "calling sendStatus2102(%i) @ %i", minion->S.exp.data, __LINE__);
                        sendStatus2102(0, NULL,minion,&mt);
                        minion->S.exp.data = 45; // we're concealing again
                        DCMSG(GRAY, "Sending fake 2102 expose: S.exp.newdata=%i, S.exp.data=%i @ %i", minion->S.exp.newdata, minion->S.exp.data, __LINE__);
                        DDCMSG(D_POINTER|D_NEW, YELLOW, "calling sendStatus2102(%i) @ %i", minion->S.exp.data, __LINE__);
                        sendStatus2102(0, NULL,minion,&mt);
                        if (minion->S.rf_t.fast_flags) {
                           // we're down, change state to going slow
                           minion->S.rf_t.slow_flags = F_fast_once;
                           START_STANDARD_LOOKUP(minion->S);
                        } else {
                           // set timer to come back soon
                           stopTimer(minion->S.rf_t, fast_timer, fast_flags);
                           setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_SOON_TIME, F_fast_start);
                           minion->S.rf_t.fast_missed = 0;
                        }
                     } else { /* ended up concealed */
                        // we transitioned down-up-down (most common reason was we got hit soon after expose) or up-down-up-down...etc.
                        // send "up" status to FASIT server and do standard "up" stuff, standard "down" stuff is below, and will happen after our next status response
                        minion->S.exp.newdata = 0; // fix the send_LB_exp messing with this
                        minion->S.exp.lastdata = 0; // force sending of status by messing with cache
                        minion->S.exp.data = 90; // we're now concealed
                        DCMSG(GRAY, "Sending fake 2102 expose: S.exp.newdata=%i, S.exp.data=%i @ %i", minion->S.exp.newdata, minion->S.exp.data, __LINE__);
                        DDCMSG(D_POINTER|D_NEW, YELLOW, "calling sendStatus2102(%i) @ %i", minion->S.exp.data, __LINE__);
                        sendStatus2102(0, NULL,minion,&mt);
                        minion->S.exp.data = 45; // we're exposing again
                        DCMSG(GRAY, "Sending fake 2102 expose: S.exp.newdata=%i, S.exp.data=%i @ %i", minion->S.exp.newdata, minion->S.exp.data, __LINE__);
                        DDCMSG(D_POINTER|D_NEW, YELLOW, "calling sendStatus2102(%i) @ %i", minion->S.exp.data, __LINE__);
                        sendStatus2102(0, NULL,minion,&mt);
                        // set timer to come back soon
                        stopTimer(minion->S.rf_t, fast_timer, fast_flags);
                        setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_SOON_TIME, F_fast_start);
                        minion->S.rf_t.fast_missed = 0;
                     }
                     DDCMSG(D_NEW, MAGENTA, "At end of abnormality, msg:%i last:%i curr:%i new:%i", L->expose, minion->S.exp.lastdata, minion->S.exp.data, minion->S.exp.newdata);
                     minion->S.exp.recv_dec = 1; // next time we receive we won't be abnormal
                  } else {
                     DDCMSG(D_NEW, MAGENTA, "Normal status response...doing normal stuff...%i %i %i", minion->S.exp.data, minion->S.exp.newdata, L->expose);
                     // normal message, let's do normal stuff
                     if (minion->S.exp.data == 0 && L->expose) {
                        // random movement from down to up, should send "up" command and start new event
                        DDCMSG(D_POINTER|D_NEW, black, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\ncalling send_LB_exp @ %i", __LINE__);
                        send_LB_exp(minion, 0, &mt); // don't expose, just end current event and state
                     } else if (!L->expose && minion->S.rf_t.fast_flags) {
                        // we're down, change state to going slow
                        START_CONCEAL_TIMER(minion->S);
                     }

                     // new state timer code
                     // finish transition
                     finishTransition(minion);

                     DCMSG(GRAY, "Received expose RESP: before:exp.data=%i, after:L->expose=%i @ %i", minion->S.exp.data, L->expose, __LINE__);
                     minion->S.exp.data=L->expose ? 90 : 0; // convert to only up or down
#if 0 /* start of old state timer code */
                     minion->S.exp.timer=0;
#endif /* end of old state timer code */
                     
                     minion->S.speed.data = (float)(L->speed / 100.0); // convert back to correct float
                     minion->S.move.data = L->move;
                     minion->S.react.newdata = L->react;
                     minion->S.tokill.newdata = L->tokill;
                     minion->S.mode.newdata = L->hitmode ? 2 : 1; // back to burst/single
                     minion->S.sens.newdata = L->sensitivity;
                     minion->S.position.data = L->location;
                     minion->S.burst.newdata = L->timehits * 5; // convert back
                     minion->S.fault.data = L->fault;
                     DDCMSG(D_PARSE,YELLOW,"Minion %i: (Rf_addr=%i) parsed response. location=%i fault=%02X sending 2102 status",
                            minion->mID,minion->RF_addr,L->location,L->fault);
                        DDCMSG(D_POINTER|D_NEW, YELLOW, "calling sendStatus2102(%i) @ %i", minion->S.exp.data, __LINE__);
                     sendStatus2102(0, NULL,minion,&mt);
                  }
               }

                  // reset lookup timer
#if 0 /* start of old state timer code */
               if (minion->S.exp.flags == F_exp_expose_D) {
                  minion->S.exp.flags=F_exp_expose_C;        // we have reached the exposed position, now ask for an update
                  minion->S.exp.timer=15;    // 1.5 second later
               }
#endif /* end of old state timer code */
               break;

               default:
                  DDCMSG(D_RF,YELLOW,"Minion %i:  recieved a cmd=%i    don't do anything",minion->mID,LB->cmd);

                  break;
            }  // switch LB cmd


         } else if (msglen<0) {
            if (errno!=EAGAIN){
               DCMSG(RED,"Minion %i: read returned %i errno=%i socket to MCP closed, we are FREEEEEeeee!!!",minion->mID,msglen,errno);
               exit(-2);  /* this minion dies!   it should do something else maybe  */
            }
         }else {
            DCMSG(RED,"Minion %i: socket to MCP closed, we are FREEEEEeeee!!!", minion->mID);
            exit(-2);  /* this minion dies!  possibly it should do something else - but maybe it dies of happyness  */
         }

         timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);    
         DDCMSG(D_TIME,CYAN,"Minion %i: End of MCP Parse at %i.%03iet, delta=%i.%03i"
                ,minion->mID, DEBUG_TS(mt.elapsed_time), DEBUG_TS(mt.delta_time));
      }

      /**********************************    end of reading and processing the mcp command  ***********************/

      /*************** check to see if there is something to read from the rcc   **************************/
      if (FD_ISSET(minion->rcc_sock,&rcc_or_mcp)){
         // now read it using the new routine    
         result = read_FASIT_msg(minion,mb.buf, BufSize);
         if (result>0){         
            mb.header = (FASIT_header*)(mb.buf);        // find out how long of message we have
            length=htons(mb.header->length);    // set the length for the handle function
            if (result>length){
               DDCMSG(D_PACKET,BLUE,"Minion %i: Multiple Packet  num=%i  result=%i seq=%i mb.header->length=%i",
                      minion->mID,htons(mb.header->num),result,htonl(mb.header->seq),length);
            } else {
               DDCMSG(D_PACKET,BLUE,"Minion %i: num=%i  result=%i seq=%i mb.header->length=%i",
                      minion->mID,htons(mb.header->num),result,htonl(mb.header->seq),length);
            }
            tbuf=mb.buf;                        // use our temp pointer so we can step ahead
            // loop until result reaches 0
            while((result>=length)&&(length>0)) {
               timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);      
               DDCMSG(D_TIME,CYAN,"Minion %i:  Packet %i recieved at %i.%03iet, delta=%i.%03i"
                      ,minion->mID,htons(mb.header->num), DEBUG_TS(mt.elapsed_time), DEBUG_TS(mt.delta_time));
               handle_FASIT_msg(minion,tbuf,length,&mt);
               result-=length;                  // reset the length to handle a possible next message in this packet
               tbuf+=length;                    // step ahead to next message
               mb.header = (FASIT_header*)(tbuf);       // find out how long of message we have
               length=htons(mb.header->length); // set the length for the handle function
               if (result){
                  DDCMSG(D_PACKET,BLUE,"Minion %i: Continue processing the rest of the BIG fasit packet num=%i  result=%i seq=%i  length=%i",
                         minion->mID,htons(mb.header->num),result,htonl(mb.header->seq),length);
               }
            }
         } else {
            strerror_r(errno,mb.buf,BufSize);
            DDCMSG(D_PACKET,RED,"Minion %i: read_FASIT_msg returned %i and Error: %s", minion->mID,result,mb.buf);
            DDCMSG(D_PACKET,RED,"Minion %i: which means it likely has closed!", minion->mID);
            minion->rcc_sock=-1;        // mark the socket so we know to open again

         }
         timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);    
         DDCMSG(D_TIME,CYAN,"Minion %i: End of RCC Parse at %i.%03iet, delta=%i.%03i"
                ,minion->mID, DEBUG_TS(mt.elapsed_time), DEBUG_TS(mt.delta_time));
      }
      /**************   end of rcc command parsing   ****************/
      /**  first we just update our counter, and if less then a tenth of a second passed,
       **  skip the timer updates and reloop, using a new select value that is a fraction of a tenth
       **
       **/

      minion_state(minion, &mt, &mb);

#if 0
      timestamp(&mt.elapsed_time,&mt.istart_time,&mt.delta_time);
      DDCMSG(D_TIME,CYAN,"Minion %i: End timer updates at %6li.%09li timestamp, delta=%1li.%09li"
             ,minion->mID,mt.elapsed_time.tv_sec, mt.elapsed_time.tv_nsec,mt.delta_time.tv_sec, mt.delta_time.tv_nsec);
#endif
   }  // while forever
   if (minion->rcc_sock > 0) { close(minion->rcc_sock); }
   if (minion->mcp_sock > 0) { close(minion->mcp_sock); }
}  // end of minion thread




