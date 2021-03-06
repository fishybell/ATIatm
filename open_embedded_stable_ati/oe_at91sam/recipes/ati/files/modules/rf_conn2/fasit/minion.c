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

#define __PROGRAM__ "MINION "

int resp_num,resp_seq;          // global for now.  not happy

const int cal_table[16] = {0xFFFFFFFF,333,200,125,75,60,48,37,29,22,16,11,7,4,2,1};

// globals that get inherited from the parent (MCP)
extern int verbose;
extern struct sockaddr_in fasit_addr;
extern int close_nicely;


// initialize our state to default values
void initialize_state(minion_state_t *S){

   S->ignoring_response = 0;

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
   S->resp.data = 0;
   S->resp.newdata = 0;
   S->resp.lastdata = 0;
   S->resp.last_exp = -1;
   S->resp.last_direction = -1;
   S->resp.last_speed = -1;
   S->resp.last_position = -100; // -1 through about -10 are valid position values, go way out
   S->resp.last_hit = -1;
   S->resp.current_exp = -1;
   S->resp.current_direction = -1;
   S->resp.current_speed = -1;
   S->resp.current_position = -1;
   S->resp.current_hit = -1;
   S->resp.ever_exp = 0;
   S->resp.ever_con = 0;
   S->resp.ever_move_left = 0;
   S->resp.ever_move_right = 0;
   S->resp.ever_stop = 0;
   S->resp.ever_hit = 0;
   S->resp.ever_move = 0;
   //S_set(resp,0,0,0,0); -- leave timers alone, we init with a timer running on purpose

   S->last_sequence_sent = -1;
   S->exp.data_uc = -1;
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

void Cancel_Status_Resp_Timer(thread_data_t *minion) {
   // cancel timer...
   stopTimer(minion->S.resp, timer, flags);
   // ... and reset the validity of the current data (will cause Handle_Status_Resp to ignore this data, but everywhere else should continue to use it as valid)
   minion->S.resp.newdata = 0;
   minion->S.resp.lastdata = 0;
   // ... and remove all "last" data
   minion->S.resp.last_exp = -1;
   minion->S.resp.last_direction = -1;
   minion->S.resp.last_speed = -1;
   minion->S.resp.last_position = -100; // -1 through about -10 are valid position values, go way out
   minion->S.resp.last_hit = -1;
   // ... and remove all "did we ever do this" statuses
   minion->S.resp.ever_exp = 0;
   minion->S.resp.ever_con = 0;
   minion->S.resp.ever_move_left = 0;
   minion->S.resp.ever_move_right = 0;
   minion->S.resp.ever_stop = 0;
   minion->S.resp.ever_hit = 0;
   minion->S.resp.ever_move = 0;
}

void Handle_Status_Resp(thread_data_t *minion, minion_time_t *mt) {
   // quick lookup macros -- so it is easier to follow logic below
   #define fake_movement_right() { \
      minion->S.move.newdata = 1; \
      minion->S.resp.mover_command = move_right; \
      START_MOVE_TIMER(minion->S); /* pretend to start moving */ \
   }
   #define fake_movement_stop() { \
      minion->S.move.newdata = 0; \
      minion->S.resp.mover_command = move_stop; \
      minion->S.speed.data = 0.0; \
      switch (minion->S.dev_type) { \
         default: \
         case Type_MIT: \
            setTimerTo(minion->S.move, timer, flags, MIT_MOVE_START_TIME, F_move_end_movement); \
            break; \
         case Type_MAT: \
            setTimerTo(minion->S.move, timer, flags, MAT_MOVE_START_TIME, F_move_end_movement); \
            break; \
      } \
   }
   #define fake_movement_left() { \
      minion->S.move.newdata = 2; \
      minion->S.resp.mover_command = move_left; \
      START_MOVE_TIMER(minion->S); /* pretend to start moving */ \
   }
   #define fake_transition_up() { \
      if (!faking_up) { \
         minion->S.resp.lifter_command = lift_expose; \
         START_EXPOSE_TIMER(minion->S); \
      } \
   }
   #define fake_transition_down() { \
      if (!faking_down) { \
         minion->S.resp.lifter_command = lift_conceal; \
         START_CONCEAL_TIMER(minion->S); \
      } \
   }
   #define create_fail_status(fail) { \
      DDCMSG(D_POINTER, CYAN, "___________________________________________________________\ncreate_fail_status(%s=%i) for minion %i, devid %3x @ %s:%i\n___________________________________________________________", #fail, fail, minion->mID, minion->devid, __FILE__, __LINE__); \
      minion->S.fault.data = fail; \
   }
#if 1 /* attempt 3 -- use two blocks: one for has-only-one-status, one for has-new-and-old */
   // quick-lookup-variables used in many locations
   int killed = minion->S.resp.data < 0 ? 1 : 0; // target is currently "killed"
   int faking_left, faking_right, faking_stopped, faking_up, faking_down; // what we've told the FASIT server
   int looking_fast; // either 1 for looking fast, or 0 for looking slow
   if (minion->S.rf_t.fast_flags == F_fast_none) {
      looking_fast = 0;
   } else {
      looking_fast = 1;
   }
   switch (minion->S.exp.last_step) {
      default:
      case TS_exp_to_con:
      case TS_concealed:
         faking_down = 1;
         faking_up = 0;
         break;
      case TS_con_to_exp:
      case TS_exposed:
         faking_down = 0;
         faking_up = 1;
         break;
   }
   if (minion->S.speed.data > 0.0) {
      faking_stopped = 0;
      if (minion->S.move.data == 1) {
         faking_right = 1;
         faking_left = 0;
      } else {
         faking_right = 0;
         faking_left = 1;
      }
   } else {
      faking_stopped = 1;
      faking_left = 0;
      faking_right = 0;
   }


   if (!minion->S.resp.newdata && badFault(minion->S.fault.data) != 1) {
      // no actual status or new error to report, we're done here
      DDCMSG(D_POINTER, MAGENTA, "-----------------------------\nDev %3x skipping status response", minion->devid);
      return;
   }
   if (minion->S.ignoring_response && badFault(minion->S.fault.data) != 1) {
      // no actual status or new error to report, we're done here
      DDCMSG(D_POINTER, MAGENTA, "-----------------------------\nDev %3x ignoring status response", minion->devid);
      return;
   }

   if (badFault(minion->S.fault.data) != 1) {
      // target missed command to expose?
      if (minion->S.resp.lifter_command == lift_expose &&
          minion->S.resp.current_exp == 0) {
         DDCMSG(D_POINTER, MAGENTA, "-----------------------------\nDev %3x concealed unexpectedly", minion->devid);
         // never went up, show correctly as down in FASIT server
         fake_transition_down();
      }
      // target missed command to conceal?
      if (minion->S.resp.lifter_command == lift_conceal &&
          minion->S.resp.current_exp == 90) {
         DDCMSG(D_POINTER, MAGENTA, "-----------------------------\nDev %3x exposed unexpectedly", minion->devid);
         // never went down, show correctly as up in FASIT server
         fake_transition_up();
      }
   }

   /************************************************************************************/
   /* check target states that need only one RF response, but aren't okay with more    */
   /************************************************************************************/
   if (!minion->S.resp.lastdata) {
   }

   /************************************************************************************/
   /* check target states that need at least two RF responses                     */
   /************************************************************************************/
   else {
      DDCMSG(D_POINTER, MAGENTA, "-----------------------------\nDev %3x another status response", minion->devid);
   }

   // send status, if it's updated, to FASIT server
   sendStatus2102(0, NULL,minion,mt); // tell FASIT server
#endif /* end of attempt 3 */

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
   timestamp(mt);
   //DDCMSG(D_POINTER|D_NEW, YELLOW, "sendStatus2102 (%i) called @ %3i.%03i", minion->S.exp.data, DEBUG_TS(mt->elapsed_time));

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
         DDCMSG(D_NEW, black, "Changed data for " #S " %f := %f", (float)S.lastdata, (float)S.data); \
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
         DDCMSG(D_PACKET, GRAY, "Might send 2102 expose: msg.body.exp=%i @ %i", msg.body.exp, __LINE__);
         msg.body.speed = htonf(0.0);
         //DCMSG(GRAY, "Lifter Dev %3x sending speed %f from %f", minion->devid, msg.body.speed, minion->S.speed.data);
         //DCMSG(GRAY, "Lifter Dev %3x sending speed %08x from %08x", minion->devid, *((int*)((char*)&msg.body.speed)), *((int*)((char*)&minion->S.speed.data)));
         break;
      case Type_MIT:
      case Type_MAT:
         CACHE_CHECK(minion->S.speed);
         msg.body.speed = htonf(minion->S.speed.data);
         //DCMSG(GRAY, "Mover Dev %3x sending speed %f from %f", minion->devid, msg.body.speed, minion->S.speed.data);
         //DCMSG(GRAY, "Mover Dev %3x sending speed %08x from %08x", minion->devid, *((int*)((char*)&msg.body.speed)), *((int*)((char*)&minion->S.speed.data)));
         CACHE_CHECK(minion->S.exp);
         msg.body.exp = minion->S.exp.data;
         DDCMSG(D_PACKET, GRAY, "Might send 2102 expose: msg.body.exp=%i @ %i", msg.body.exp, __LINE__);
         CACHE_CHECK(minion->S.move);
         msg.body.move = minion->S.move.data;
         CACHE_CHECK(minion->S.position);
         msg.body.pos = htons(minion->S.position.data);
//         DDCMSG(D_NEW,MAGENTA,"\n_____________________________________________________\nMinion %i: building 2102 status.   S.position.data=%i msg.body.pos=%i  htons(...)=%i ",
//                minion->mID,minion->S.position.data,msg.body.pos,htons(msg.body.pos));
         break;
   }

   // left to do:
   msg.body.pstatus = 0; // always "shore" power
   // CACHE_CHECK(minion->S.fault); -- fault.lastdata has a different meaning, don't use CACHE_CHECK on it
   if (minion->S.fault.data != ERR_normal) {
      //DDCMSG(D_POINTER, GRAY, "Minion %i forced sending due to fault %i", minion->mID, minion->S.fault.data);
      force=1; // all errors/statuses that get here should be sent
   }
   msg.body.fault = htons(minion->S.fault.data);
   if (badFault(minion->S.fault.data) != -2) {
      // it is a one-off message...reset
      minion->S.fault.data = ERR_normal;
   }

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
      DDCMSG(D_PACKET, GRAY, "Sending 2102 expose: msg.body.exp=%i @ %i", msg.body.exp, __LINE__);
      DDCMSG(D_PACKET,BLUE,"Minion %i: sending a 2102 status response - %i byte header %i byte body",minion->mID,sizeof(FASIT_header),sizeof(FASIT_2102));
      DDCMSG(D_PARSE,BLUE,"M-Num | ICD-v | seq-# | rsrvd | length  R-num  R-seq          <--- Header\n %6i  %i.%i  %6i  %6i %7i %6i %7i "
             ,htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htonl(rhdr.seq),htonl(rhdr.rsrvd),htons(rhdr.length),htons(msg.response.resp_num),htonl(msg.response.resp_seq));
      DDCMSG(D_PARSE,BLUE,\
             "PSTAT | Fault | Expos | Aspct |  Dir | Move |  Speed  | POS | Type | Hits | On/Off | React | ToKill | Sens | Mode | Burst\n"\
             "       %3i    %3i     %3i     %3i     %3i    %3i    %6.2f    %3i   %3i    %3i      %3i     %3i      %3i     %3i    %3i    %3i ",
             msg.body.pstatus,msg.body.fault,msg.body.exp,msg.body.asp,msg.body.dir,msg.body.move,htonf(msg.body.speed),htons(msg.body.pos),msg.body.type,htons(msg.body.hit),
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

// create and send a status messsage to the FASIT server
void sendStatus2115(FASIT_header *hdr,thread_data_t *minion, FASIT_2114* msg) {
   struct iovec iov[2];
   FASIT_header rhdr;
   FASIT_2115 rmsg;
   int result;

   // sets the sequence number and other data    
   defHeader(&rhdr,2115,minion->seq++, sizeof(FASIT_2115));

   // fill message
   // start with zeroes
   memset(&rmsg, 0, sizeof(FASIT_2115));

   // fill out as response
   rmsg.response.resp_num = hdr->num; //  pulls the message number from the header that we are responding to
   rmsg.response.resp_seq = hdr->seq;
   rmsg.body.code = msg->code;
   rmsg.body.ammo = msg->ammo;
   rmsg.body.player = msg->player;
   rmsg.body.delay = msg->delay;

   // -- debug output would go here if I cared --

   write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&rmsg,sizeof(FASIT_2115));
}// create and send a status messsage to the FASIT server

void sendStatus13112(FASIT_header *hdr,thread_data_t *minion, FASIT_13110* msg) {
   struct iovec iov[2];
   FASIT_header rhdr;
   FASIT_13112 rmsg;
   int result;

   // sets the sequence number and other data    
   defHeader(&rhdr,13112,minion->seq++, sizeof(FASIT_13112));

   // fill message
   // start with zeroes
   memset(&rmsg, 0, sizeof(FASIT_13112));

   // fill out as response
   rmsg.response.resp_num = hdr->num; //  pulls the message number from the header that we are responding to
   rmsg.response.resp_seq = hdr->seq;
   rmsg.body.on = msg->on;

   // -- debug output would go here if I cared --

   write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&rmsg,sizeof(FASIT_13112));
}// create and send a status messsage to the FASIT server

void sendStatus14112(FASIT_header *hdr,thread_data_t *minion, FASIT_14110* msg) {
   struct iovec iov[2];
   FASIT_header rhdr;
   FASIT_14112 rmsg;
   int result;

   // sets the sequence number and other data    
   defHeader(&rhdr,14112,minion->seq++, sizeof(FASIT_14112));

   // fill message
   // start with zeroes
   memset(&rmsg, 0, sizeof(FASIT_14112));

   // fill out as response
   rmsg.response.resp_num = hdr->num; //  pulls the message number from the header that we are responding to
   rmsg.response.resp_seq = hdr->seq;
   rmsg.body.on = msg->on;

   // -- debug output would go here if I cared --

   write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&rmsg,sizeof(FASIT_14112));
}// create and send a status messsage to the FASIT server

void sendStatus15112(FASIT_header *hdr,thread_data_t *minion, FASIT_15110* msg) {
   struct iovec iov[2];
   FASIT_header rhdr;
   FASIT_15112 rmsg;
   int result;

   // sets the sequence number and other data    
   defHeader(&rhdr,15112,minion->seq++, sizeof(FASIT_15112));

   // fill message
   // start with zeroes
   memset(&rmsg, 0, sizeof(FASIT_15112));

   // fill out as response
   rmsg.response.resp_num = hdr->num; //  pulls the message number from the header that we are responding to
   rmsg.response.resp_seq = hdr->seq;
   rmsg.body.on = msg->on;

   // -- debug output would go here if I cared --

   write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&rmsg,sizeof(FASIT_15112));
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
	//DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd |
	//length\n%6i  %i.%i  %6i  %6i  %7i",htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htonl(rhdr.seq),htons(rhdr.rsrvd),htons(rhdr.length));
	//DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nR-NUM | R-Seq | Response\n%5i  %6i  '%c'",
	//htons(rmsg.response.resp_num),htons(rmsg.response.resp_seq),rmsg.body.resp);

	write_FASIT_msg(minion,&rhdr,sizeof(FASIT_header),&rmsg,sizeof(FASIT_2101));
	return 0;
}

// now send it to the MCP master
// JJ: Understood to be RF command outflow from minion
int psend_mcp(thread_data_t *minion,void *Lc){
	int result;
	char hbuf[100];
	LB_packet_t *L=(LB_packet_t *)Lc;    // so we can call it with different types and not puke.

	// calculates the correct CRC and adds it to the end of the packet payload
	set_crc8(L);

	// write all at once
	result=write(minion->mcp_sock,L,RF_size(L->cmd));

	// sent LB
	if (verbose&D_RF){       // don't do the sprintf if we don't need to
		sprintf(hbuf,"Minion %i: psend LB packet to MCP sock=%i   RF address=%4i cmd=%2i msglen=%i result=%i",minion->mID,minion->mcp_sock,minion->RF_addr,L->cmd,RF_size(L->cmd),result);
		DDCMSG2_HEXB(D_RF,YELLOW,hbuf,L,RF_size(L->cmd));
		DDpacket(Lc,RF_size(L->cmd));
	}
	return result;
}

// now send it to the MCP master and keep track of this message
// JJ: Understood to be RF command outflow from minion
int psend_mcp_seq(thread_data_t *minion,void *Lc){
	int result;
	char hbuf[100];
	LB_packet_t *L=(LB_packet_t *)Lc;    // so we can call it with different types and not puke.

	// create a new control message for this packet
	LB_control_queue_t lcq;
	lcq.cmd = LB_CONTROL_QUEUE; // we want this message to be queued
	lcq.addr = L->addr;
	lcq.sequence = ++minion->S.last_sequence_sent;
	set_crc8(&lcq);

	// calculates the correct CRC and adds it to the end of the packet payload
	set_crc8(L);

	// write all at once
	memcpy(hbuf, &lcq, RF_size(lcq.cmd));
	memcpy(hbuf+RF_size(lcq.cmd), L, RF_size(L->cmd));
	result=write(minion->mcp_sock,hbuf,RF_size(lcq.cmd) + RF_size(L->cmd));

	//  sent LB
	if (verbose&D_RF){       // don't do the sprintf if we don't need to
		sprintf(hbuf,"Minion %i: psend_seq LB packet to MCP sock=%i   RF address=%4i cmd=%2i seq=%i msglen=%i result=%i",minion->mID,minion->mcp_sock,minion->RF_addr,L->cmd,lcq.sequence,RF_size(L->cmd),result);
		DDCMSG2_HEXB(D_RF,YELLOW,hbuf,L,RF_size(L->cmd));
		DDpacket(Lc,RF_size(L->cmd));
	}
	return result;
}

/* inform FASIT server of current state, in pieces if necessary
 */
void inform_state(thread_data_t *minion, minion_time_t *mt, FASIT_header *hdr) {
   // find out what state we should be presenting to FASIT server
   trans_step_t should_be;
   switch (minion->S.exp.data) {
      case 0:
         should_be = TS_concealed;
         break;
      case 45:
         if (minion->S.exp.newdata == 90) {
            should_be = TS_con_to_exp;
         } else {
            should_be = TS_exp_to_con;
         }
         break;
      case 90:
         should_be = TS_exposed;
         break;
   }

   // re-send previous state sent...
   if (minion->S.exp.last_step != TS_too_far) {
      minion->S.exp.lastdata = -1; // force sending of status by messing with cache
      minion->S.exp.data = steps_translation[minion->S.exp.last_step]; // convert to 0/45/90 values
      sendStatus2102(1, hdr, minion, mt); // responsd to FASIT server
   
      // ... then send more if necessary
      //DDCMSG(D_POINTER|D_NEW, YELLOW, "calling change_states(%s) @ %s:%i", step_words[should_be], __FILE__, __LINE__);
      change_states(minion, mt, should_be, 0); // minion->S.exp.data will return to what it was at the beginning
   } else {
      minion->S.exp.lastdata = -1; // force sending of status by messing with cache
      minion->S.exp.data = steps_translation[should_be]; // convert "should_be" to 0/45/90 values
      sendStatus2102(1, hdr, minion, mt); // responsd to FASIT server
      minion->S.exp.last_step = should_be;
   }
}

/* move FASIT server to desired step
 *
 * "step" is the final step to do
 * "force" allows changing from step all the way to doing the same step again
 *   if force is 0, and the "step" argument is the "last_step" value, nothing will happen
 *
 * will update "logged" timers along the way
 * will move the current event if necessary
 * will send LB messages to update targets' timers if necessary
 */
void change_states_internal(thread_data_t *minion, minion_time_t *mt, trans_step_t step, int force) {
   int needsleep = 0;
   int force_send = 0;

   // standard way to move from one step to the next
   #define NEXT_STEP(S) { if (++S >= TS_too_far) { S = TS_concealed; } }

   timestamp(mt);
   //DDCMSG(D_POINTER|D_NEW, YELLOW, "change_states (%s, %i) called @ %3i.%03i", step_words[step], force, DEBUG_TS(mt->elapsed_time));

   // check if we will need to sleep somewhere
   // -- we need to sleep if the complete transition that happens here causes multiple log times
   switch (minion->S.exp.last_step) {
      case TS_concealed:
         switch (step) {
            case TS_concealed:
               if (force) {
                  needsleep = 1;
               }
               break;
         }
         break;
      case TS_con_to_exp:
         switch (step) {
            case TS_concealed:
               needsleep = 1;
               break;
            case TS_con_to_exp:
               if (force) {
                  needsleep = 1;
               }
               break;
         }
         break;
      case TS_exposed:
         switch (step) {
            case TS_exposed:
               if (force) {
                  needsleep = 1;
               }
               break;
         }
         break;
      case TS_exp_to_con:
         switch (step) {
            case TS_exposed:
               needsleep = 1;
               break;
            case TS_exp_to_con:
               if (force) {
                  needsleep = 1;
               }
               break;
         }
         break;
   }
   //DDCMSG(D_POINTER|D_NEW, YELLOW, "Here...needsleep=%i derived from last_step=%s", needsleep, step_words[minion->S.exp.last_step]);

   // check to see if we should force sending or let sendStatus2102() figure it out
   switch (minion->S.exp.data) {
      case 0:
         if (minion->S.exp.last_step != TS_concealed) {
            DDCMSG(D_POINTER|D_NEW, YELLOW, "Force sending here @ %i derived from exp.data=%i & last_step=%s", __LINE__, minion->S.exp.data, step_words[minion->S.exp.last_step]);
            force_send = 1;
         }
         break;
      case 45:
         if (minion->S.exp.newdata == 90) {
            if (minion->S.exp.last_step != TS_con_to_exp) {
               DDCMSG(D_POINTER|D_NEW, YELLOW, "Force sending here @ %i derived from exp.data=%i & last_step=%s", __LINE__, minion->S.exp.data, step_words[minion->S.exp.last_step]);
               force_send = 1;
            }
         } else {
            if (minion->S.exp.last_step != TS_exp_to_con) {
               DDCMSG(D_POINTER|D_NEW, YELLOW, "Force sending here @ %i derived from exp.data=%i & last_step=%s", __LINE__, minion->S.exp.data, step_words[minion->S.exp.last_step]);
               force_send = 1;
            }
         }
         break;
      case 90:
         if (minion->S.exp.last_step != TS_exposed) {
            DDCMSG(D_POINTER|D_NEW, YELLOW, "Force sending here @ %i derived from exp.data=%i & last_step=%s", __LINE__, minion->S.exp.data, step_words[minion->S.exp.last_step]);
            force_send = 1;
         }
         break;
   }

   // loop until we've arrive at the correct step
   while (minion->S.exp.last_step != step || force) {
      // remove the force after one use
      if (minion->S.exp.last_step == step && force) {
         force = 0;
         DDCMSG(D_POINTER|D_NEW, YELLOW, "Force sending here @ %i derived from exp.last_step=%s & step=%s & force=%i", __LINE__, step_words[minion->S.exp.last_step], step_words[step], force);
         force_send = 1; // force hand next send
      }

      // change steps
      NEXT_STEP(minion->S.exp.last_step);

      //DDCMSG(D_POINTER|D_NEW, YELLOW, "Here...this step=%s", step_words[minion->S.exp.last_step]);

      // tell FASIT server our plight
      if (force_send) {
         minion->S.exp.lastdata = -1; // force sending of status by messing with cache
         force_send = 0; // we've used up the force here
      }
      minion->S.exp.data = steps_translation[minion->S.exp.last_step]; // convert to 0/45/90 values
      sendStatus2102(0, NULL,minion,mt); // tell FASIT server

      switch (minion->S.exp.last_step) {
         case TS_concealed:
            // log time to event
            timestamp(mt);
            minion->S.exp.log_end_time[minion->S.exp.event] = ts2ms(&mt->elapsed_time); // set log end time
            //DDCMSG(D_POINTER|D_NEW, YELLOW, "Here...logged end time=%i", ts2ms(&mt->elapsed_time));
            break;
         case TS_con_to_exp:
            // no logging happens here, so move on
            break;
         case TS_exposed:
            // log time to event
            timestamp(mt);
            minion->S.exp.log_start_time[minion->S.exp.event] = ts2ms(&mt->elapsed_time); // set log start time
            //DDCMSG(D_POINTER|D_NEW, YELLOW, "Here...logged start time=%i", ts2ms(&mt->elapsed_time));

            // do we need to sleep?
            if (needsleep) {
               usleep(350000); // 350 milliseconds
               timestamp(mt);
               //DDCMSG(D_POINTER|D_NEW, YELLOW, "Here...sleep returned @ %3i.%03i", DEBUG_TS(mt->elapsed_time));
            }
            break;
         case TS_exp_to_con:
            // no logging happens here, so move on
            break;
         default:
            // broken, possibly via negative numbers...fix here
            NEXT_STEP(minion->S.exp.last_step);
            break;
      }
   }
   //DDCMSG(D_POINTER|D_NEW, YELLOW, "Done...last step=%s", step_words[minion->S.exp.last_step]);
}

void ExposeSentCallback(thread_data_t *minion, minion_time_t *mt, char *msg, void *data) {
   //DDCMSG(D_POINTER, YELLOW, "Hey hey! We've sent the expose command now");
   //DDpacket(msg, sizeof(LB_expose_t));
   START_EXPOSE_TIMER(minion->S); // pretend to start exposing
   // new timer stuff
   minion->S.ignoring_response = 2; // not ignoring responses after next request is sent
   DO_FAST_LOOKUP(minion->S);
   Cancel_Status_Resp_Timer(minion);
}

void ExposeRemovedCallback(thread_data_t *minion, minion_time_t *mt, char *msg, void *data) {
   //DDCMSG(D_POINTER, YELLOW, "Hey hey! We've removed the expose command now");
   //DDpacket(msg, sizeof(LB_expose_t));
   // new timer stuff
   minion->S.ignoring_response = 2; // not ignoring responses after next request is sent
   if (minion->S.resp.current_speed == 0 && minion->S.resp.current_exp == 0) {
      // not moving and not exposed
      DO_SLOW_LOOKUP(minion->S);
   } else {
      // moving and/or exposed
      DO_FAST_LOOKUP(minion->S);
   }
   Cancel_Status_Resp_Timer(minion);
}

void QConcealSentCallback(thread_data_t *minion, minion_time_t *mt, char *msg, void *data) {
   //DDCMSG(D_POINTER, YELLOW, "Hey hey! We've sent the qconceal command now");
   //DDpacket(msg, sizeof(LB_qconceal_t));
   START_CONCEAL_TIMER(minion->S); // pretend to start concealing
   // new timer stuff
   minion->S.ignoring_response = 2; // not ignoring responses after next request is sent
   if (minion->S.resp.current_speed == 0 && minion->S.resp.current_exp == 0) {
      // not moving and not exposed
      DO_SLOW_LOOKUP(minion->S);
   } else {
      // moving and/or exposed
      DO_FAST_LOOKUP(minion->S);
   }
   Cancel_Status_Resp_Timer(minion);
}

void QConcealRemovedCallback(thread_data_t *minion, minion_time_t *mt, char *msg, void *data) {
   //DDCMSG(D_POINTER, YELLOW, "Hey hey! We've removed the qconceal command now");
   //DDpacket(msg, sizeof(LB_qconceal_t));
   // new timer stuff
   minion->S.ignoring_response = 2; // not ignoring responses after next request is sent
   if (minion->S.resp.current_speed == 0 && minion->S.resp.current_exp == 0) {
      // not moving and not exposed
      DO_SLOW_LOOKUP(minion->S);
   } else {
      // moving and/or exposed
      DO_FAST_LOOKUP(minion->S);
   }
   Cancel_Status_Resp_Timer(minion);
}

void MoveSentCallback(thread_data_t *minion, minion_time_t *mt, char *msg, void *data) {
   //DDCMSG(D_POINTER, YELLOW, "Hey hey! We've sent the move command now");
   //DDpacket(msg, sizeof(LB_move_t));
   START_MOVE_TIMER(minion->S); // pretend to start moving
   // new timer stuff
   minion->S.ignoring_response = 2; // not ignoring responses after next request is sent
   if (minion->S.resp.mover_command == move_stop) {
      // should be stopping now then
      DO_SLOW_LOOKUP(minion->S);
   } else {
      // should be moving now then
      DO_FAST_LOOKUP(minion->S);
   }
   Cancel_Status_Resp_Timer(minion);
}

void MoveRemovedCallback(thread_data_t *minion, minion_time_t *mt, char *msg, void *data) {
   //DDCMSG(D_POINTER, YELLOW, "Hey hey! We've removed the move command now");
   //DDpacket(msg, sizeof(LB_move_t));
   // new timer stuff
   minion->S.ignoring_response = 2; // not ignoring responses after next request is sent
   if (minion->S.resp.current_speed == 0 && minion->S.resp.current_exp == 0) {
      // not moving and not exposed
      DO_SLOW_LOOKUP(minion->S);
   } else {
      // moving and/or exposed
      DO_FAST_LOOKUP(minion->S);
   }
   Cancel_Status_Resp_Timer(minion);
}

void StatusReqSentCallback(thread_data_t *minion, minion_time_t *mt, char *msg, void *data) {
   // new timer stuff
   if (minion->S.ignoring_response == 2) {
      // not ignoring responses now that next request is sent
      minion->S.ignoring_response = 0;
   }
}

void StatusReqRemovedCallback(thread_data_t *minion, minion_time_t *mt, char *msg, void *data) {
   // new timer stuff
   if (minion->S.ignoring_response == 2) {
      // not ignoring responses now
      minion->S.ignoring_response = 0;
   }
}

void ClearTracker(thread_data_t *minion, sequence_tracker_t *tracker) {
   // remove from list
   sequence_tracker_t *temp = minion->S.tracker;
   sequence_tracker_t *prev = NULL;
   while (tracker != temp && temp != NULL) {
      prev = temp;
      temp = temp->next;
   }
   if (temp != NULL) {
      // we've been found
      if (prev != NULL) {
         // ...and we're in the middle of the chain or the tail
         prev->next = temp->next; 
      } else {
         // ...and we're the head of the chain
         minion->S.tracker = temp->next;
      }
   } else if (tracker == minion->S.tracker) {
      // something was broken...
      if (prev != NULL) {
         // ...and we were the head
         prev->next = tracker->next; 
      } else {
         // ...and we're the head of the chain
         minion->S.tracker = tracker->next;
      }
   }
   
   // free memory
   if (tracker->pkt != NULL) {
      free(tracker->pkt);
   }
   if (tracker->data != NULL) {
      free(tracker->data);
   }
}

//JJ: This function sends an expose command?
void send_LB_exp(thread_data_t *minion, int expose, minion_time_t *mt) {
	char qbuf[32];      // more packet buffers
	LB_expose_t *LB_exp;

	timestamp(mt);
	//DDCMSG(D_POINTER|D_NEW, black, "send_LB_exp (%i) called @ %3i.%03i\n____________________________________", expose, DEBUG_TS(mt->elapsed_time));

	LB_exp = (LB_expose_t *)qbuf;// make a pointer to our buffer so we can use the bits right
	LB_exp->cmd = LBC_EXPOSE;
	// testing quick group code
	LB_exp->addr=minion->RF_addr;
	// LB_exp->addr = qg.temp_addr;
	// end testing quick group code
	LB_exp->expose=expose; // not expose vs. conceal, but a flag saying whether to actually expose or just change event number internally

	//JJ: change state of minion
	minion->S.exp.newdata=90; // moving to exposed
	minion->S.exp.recv_dec = 0;
	if (expose) {
		LB_exp->event=++minion->S.exp.event;  // fill in the event/move to next event
		// reset our times (we'll set our start time when we receive our first status response)
		minion->S.exp.cmd_start_time[minion->S.exp.event] = ts2ms(&mt->elapsed_time); // set command start time
		minion->S.exp.log_start_time[minion->S.exp.event] = 0; // reset log start time
		minion->S.exp.cmd_end_time[minion->S.exp.event] = 0; // reset cmd end time
		minion->S.exp.log_end_time[minion->S.exp.event] = 0; // reset log end time
		// set our response cache kill and expose data as fresh
		minion->S.resp.data = min(minion->S.tokill.newdata, 1) - 1; // minus one so when we're killed it tests differently than the initial state of zero (killed == -1 or less)
		minion->S.resp.last_exp = -1; // won't match as "same" next time
	} else {
		LB_exp->event=minion->S.exp.event;  // just fill in the event
		// change our event end to match the event times on the target (delay notwithstanding)
		//DDCMSG(D_POINTER, GREEN, "Expose (0) changing end to %3i.%03i from %3i.%03i)", DEBUG_MS(minion->S.exp.cmd_end_time[minion->S.exp.event]), DEBUG_TS(mt->elapsed_time));
		minion->S.exp.cmd_end_time[minion->S.exp.event] = ts2ms(&mt->elapsed_time);
	}

	//DDCMSG(D_POINTER|D_NEW, YELLOW, "Changing exp.data = 45 @ %i", __LINE__);
	minion->S.exp.data=45;  // make the current positon in movement
	DDCMSG(D_NEW, GRAY, "Sending LB expose: S.exp.newdata=%i, S.exp.data=%i @ %i", minion->S.exp.newdata, minion->S.exp.data, __LINE__);
	//DDCMSG(D_POINTER|D_NEW, MAGENTA, "starting exp timer @ %s:%i", __FILE__, __LINE__);
	//START_EXPOSE_TIMER(minion->S); // new state timer code

	// really need to fill in with the right stuff
	LB_exp->hitmode = (minion->S.mode.newdata == 2 ? 1 : 0); // burst / single
	LB_exp->tokill =  minion->S.tokill.newdata;
	LB_exp->react = minion->S.react.newdata;
	if (minion->S.mfs_on.newdata) {
		if (minion->S.mfs_mode.newdata == 1) {
			LB_exp->mfs = 2; // burst
		} else {
			LB_exp->mfs = 1; // sigle
		}
	} else {
		LB_exp->mfs = 0; // off
	}

	// TODO -- LB_exp->mfs = 3 // random burst/single ?
	LB_exp->thermal=0; // TODO -- thermals (what about thermal delay?)
	DDCMSG(D_NEW,BLUE,"Minion %i:  LB_exp cmd=%i", minion->mID,LB_exp->cmd);
	if (1 || expose) { // always track
		// keep track of command if it's doing something
		sequence_tracker_t *t;
		psend_mcp_seq(minion,LB_exp);

		// fill in tracker info
		t = malloc(sizeof(sequence_tracker_t));
		t->sequence = minion->S.last_sequence_sent;
		LB_exp = malloc(sizeof(LB_expose_t)); // turn link into allocated data
		memcpy(LB_exp, qbuf, sizeof(LB_expose_t));
		t->pkt = (char*)LB_exp;
		t->data = NULL;
		t->sent_callback = ExposeSentCallback;
		t->removed_callback = ExposeRemovedCallback;
		t->missed = 50; // after we don't get a callback for this message X times, but we do for other messages, give up

		// place at head of chain
		t->next = minion->S.tracker;
		minion->S.tracker = t;
	}
	else {
		//JJ: Unreachable code
		// don't keep track if it's not
		psend_mcp(minion,LB_exp);
	}
}

/* Process the FASIT message */
int handle_FASIT_msg(thread_data_t *minion,char *buf, int packetlen, minion_time_t *mt){

	int result,psize;
	char hbuf[100];

	FASIT_header *header;
	FASIT_header rhdr;
	FASIT_2111    msg;
	FASIT_2100    *message_2100;
	FASIT_2110    *message_2110;
	FASIT_2114    *message_2114;
	FASIT_13110   *message_13110;
	FASIT_14110   *message_14110;
	FASIT_15110   *message_15110;
	FASIT_14200   *message_14200;

	char qbuf[32],rbuf[32];      // more packet buffers
   
	LB_packet_t          LB_buf;
	LB_device_reg_t      *LB_devreg;
	LB_status_req_t      *LB_status_req;
	//LB_expose_t          *LB_exp;
	LB_move_t            *LB_move;
	LB_configure_t       *LB_configure;
	LB_power_control_t   *LB_power;
	LB_audio_control_t   *LB_audio;
	LB_pyro_fire_t       *LB_pyro;
	LB_qconceal_t        *LB_qcon;

	// new state timer code
	// there is no new timer code on receipt of FASIT message, only on receipt of RF message

	// map header and body for both message and response
	header = (FASIT_header*)(buf);
	DDCMSG(D_PACKET,BLUE,"Minion %i: Handle_FASIT_msg recieved fasit packet num=%i seq=%i length=%i packetlen=%i",minion->mID,htons(header->num),htonl(header->seq),htons(header->length),packetlen);

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
			DDCMSG(D_PARSE,CYAN,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6i  %i.%i  %6i  %6i  %7i",htons(header->num),htons(header->icd1),htons(header->icd2),htonl(header->seq),htons(header->rsrvd),htons(header->length));

			DDCMSG(D_PARSE,CYAN,"\t\t\t\t\t\t\tmessage body\n"\
				"C-ID | Expos | Aspct |  Dir | Move |  Speed | On/Off | Hits | React | ToKill | Sens | Mode | Burst\n"\
				"%3i    %3i     %3i     %2i    %3i    %7.2f     %4i     %2i     %3i     %3i     %3i    %3i   %5i ",message_2100->cid,message_2100->exp,message_2100->asp,message_2100->dir,message_2100->move,message_2100->speed,message_2100->on,htons(message_2100->hit),message_2100->react,htons(message_2100->tokill),htons(message_2100->sens),message_2100->mode,htons(message_2100->burst));
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
					DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Status_Request   send 2102 status  hdr->num=%i, hdr->seq=%i",minion->mID,htons(header->num),htonl(header->seq));
					inform_state(minion, mt, header); // forces sending of a 2102
					if (minion->S.cap&PD_NES){
						DDCMSG(D_PACKET,BLUE,"Minion %i: we also seem to have a MFS Muzzle Flash Simulator - send 2112 status",minion->mID) ; 
						// AND send 2112 Muzzle Flash status if supported  
						sendStatus2112(0,header,minion); // send of a 2112 (0 means unsolicited, 1 copys mnum and seq to resp_mnum and resp_seq)
					}

				break;
				case CID_Expose_Request:
					DDCMSG(D_PACKET,BLACK,"Minion %i: CID_Expose_Request  send 'S'uccess ack.   message_2100->exp=%i",minion->mID,message_2100->exp);
					//also build an LB packet to send
					if (message_2100->exp==90){
						DDCMSG(D_PACKET,BLUE,"Minion %i:  going up (exp=%i)",minion->mID,message_2100->exp);
						DDCMSG(D_PACKET,BLACK,"Minion %i: we seem to work here******************* &LB_buf=%p",minion->mID,&LB_buf);
						// new timer stuff
						minion->S.resp.lifter_command = lift_expose;
						minion->S.ignoring_response = 1; // ignoring responses due to exposure
						STOP_SLOW_LOOKUP(minion->S);
						Cancel_Status_Resp_Timer(minion);

						// psend is inside here:
						send_LB_exp(minion, 1, mt); // do an expose, don't just change states and send "event" message
					}
					else {
						int uptime;
						DDCMSG(D_PACKET,BLACK,"Minion %i: going down(exp=%i)",minion->mID,message_2100->exp);

						// we must assume that this 'conceal' followed the last expose.
						// so now the event is over and we need to send the qconceal command
						// and the termination of the event time and stuff
						// calculate uptime based on the time between commands
						// TODO -- fix uptime based on something smarter than "difference from now" ???
						uptime=(mt->elapsed_time.tv_sec - (minion->S.exp.cmd_start_time[minion->S.exp.event]%1000))*10; //  find uptime in tenths of a second
						uptime+=(mt->elapsed_time.tv_nsec - (minion->S.exp.cmd_start_time[minion->S.exp.event]*1000000l))/100000000L;      //  find uptime in tenths of a second

						if (uptime>0x7FF){
							uptime=0x7ff; // set time to max (204.7 seconds) if it was too long
						}
						else {
							uptime&= 0x7ff;
						}

						// furthermore, we also assume that this command will be received on
						// the downstream end and responded to from that end in the same
						// relative timeframe as the original expose message. if there
						// are significant delays at either end of the spectrum, we can
						// only assume that the unqualified hits we receive will be logged
						// correctly when we use them
						// log the information on when we sent the conceal message to the target
						//DDCMSG(D_POINTER, GREEN, "CID_expose_request changing end to %3i.%03i from %3i.%03i)", DEBUG_MS(minion->S.exp.cmd_end_time[minion->S.exp.event]), DEBUG_TS(mt->elapsed_time));
						minion->S.exp.cmd_end_time[minion->S.exp.event] = ts2ms(&mt->elapsed_time);

						DDCMSG(D_VERY,BLACK,"Minion %i: uptime=%i",minion->mID,uptime);

						minion->S.exp.newdata=0; // moving to concealed
						minion->S.exp.recv_dec = 0;

						DDCMSG(D_VERY,BLACK,"Minion %i: &LB_buf=%p",minion->mID,&LB_buf);                     
						LB_qcon =(LB_qconceal_t *)qbuf; // make a pointer to our buffer so we can use the bits right
						DDCMSG(D_VERY,BLACK,"Minion %i: map packet in",minion->mID);

						LB_qcon->cmd=LBC_QCONCEAL;
						LB_qcon->addr=minion->RF_addr;
						LB_qcon->event=minion->S.exp.event;
						LB_qcon->uptime=uptime;
 
						// new timer stuff
						minion->S.resp.lifter_command = lift_conceal;
						if (minion->S.resp.current_speed == 0) { // not moving
							minion->S.ignoring_response = 1; // ignoring responses due to exposure
							STOP_FAST_LOOKUP(minion->S);
							Cancel_Status_Resp_Timer(minion);
						}
                 
						DDCMSG(D_PACKET,BLUE,"Minion %i:  LB_qcon cmd=%i", minion->mID,LB_qcon->cmd);
//{
						// keep track of command if it's doing something
						sequence_tracker_t *t;
						psend_mcp_seq(minion,LB_qcon);

						// fill in tracker info
						t = malloc(sizeof(sequence_tracker_t));
						t->sequence = minion->S.last_sequence_sent;
						LB_qcon = malloc(sizeof(LB_qconceal_t)); // turn link into allocated data
						memcpy(LB_qcon, qbuf, sizeof(LB_qconceal_t));
						t->pkt = (char*)LB_qcon;
						t->data = NULL;
						t->sent_callback = QConcealSentCallback;
						t->removed_callback = QConcealRemovedCallback;
						t->missed = 50; // after we don't get a callback for this message X times, but we do for other messages, give up

						// place at head of chain
						t->next = minion->S.tracker;
						minion->S.tracker = t;
//}

						// new state timer code
						//DDCMSG(D_POINTER|D_NEW, YELLOW, "Changing exp.data = 45 @ %i", __LINE__);
						minion->S.exp.data = 45;  // make the current positon in movement
						DDCMSG(D_NEW, GRAY, "Send 2100 expose: S.exp.newdata=%i, S.exp.data=%i @ %i", minion->S.exp.newdata, minion->S.exp.data, __LINE__);
               				}

					// send 2101 ack  (2102's will be generated at start and stop of actuator)
					send_2101_ACK(header,'S',minion);    // TRACR Cert complains if these are not there

					// it should happen in this many deciseconds
					//  - fasit device cert seems to come back immeadiately and ask for status again, and
					//    that is causing a bit of trouble.
				break;
				case CID_Reset_Device:
{//----?
					LB_reset_t LBr;
					// send 2101 ack
					DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Reset_Device  send 'S'uccess ack.   set lastHitCal.* to defaults",minion->mID);
					send_2101_ACK(header,'S',minion);
					// this opens a serious can of worms, should probably handle via minion telling RFmaster to set the forget bit and resetting its own state
					LBr.cmd = LBC_RESET;
					LBr.addr=minion->RF_addr;
					psend_mcp(minion,&LBr);
					DISCONNECT; // will reset minion state properly
}//----?
				break;
				/* handle "move away" the same as a move request */
				case CID_MoveAway:
				/* handle emergency stop the same as a move request */               
				case CID_Stop: 
				/* handle continous move the same as a move request */               
				case CID_Continuous_Move_Request:
				/* handle dock the same as a move request */               
				case CID_Dock:
				/* handle go home the same as a move request */               
				case CID_Gohome:
				/* ...all the above fall through... */

				case CID_Move_Request:
					DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request  send 'S'uccess ack.   message_2100->speed=%f, message_2100->move=%i",minion->mID,message_2100->speed, message_2100->move);
					//DCMSG(CYAN,"Minion %i: message_2100->speed=%f, message_2100->move=%i",minion->mID,message_2100->speed, message_2100->move);
               				// also build an LB packet  to send
					LB_move =(LB_move_t *)&LB_buf;// make a pointer to our buffer so we can use the bits right
					LB_move->cmd=LBC_MOVE;
					LB_move->addr=minion->RF_addr;

					// minion->S.exp.data=0; // cheat and set the current state to 45
					minion->S.speed.newdata=message_2100->speed; // set newdata to be the future state
					minion->S.move.newdata=message_2100->move; // set newdata to be the future state

					// set our response cache move data as fresh
					minion->S.resp.last_direction = -1; // won't match as "same" next time
					minion->S.resp.last_speed = -1; // won't match as "same" next time
					minion->S.resp.last_position = -100; // won't match as "same" next time

					LB_move->speed = ((int)(max(0.0,min(htonf(message_2100->speed), 20.0)) * 100)) & 0x7ff;
					DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: speed after: %i",minion->mID, LB_move->speed);
					switch (message_2100->cid) {
						case CID_Move_Request:
							if (message_2100->move == 2) {
								DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: direction 2",minion->mID);
								LB_move->move = 2;
								minion->S.resp.mover_command = move_left;
							}
							else if (message_2100->move == 1) {
								DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: direction 1",minion->mID);
								LB_move->move = 1;
								minion->S.resp.mover_command = move_right;
							}
							else {
								DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: direction 0",minion->mID);
								LB_move->speed = 0;
							}

							if (LB_move->speed == 0) {
								DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: speed 0",minion->mID);
								LB_move->move = 0;
								minion->S.resp.mover_command = move_stop;
							}
						break;
						case CID_Stop:
							DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: E-Stop",minion->mID);
							LB_move->speed = 2047; // emergency stop speed
							minion->S.resp.mover_command = move_stop;
						break;
						case CID_Continuous_Move_Request:
							DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: Continuous move",minion->mID);
							LB_move->move = 3;
							minion->S.resp.mover_command = move_continuous;
						break;
						case CID_Dock:
							DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: Dock",minion->mID);
							LB_move->move = 4;
							minion->S.resp.mover_command = move_dock;
						break;
						case CID_Gohome:
							DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: Go home",minion->mID);
							LB_move->move = 5;
							minion->S.resp.mover_command = move_left; // home is left for now
						break;
						case CID_MoveAway:
							DDCMSG(D_PACKET,BLUE,"Minion %i: CID_Move_Request: Move away",minion->mID);
							LB_move->move = 6;
							minion->S.resp.mover_command = move_away; // home is left for now
						break;
					}

					// new timer stuff
					if (minion->S.resp.mover_command == move_stop) {
						// stop request
						if (minion->S.resp.current_exp == 0) { // not exposed
							minion->S.ignoring_response = 1; // ignoring responses due to moving
							STOP_FAST_LOOKUP(minion->S);
							Cancel_Status_Resp_Timer(minion);
						}
					}
					else {
						// move request
						minion->S.ignoring_response = 1; // ignoring responses due to moving
						STOP_SLOW_LOOKUP(minion->S);
						Cancel_Status_Resp_Timer(minion);
					}

					// set what we're moving towards, as an index
					if (LB_move->speed != 2047) {
						minion->S.speed.towards_index = max(0, min(LB_move->speed / 100, 4)); // SR doesn't send actual speed, it sends the index (TODO -- fix everywhere so TRACR can work)
					}
					else {
						minion->S.speed.towards_index = 0;
					}

					// now send it to the MCP master
					DDCMSG(D_PACKET,BLUE,"Minion %i:  LB_move cmd=%i", minion->mID,LB_move->cmd);
					//if (LB_move->speed < 2047 && minion->S.move.data == 0)

					{
						// keep track of command if it's doing something
						sequence_tracker_t *t;
						psend_mcp_seq(minion,LB_move);

						// fill in tracker info
						t = malloc(sizeof(sequence_tracker_t));
						t->sequence = minion->S.last_sequence_sent;
						LB_move = malloc(sizeof(LB_move_t)); // turn link into allocated data
						memcpy(LB_move, &LB_buf, sizeof(LB_move_t));
						t->pkt = (char*)LB_move;
						t->data = NULL;
						t->sent_callback = MoveSentCallback;
						t->removed_callback = MoveRemovedCallback;
						t->missed = 50; // after we don't get a callback for this message X times, but we do for other messages, give up
						// place at head of chain
						t->next = minion->S.tracker;
						minion->S.tracker = t;

						// get ready to fake movement
						minion->S.last_move_time = -1; // not sent for this movement message
						//DDCMSG(D_POINTER,BLUE,"Moving minion %i with sequence %i because %i and %i", minion->mID, minion->S.last_sequence_sent, LB_move->speed, minion->S.move.data);
						// } else {
						//   // send changes
						//   psend_mcp(minion,LB_move);
						//   //DDCMSG(D_POINTER,BLUE,"Moving minion %i without sequence because %i and %i", minion->mID, LB_move->speed, minion->S.move.data);
					}

					// send 2101 ack  (2102's will be generated at start and stop of actuator)
					send_2101_ACK(header,'S',minion);    // TRACR Cert complains if these are not there

					// it should happen in this many deciseconds
					//  - fasit device cert seems to come back immeadiately and ask for status again, and
					//    that is causing a bit of trouble.              break;
				break;
				case CID_Config_Hit_Sensor:
					DDCMSG(D_PARSE,BLUE,"Minion %i: CID_Config_Hit_Sensor  send a 2102 in response",minion->mID);
					DDCMSG(D_PARSE,BLUE,"Full message decomposition....");
					DDCMSG(D_PARSE,BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6i  %i.%i  %6i  %6i  %7i",htons(header->num),htons(header->icd1),htons(header->icd2),htonl(header->seq),htons(header->rsrvd),htons(header->length));
					DDCMSG(D_PARSE,BLUE,"\t\t\t\t\t\t\tmessage body\n"\
						"C-ID | Expos | Aspct |  Dir | Move |  Speed | On/Off | Hits | React | ToKill | Sens | Mode | Burst\n"\
						"%3i    %3i     %3i     %2i    %3i    %7.2f     %4i     %2i     %3i     %3i     %3i    %3i   %5i ",message_2100->cid,message_2100->exp,message_2100->asp,message_2100->dir,message_2100->move,message_2100->speed,message_2100->on,htons(message_2100->hit),message_2100->react,htons(message_2100->tokill),htons(message_2100->sens),message_2100->mode,htons(message_2100->burst));

					// also build an LB packet  to send
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
					}
					else if (minion->S.hit.data == (htons(message_2100->hit)) - 1) {
						LB_configure->hitcountset = 2; // incriment by one
					}
					else if (htons(message_2100->hit) == 0) {
						LB_configure->hitcountset = 1; // reset to zero
					}
					else {
						LB_configure->hitcountset = 0; // no change
					}

					// now send it to the MCP master
					DDCMSG(D_PACKET,BLUE,"Minion %i:  LB_configure cmd=%i", minion->mID,LB_configure->cmd); 					psend_mcp(minion,LB_configure);

					// actually Riptide says that the FASIT spec is wrong and should not send an ACK here
					// send_2101_ACK(header,'S',minion); // FASIT Cert seems to complain about this ACK

					// send 2102 status - after doing what was commanded
					// which is setting the values in the hit_calibration structure
					// uses the lastHitCal so what we set is recorded
					// there are fields that don't match up with the RF slaves, but we will match up our states
					// TODO I believe a 2100 config hit sensor is supposed to set the hit count
					// BDR: I think the lastHitCal won't be needed as that is stuff on the lifter board, and we
					// won't need to simulate at that low of a level.

					minion->S.on.newdata  = message_2100->on;        // set the new value for 'on'
					// minion->S.on.flags  |= F_tell_RF;        // just note it was set

					if (htons(message_2100->burst)) {
						minion->S.burst.newdata = htons(message_2100->burst); // spec says we only set if non-Zero
						// minion->S.burst.flags |= F_tell_RF;   // just note it was set
					}

					if (htons(message_2100->sens)) {
						minion->S.sens.newdata = htons(message_2100->sens);
						// minion->S.sens.flags |= F_tell_RF;    // just note it was set
					}
					// remember stated value for later
					// fake_sens = htons(message_2100->sens);

					if (htons(message_2100->tokill)) {
						minion->S.tokill.newdata = htons(message_2100->tokill);
						// minion->S.tokill.flags |= F_tell_RF;  // just note it was set
					}

					//if (htons(message_2100->hit)) {
					//	minion->S.hit.newdata = htons(message_2100->hit); -- TODO -- new hit time message
					//	minion->S.hit.flags |= F_tell_RF;
					//}

					minion->S.react.newdata = message_2100->react;
					// minion->S.react.flags |= F_tell_RF;      // just note it was set

					minion->S.mode.newdata = message_2100->mode;
					// minion->S.mode.flags |= F_tell_RF;       // just note it was set

					minion->S.burst.newdata = htons(message_2100->burst);
					// minion->S.burst.flags |= F_tell_RF;      // just note it was set

					// doHitCal(lastHitCal); // tell kernel by calling SIT_Clients version of doHitCal
					DDCMSG(D_PACKET,BLUE,"Minion %i: calling doHitCal after setting values",minion->mID) ;

					// send 2102 status or change the hit count (which will send the 2102 later)
					if (1 /*hits == htons(message_2100->hit)*/) {
						//DDCMSG(D_POINTER|D_NEW, YELLOW, "calling inform() @ %i", __LINE__);
						// sendStatus2102(1,header,minion,mt);  // sends a 2102 as we won't if we didn't change the the hit count -- removed in favor of inform_state()
						DDCMSG(D_PACKET,BLUE,"Minion %i: We will send 2102 status in response to the config hit sensor command",minion->mID);
						inform_state(minion, mt, header);  // sends a 2102 as we won't if we didn't change the the hit count
					}
					else {
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
				// also build an LB packet  to send
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
		case 2110: {
			LB_accessory_t lba;
			message_2110 = (FASIT_2110*)(buf + sizeof(FASIT_header));
			DDCMSG(D_PACKET,BLUE,"Minion %i: fasit packet 2110 Configure_Muzzle_Flash, seq=%i  on=%i  mode=%i  idelay=%i  rdelay=%i",minion->mID,htonl(header->seq),message_2110->on,message_2110->mode,message_2110->idelay,message_2110->rdelay);

			// check to see if we have muzzle flash capability -  or just pretend
			// Shelly - comment out the following if because it's broken for mit/sits
			/*if (minion->S.cap&PD_NES){*/
				minion->S.mfs_on.newdata  = message_2110->on;       // set the new value for 'on'
				// minion->S.mfs_on.flags  |= F_tell_RF;       // just note it was set
				minion->S.mfs_mode.newdata  = message_2110->mode;   // set the new value for 'on'
				// minion->S.mfs_mode.flags  |= F_tell_RF;     // just note it was set
				minion->S.mfs_idelay.newdata  = message_2110->idelay;       // set the new value for 'on'
				// minion->S.mfs_idelay.flags  |= F_tell_RF;   // just note it was set
				minion->S.mfs_rdelay.newdata  = message_2110->rdelay;       // set the new value for 'on'
				// minion->S.mfs_rdelay.flags  |= F_tell_RF;   // just note it was set

				//doMFS(msg->on,msg->mode,msg->idelay,msg->rdelay);
				// when the didMFS happens fill in the 2112, force a 2112 message to be sent
				sendStatus2112(1,header,minion); // forces sending of a 2112

				// sendStatus2102(0,header,minion,&mt); // forces sending of a 2102
				//sendMFSStatus = 1; // force

				// send low-bandwidth message
				lba.cmd = LBC_ACCESSORY;
				lba.addr = minion->RF_addr;
				lba.on = message_2110->on;
				lba.type = 0; // mfs
				lba.idelay = message_2110->idelay;
				lba.rdelay = message_2110->rdelay;
				psend_mcp(minion,&lba);
			/*} else {  // Shelly - commented out
        			send_2101_ACK(header,'F',minion);  // no muzzle flash capability, so send a negative ack
			}*/
		}
		break;
		case 2114: {
			LB_accessory_t lba;
			message_2114 = (FASIT_2114*)(buf + sizeof(FASIT_header));

			DDCMSG(D_NEW,MAGENTA,"Minion %i: fasit packet 2114 Configure_MSDH, seq=%i  code=%i  ammo=%i  player=%i  delay=%i", minion->mID,htonl(header->seq),message_2114->code,message_2114->ammo,htons(message_2114->player),message_2114->delay);

			// reply to server
			sendStatus2115(header,minion,message_2114);

			// send low-bandwidth message
			lba.cmd = LBC_ACCESSORY;
			lba.addr = minion->RF_addr;
			lba.on = message_2114->delay <= 60; // delay of lower than a minute = on
			lba.type = 3; // msdh
			lba.idelay = message_2114->delay;
			lba.rdelay = 0; // no repeat delay
			DDCMSG(D_NEW,YELLOW, "Before send...");
			set_crc8(&lba);
			DDpacket((char*)&lba, RF_size(lba.cmd));
			psend_mcp(minion,&lba);
		}
		break;
		case 13110: {
			LB_accessory_t lba;
			message_13110 = (FASIT_13110*)(buf + sizeof(FASIT_header));

			DDCMSG(D_NEW,MAGENTA,"Minion %i: fasit packet 13110 Configure_MGL, seq=%i  on=%i",minion->mID,htonl(header->seq),message_13110->on);

			// reply to server
			sendStatus13112(header,minion,message_13110);

			// send low-bandwidth message
			lba.cmd = LBC_ACCESSORY;
			lba.addr = minion->RF_addr;
			lba.on = message_13110->on;
			lba.type = 2; // mgl
			lba.idelay = 0; // no initial delay
			lba.rdelay = 0; // no repeat delay
			DDCMSG(D_NEW,YELLOW, "Before send...");
			set_crc8(&lba);
			DDpacket((char*)&lba, RF_size(lba.cmd));
			psend_mcp(minion,&lba);
		}
		break;
		case 14110: {
			LB_accessory_t lba;
			message_14110 = (FASIT_14110*)(buf + sizeof(FASIT_header));
			DDCMSG(D_NEW,MAGENTA,"Minion %i: fasit packet 14110 Configure_PHI, seq=%i  on=%i",minion->mID,htonl(header->seq),message_14110->on);

			// reply to server
			sendStatus14112(header,minion,message_14110);

			// send low-bandwidth message
			lba.cmd = LBC_ACCESSORY;
			lba.addr = minion->RF_addr;
			lba.on = message_14110->on;
			lba.type = 1; // phi
			lba.idelay = 0; // no initial delay
			lba.rdelay = 0; // no repeat delay
			DDCMSG(D_NEW,YELLOW, "Before send...");
			set_crc8(&lba);
			DDpacket((char*)&lba, RF_size(lba.cmd));
			psend_mcp(minion,&lba);
		}
		break;
		case 15110: {
			LB_accessory_t lba;
			message_15110 = (FASIT_15110*)(buf + sizeof(FASIT_header));
			DDCMSG(D_NEW,MAGENTA,"Minion %i: fasit packet 15110 Configure_Thermals, seq=%i  on=%i", minion->mID,htonl(header->seq),message_15110->on);

			// reply to server
			sendStatus15112(header,minion,message_15110);

			// send low-bandwidth message
			lba.cmd = LBC_ACCESSORY;
			lba.addr = minion->RF_addr;
			lba.on = message_15110->on;
			lba.type = 4; // thermals
			lba.idelay = 0; // no initial delay
			lba.rdelay = 0; // no repeat delay
			DDCMSG(D_NEW,YELLOW, "Before send...");
			set_crc8(&lba);
			DDpacket((char*)&lba, RF_size(lba.cmd));
			psend_mcp(minion,&lba);
		}
		break;
		case 14200: {
			LB_hit_blanking_t lhb;
			message_14200 = (FASIT_14200*)(buf + sizeof(FASIT_header));

			DDCMSG(D_NEW,MAGENTA,"Minion %i: fasit packet 14200 Configure_Hit_Blanking, seq=%i  blank=%i", minion->mID,htonl(header->seq),message_14200->blank);

			// there is no reply to server

			// send low-bandwidth message
			lhb.cmd = LBC_HIT_BLANKING;
			lhb.addr = minion->RF_addr;
			lhb.blanking = htons(message_14200->blank);
			DDCMSG(D_NEW,YELLOW, "Before send...");
			set_crc8(&lhb);
			DDpacket((char*)&lhb, RF_size(lhb.cmd));
			psend_mcp(minion,&lhb);
		}
		break;
	}  /**   end of 'switch (packet_num)'   **/
}

void finishTransition(thread_data_t *minion) {
	if (minion->S.exp.data == 45 && minion->S.exp.newdata == 90) {
		// transitioned from conceal to expose
		//DDCMSG(D_POINTER|D_NEW, MAGENTA, "starting exp timer @ %s:%i", __FILE__, __LINE__);
		//START_EXPOSE_TIMER(minion->S); // will do non-45 based timer stuff
	} else if (minion->S.exp.data == 45 && minion->S.exp.newdata == 0) {
		// transitioned from expose to conceal
		//DDCMSG(D_POINTER|D_NEW, MAGENTA, "starting con timer @ %s:%i", __FILE__, __LINE__);
		//START_CONCEAL_TIMER(minion->S); // will do non-45 based timer stuff
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

/*********************
 minion_thread

 Minion processes are started by the MCP. They simulate lifters
 or other PDs (presentation devices). Each minion has it's own
 connection to the RCC (Range Control Computer) which is used
 for FASIT communications, and another connection to the MCP
 which is used to pass communications to the PD via RF.

 All RF communications will pass through the MCP, which
 forwards them to the RF process. The RF process groups and
 ungroups the messages. When the minion recieves a FASIT
 command, it sets up a response that simulates the real PD's
 state. It also passes a message to the MCP which sends it to
 the RF process which radios the command to the slave [bosses
 eventually].
 *********************/

void *minion_thread(thread_data_t *minion){

	//Init & Decl
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

	LB_packet_t *LB;
	LB_device_reg_t *LB_devreg;
	LB_assign_addr_t *LB_addr;
	LB_expose_t *LB_exp;

	// wait a few seconds for the mcp to send the ASSIGN ADDR packet
	sleep(5);

	//initialize_state: Initialize values that were not set by the MCP
	initialize_state( &minion->S );
	minion->S.exp.last_step = TS_too_far; // invalid step, will have to find way back

	DDCMSG(D_RF, BLUE,"Minion %i: state is initialized as devid 0x%06X", minion->mID,minion->devid);
	DDCMSG(D_RF, BLUE,"Minion %i: RF_addr = %i capabilities=%i  device_type=%i", minion->mID,minion->RF_addr,minion->S.cap,minion->S.dev_type);

	//Check Minion capabilities
	if (minion->S.cap & PD_NES) {
		DDCMSG(D_RF, BLUE,"Minion %i: has Night Effects Simulator (NES) capability", minion->mID);
	}

	if (minion->S.cap & PD_MILES) {
		DDCMSG(D_RF, BLUE,"Minion %i: has MILES (shootback) capability", minion->mID);
	}

	if (minion->S.cap & PD_GPS) {
		DDCMSG(D_RF, BLUE,"Minion %i: has Global Positioning System (GPS) capability", minion->mID);
	}

	i=0;

	// setup the timeout for the first select. subsequent selects have the timout set
	// in the timer related bits at the end of the loop
	// the goal is to update all our internal state timers with tenth second resolution

	minion->S.state_timer=900;   // every 10.0 seconds, worst case

	clock_gettime(CLOCK_MONOTONIC,&mt.istart_time);  // get the intial current time
	minion->rcc_sock=-1; // mark the socket so we know to open again
	minion->rcc_connected = 0; // we've never connected to the rcc before

	// main loop 
	// respond to the mcp commands
	// respond to FASIT commands
	// loop and reconnect to FASIT if it disconnects.
	// feed the MCP packets to send out the RF transmitter
	// update our state when MCP commands are RF packets back from our slave(s)

	while(!close_nicely) {

		if (minion->rcc_sock<0) {
			// Always runs first pass through loop. Get a connection to the RCC using fasit.
			do {
				minion->rcc_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

				if(minion->rcc_sock < 0) {
	               			PERROR("socket() failed");
				}

				result=connect(minion->rcc_sock,(struct sockaddr *) &fasit_addr, sizeof(struct sockaddr_in));

				if (result<0) {
					close(minion->rcc_sock);

					strerror_r(errno,mb.buf,BufSize);
					DCMSG(RED,"Minion %i: fasit server not found! connect(...,%s:%i,...) error : %s  ", minion->mID,inet_ntoa(fasit_addr.sin_addr),htons(fasit_addr.sin_port),mb.buf);
					DCMSG(RED,"Minion %i:   waiting for a bit and then re-trying ", minion->mID);
					minion->rcc_sock=-1;     // make sure it is marked as closed
					sleep(10);               // adding a little extra wait
				} else {
					// set keepalive so we disconnect on link failure or timeout
					setsockopt(minion->rcc_sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int)); 
				}

			} while (result<0);

			// we now have a socket.
			DDCMSG(D_MINION,BLUE,"Minion %i: has a socket to a RCC", minion->mID);
		}//End if (minion->rcc_sock<0)

		/* create a fd_set so we can monitor both the mcp and the connection to the RCC*/
		FD_ZERO(&rcc_or_mcp);
		FD_SET(minion->mcp_sock,&rcc_or_mcp);	// we are interested hearing the mcp
		FD_SET(minion->rcc_sock,&rcc_or_mcp);	// we also want to hear from the RCC

		/* block for up to "state_timer" deciseconds waiting for RCC or MCP */
		mt.timeout.tv_sec=minion->S.state_timer/10;
		mt.timeout.tv_usec=100000*(minion->S.state_timer%10);
		timestamp(&mt);

		DDCMSG(D_TIME,CYAN,"Minion %i: before Select... state_timer=%i (%i.%03i) at %i.%03iet",minion->mID,minion->S.state_timer,(int)mt.timeout.tv_sec,(int)mt.timeout.tv_usec/1000,DEBUG_TS(mt.elapsed_time));

		sock_ready=select(FD_SETSIZE,&rcc_or_mcp,(fd_set *) 0,(fd_set *) 0, &mt.timeout); 
		// If we are running on linux, mt.timeout will have a remaining time left in it if
		// one of the file descriptors was ready. We are going to use that for now.

		oldtimer=minion->S.state_timer;	// copy our old time for debugging
		minion->S.state_timer=(mt.timeout.tv_sec*10)+(mt.timeout.tv_usec/100000L); // convert to deciseconds

		if (sock_ready<0){
			PERROR("NOTICE!  select error : ");
			exit(-1);
		}

		timestamp(&mt);
		DDCMSG(D_TIME,CYAN,"Minion %i: After Select: sock_ready=%i FD_ISSET(mcp)=%i FD_ISSET(rcc)=%i oldtimer=%i state_timer=%i at %i.%03iet, delta=%i.%03i",minion->mID,sock_ready,FD_ISSET(minion->mcp_sock,&rcc_or_mcp),FD_ISSET(minion->rcc_sock,&rcc_or_mcp),oldtimer,minion->S.state_timer,DEBUG_TS(mt.elapsed_time), DEBUG_TS(mt.delta_time));

		//check to see if the MCP has any commands for us
		if (FD_ISSET(minion->mcp_sock,&rcc_or_mcp)){
			msglen=read(minion->mcp_sock, mb.buf, 1); // grab first byte so we know how many to grab

			if (msglen == 1) {
				LB=(LB_packet_t *)mb.buf;   // map the header in
				msglen+=read(minion->mcp_sock, (mb.buf+1), RF_size(LB->cmd)-1); // grab the rest, but leave the rest to be read again later
				DDCMSG(D_NEW, RED, "Minion %i read %i bytes with %i cmd", minion->mID, msglen, LB->cmd);

				if (verbose&D_PACKET) {
					DDpacket(mb.buf,msglen);
				}
			}

			if (msglen > 0) {
				mb.buf[msglen]=0;
				if (verbose&D_PACKET) {      // don't do the sprintf if we don't need to             
					sprintf(mb.hbuf,"Minion %i: received %i from MCP (RF) ", minion->mID,msglen);
					DDCMSG_HEXB(D_PACKET,YELLOW,mb.hbuf,mb.buf,msglen);
				}

				// we received something over RF, so we haven't timed out: reset the rf timer to look in 5 minutes -- TODO -- make this timeout smarter, or at the very least, user configurable
				// we have received a message from the mcp, process it
				// it is either a command, or it is an RF response from our slave

				LB=(LB_packet_t *)mb.buf;   // map the header in

				crc=crc8(LB);
				DDCMSG(D_RF,YELLOW,"Minion %i: LB packet (cmd=%2i aidr=%i crc=%i)", minion->mID,LB->cmd,LB->addr,crc);

				switch (LB->cmd){
					case LB_CONTROL_SENT: {
						LB_control_sent_t *lcs = (LB_control_sent_t*)LB;
						sequence_tracker_t *tracker = minion->S.tracker, *tt;

						// look for things tracking this sequence
						while (tracker != NULL) {
							if (tracker->sequence == lcs->sequence) {
								// found it
								if (tracker->sent_callback != NULL) {
									tracker->sent_callback(minion, &mt, tracker->pkt, tracker->data);
								}
								// clear and move on
								tt = tracker;
								tracker = tracker->next;
								ClearTracker(minion, tt);
								free(tt);
							} else if (--tracker->missed <= 0) {
								// missed too many times
								// clear and move on
								tt = tracker;
								tracker = tracker->next;
								ClearTracker(minion, tt);
								free(tt);
							} else {
								// not this one
								tracker = tracker->next;
							}
						}
					}
					break;
					case LB_CONTROL_REMOVED: {
						LB_control_sent_t *lcs = (LB_control_sent_t*)LB;
						sequence_tracker_t *tracker = minion->S.tracker, *tt;
						// look for things tracking this sequence
						while (tracker != NULL) {
							if (tracker->sequence == lcs->sequence) {
								// found it
								if (tracker->removed_callback != NULL) {
									tracker->removed_callback(minion, &mt, tracker->pkt, tracker->data);
								}
								// clear and move on
								tt = tracker;
								tracker = tracker->next;
								ClearTracker(minion, tt);
								free(tt);
							} else if (--tracker->missed <= 0) {
								// missed too many times
								// clear and move on
								tt = tracker;
								tracker = tracker->next;
								ClearTracker(minion, tt);
								free(tt);
							} else {
								// not this one...move on
								tracker = tracker->next;
							}
						}
					}
					break;
					case LBC_REQUEST_NEW:
						DDCMSG(D_NEW,YELLOW,"Minion %i: Recieved 'request new devices' packet.",minion->mID);
						// the response was created when the MCP spawned us, so we do nothing for this packet
					break;
					case LBC_DEVICE_REG: {
						DDCMSG(D_POINTER, MAGENTA, "Minion %i connected or reconnected", minion->mID);
						LB_devreg =(LB_device_reg_t *)(LB);    // map our bitfields in
						// this is either our original status on first connect or on reconnect, treat it as the only status
						minion->S.resp.newdata = 1;
						minion->S.resp.lastdata = 0;
						// partial reset of stuff here
						minion->S.ignoring_response = 0;
						minion->S.exp.last_step = TS_too_far; // invalid step, will have to find way back
						minion->S.exp.data_uc = -1;
						minion->S.exp.data_uc_count = 0;
						// copy the fields over to where we care about them
						minion->S.resp.current_exp = LB_devreg->expose ? 90 : 0;
						minion->S.resp.current_direction = LB_devreg->move ? 1 : 2;
						minion->S.resp.current_speed = LB_devreg->speed; // unmodified, but still treat as modified
						minion->S.resp.current_position = signPosition(LB_devreg->location);
						minion->S.resp.last_exp = -1;
						minion->S.resp.last_direction = -1;
						minion->S.resp.last_speed = -1;
						minion->S.resp.last_position = -100;
						// not a cached response, just is
						if (badFault(LB_devreg->fault)) {
							minion->S.fault.data = LB_devreg->fault;
							minion->S.fault.lastdata = LB_devreg->fault; // keep a copy to watch for repeats
						} else {
							minion->S.fault.data = ERR_normal;
							minion->S.fault.lastdata = ERR_normal; // keep a copy to watch for repeats
						}
						minion->S.resp.did_exp_cmd = 0;
						// cached differently
						minion->S.resp.last_hit = minion->S.resp.current_hit;
						minion->S.resp.current_hit = 0;
						// start proper timer
						if (minion->S.resp.current_speed == 0 && minion->S.resp.current_exp == 0) {
							// stopped and concealed = slow lookup
							DO_SLOW_LOOKUP(minion->S);
						} else {
							// moving and/or exposed = fast lookup
							DO_FAST_LOOKUP(minion->S);
						}
						// check to see if this is a reconnect or the original connect
						if (minion->rcc_connected) {
							// we've connected before, so create a reconnect event on the rcc by disconnecting here...
							close(minion->rcc_sock);
							minion->rcc_sock=-1;	// make sure it is marked as closed
							sleep(1);	// adding a little extra wait
							// ... and reconnecting at the beginning of the loop
							// don't mess with rcc_connected as we won't get a LBC_DEVICE_REG packet later
						} else {
							// we've now connected (although technically we connected at the beginning of the minion loop)
							minion->rcc_connected = 1;
						}
					}
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

						DDCMSG(D_POINTER,YELLOW,"Minion %i: (Rf_addr=%i) parsed 'Event_report'. hits=%i  sending 2102 status",minion->mID,minion->RF_addr,L->hits);

						// new state timer code
						// find existing report
						this_r = minion->S.report_chain;
						while (this_r != NULL) {
							if (this_r->report == L->report && this_r->event == L->event && this_r->hits == L->hits) { /* if we didn't actually send the hits before -- for instance, unqualified turning into qualified -- we set this_r->hits to 0, so this won't match and we'll send 16000 below */
								break; // found it!
							}
							last_r = this_r;
							this_r = this_r->next;
						}

						// did I find one? then don't send hit time message to FASIT server
						if (this_r == NULL) {
							int now_m, start_time_m, end_time_m, delta_m; // times in milliseconds for easy calculations
							int send_hit = 0;
							// none found, create one ...
							this_r = malloc(sizeof(report_memory_t));
							if (last_r != NULL) {
							// place in chain correctly
								last_r->next = this_r;
							} else {
							// is head of chain
								minion->S.report_chain = this_r;
							}

							// find start time and end times (calculate using when the FASIT server logged the times)
							if (minion->S.exp.log_start_time[L->event] == 0) {
								// this implies we never logged the start time on the FASIT server...which is bad
								minion->S.exp.log_start_time[L->event] = minion->S.exp.cmd_start_time[L->event];
							}

							timestamp(&mt);      
							now_m = ts2ms(&mt.elapsed_time);
							start_time_m = minion->S.exp.log_start_time[L->event];

							if (L->qualified) {
								// qualified hits occured between valid expose and conceal logs, use those times
								end_time_m = minion->S.exp.log_end_time[L->event];
								DDCMSG(D_POINTER, GREEN, "Qual hit...%s:%i cmd end time (changing %3i.%03i to %i)", __FILE__, __LINE__, DEBUG_MS(minion->S.exp.log_end_time[L->event]), end_time_m);
								send_hit = 1; // yes, send the hit time message
							} else {
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
								minion->S.resp.ever_hit = 1;
								if (L->qualified) {
									// half way between original start and end...
									// (which are now reversed in size as they are "time since now,"
									// and start happened further away from now)
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
								// update response state
								if (L->event == minion->S.exp.event) {
									// we are looking at data between exposure command events, so treat it as fresh
									minion->S.resp.data -= L->hits; // data inits to hits to kill value
									minion->S.resp.current_hit += L->hits; // accumulate hits for this response
								}
							} else {
								DDCMSG(D_NEW, GREEN, "Minion %i: Didn't send %i hits", minion->mID, L->hits);
								L->hits = 0; // don't "ack" these hits
							}
						}
                  
						// set data, reset the counters
						this_r->report = L->report;
						this_r->event = L->event;
						this_r->hits = L->hits;
						this_r->unreported = 0;

						// only send the ack if we have anything to acknowledge
						if (this_r->hits > 0) {
							setTimerTo(this_r->s, timer, flags, EVENT_SOON_TIME, F_event_ack); // send an ack back for this report
						}
						// move forward counters on *all* remembered reports, vacuuming as needed
						//   -- NOTE -- the MAX value is set so that we should move these forward *each* time we get
						//              any report message as there will be a maximum number per burst
						this_r = minion->S.report_chain;
						while (this_r != NULL) {
							// check to see if this link needs to be vacuumed
							if (++this_r->unreported >= EVENT_MAX_UNREPORT) {
								// connect the chain links and free the memory of the old one
								report_memory_t *temp = this_r->next;
								free(this_r);
								this_r = temp;
								continue; // loop again
							}
							// move to next link in chain
							last_r = this_r;
							this_r = this_r->next;
						}
					}
					break;
					case LBC_STATUS_RESP:
					{
						// new state timer code
						// reset the timer miss counters, keep going on the same timer interval so they burst
						minion->S.rf_t.slow_missed = 0;
						minion->S.rf_t.fast_missed = 0;

						LB_status_resp_t *L=(LB_status_resp_t *)(LB); // map our bitfields in
                  
						// simulated fault fields are ignored and never sent from here
						if (badFault(L->fault) == 0) {
							minion->S.fault.data = ERR_normal; // clear out these non-statuses immediately
						}
						if (minion->S.ignoring_response) {
							if (badFault(L->fault) != 1) { // non-faults can be ignored
								DDCMSG(D_POINTER, CYAN, "Dev %2x Ignored LBC_STATUS_RESP for now...%i", minion->devid, minion->S.ignoring_response);
							} else { // type 1 (faults) can't be ignored
								if (minion->S.fault.lastdata == L->fault) { // ...but we ignore repeats
									DDCMSG(D_POINTER, CYAN, "Dev %2x Ignored LBC_STATUS_RESP for now...%i", minion->devid, minion->S.ignoring_response);
								} else {
									minion->S.fault.data = L->fault; // will get handled eventually
									minion->S.fault.lastdata = L->fault; // keep a copy to watch for repeats
								}
							}
							break;
						}

						// check to see if we have previous valid data
						if (minion->S.resp.newdata) { // did we have valid data before?
							minion->S.resp.lastdata = 1; // we have old and new data
							// copy last from current
							minion->S.resp.last_exp = minion->S.resp.current_exp;
							minion->S.resp.last_direction = minion->S.resp.current_direction;
							minion->S.resp.last_speed = minion->S.resp.current_speed;
							minion->S.resp.last_position = minion->S.resp.current_position;
						} else {
							// haven't initialized, only have new data
							// ...will be filled in below...
							minion->S.resp.lastdata = 0; // we have only new data
						}

						// copy new data
						minion->S.resp.newdata = 1; // we have new data
						minion->S.resp.current_exp = L->expose ? 90 : 0;
						minion->S.resp.current_direction = L->move ? 1 : 2;
						minion->S.resp.current_speed = L->speed;
						minion->S.resp.current_position = L->location;

						//look at "did we ever" statuses
						if (L->expose) { minion->S.resp.ever_exp = 1; }
						if (!L->expose) { minion->S.resp.ever_con = 1; }
						if (L->did_exp_cmd && !L->expose) {
							minion->S.resp.ever_exp = 1;
						}
						if (L->did_exp_cmd && L->expose) {
							minion->S.resp.ever_con = 1;
						}
						if (L->speed == 0) { minion->S.resp.ever_stop = 1; }
						if (minion->S.resp.lastdata) {
							// look for movement that it did before now and is doing now
							if (minion->S.resp.last_position > L->location) {
								minion->S.resp.ever_move_left = 1;
								minion->S.resp.ever_move = 1;
							} else if (minion->S.resp.last_position < L->location) {
								minion->S.resp.ever_move_right = 1;
								minion->S.resp.ever_move = 1;
							}
						} else {
							// look only at movement that it is doing now
							if (L->speed > 0) {
								minion->S.resp.ever_move = 1;
								if (L->move) {
									minion->S.resp.ever_move_right = 1;
								} else {
									minion->S.resp.ever_move_left = 1;
								}
							}
						}

						// not a cached response, just is until reset or overwrite
						if (L->fault == ERR_stop_left_limit || L->fault == ERR_stop_right_limit) {
							// don't overwrite fake fault data with real data
							minion->S.fault.data = ERR_normal;
						} else {
							if (badFault(L->fault) != -2 && minion->S.fault.lastdata == L->fault) { // use copy when looking for repeats
								// ignore repeat fault
								minion->S.fault.data = ERR_normal;
							} else {
								if (badFault(L->fault)) {
									// a fault (or status) we use
									minion->S.fault.data = L->fault;
									minion->S.fault.lastdata = L->fault; // keep a copy to watch for repeats
								} else {
									// not a fault we use
									minion->S.fault.data = ERR_normal;
									minion->S.fault.lastdata = ERR_normal;
								}
							}
						}

						minion->S.resp.did_exp_cmd = L->did_exp_cmd;

						// do Handle_Status_Resp() from timer so we can look at collected hit data
						setTimerTo(minion->S.resp, timer, flags, RESP_TIME, F_resp_handle);
					}
					break;
					default:
						DDCMSG(D_RF,YELLOW,"Minion %i:  recieved a cmd=%i    don't do anything",minion->mID,LB->cmd);
					break;
				}  // End switch on LB->cmd

			} else if (msglen<0) {
				if (errno!=EAGAIN){
					DCMSG(RED,"Minion %i: read returned %i errno=%i socket to MCP closed, we are FREEEEEeeee!!!",minion->mID,msglen,errno);
					exit(-2);  /* this minion dies!   it should do something else maybe  */
				}
			} else {
				DCMSG(RED,"Minion %i: socket to MCP closed.", minion->mID);
				exit(-2);  /* this minion dies!  possibly it should do something else - but maybe it dies of happyness  */
			}

			timestamp(&mt);    
			DDCMSG(D_TIME,CYAN,"Minion %i: End of MCP Parse at %i.%03iet, delta=%i.%03i",minion->mID, DEBUG_TS(mt.elapsed_time), DEBUG_TS(mt.delta_time));
		}

		/* End of reading and processing mcp command. Check for commands from the rcc. */

		if (FD_ISSET(minion->rcc_sock,&rcc_or_mcp)){
			// now read it using the new routine    
			result = read_FASIT_msg(minion,mb.buf, BufSize);
			if (result>0){         
				//There is a message from rcc/fasit server

				//Test packet count?
				mb.header = (FASIT_header*)(mb.buf);	// find out how long of message we have
				length=htons(mb.header->length);	// set the length for the handle function
				if (result>length){
					DDCMSG(D_PACKET,BLUE,"Minion %i: Multiple Packet  num=%i  result=%i seq=%i mb.header->length=%i",minion->mID,htons(mb.header->num),result,htonl(mb.header->seq),length);
				} else {
					DDCMSG(D_PACKET,BLUE,"Minion %i: num=%i  result=%i seq=%i mb.header->length=%i",
					minion->mID,htons(mb.header->num),result,htonl(mb.header->seq),length);
				}

				tbuf=mb.buf;	// use our temp pointer so we can step ahead

				// loop until result reaches 0
				while((result>=length)&&(length>0)) {
					timestamp(&mt);      
					DDCMSG(D_TIME,CYAN,"Minion %i:  Packet %i recieved at %i.%03iet, delta=%i.%03i",minion->mID,htons(mb.header->num), DEBUG_TS(mt.elapsed_time), DEBUG_TS(mt.delta_time));

					//process the packet
					handle_FASIT_msg(minion,tbuf,length,&mt);

					result-=length;	// reset the length to handle a possible next message in this packet
					tbuf+=length;	// step ahead to next message
					mb.header = (FASIT_header*)(tbuf);	// find out how long of message we have
					length=htons(mb.header->length);	// set the length for the handle function
					if (result){
						DDCMSG(D_PACKET,BLUE,"Minion %i: Continue processing the rest of the BIG fasit packet num=%i  result=%i seq=%i  length=%i",
						minion->mID,htons(mb.header->num),result,htonl(mb.header->seq),length);
					}
				}
			} else {
				strerror_r(errno,mb.buf,BufSize);
				DDCMSG(D_PACKET,RED,"Minion %i: read_FASIT_msg returned %i and Error: %s", minion->mID,result,mb.buf);
				DDCMSG(D_PACKET,RED,"Minion %i: which means it likely has closed!", minion->mID);
				close(minion->rcc_sock);
				minion->rcc_sock=-1;	// mark the socket so we know to open again
				minion->rcc_connected = 0;	// as we've now disconnected, pretend that we've never connected to the rcc before
			}

			timestamp(&mt);    
			DDCMSG(D_TIME,CYAN,"Minion %i: End of RCC Parse at %i.%03iet, delta=%i.%03i",minion->mID, DEBUG_TS(mt.elapsed_time), DEBUG_TS(mt.delta_time));
		}

		/* End of rcc command parsing */

		/* Update the counter. If less than a tenth of a second passed,
		   skip the timer updates and reloop, using a new select value
		   that is a fraction of a tenth */

		minion_state(minion, &mb);

	}  // while forever

	if (minion->rcc_sock > 0) { close(minion->rcc_sock); }
	if (minion->mcp_sock > 0) { close(minion->mcp_sock); }
}  // end of minion thread

