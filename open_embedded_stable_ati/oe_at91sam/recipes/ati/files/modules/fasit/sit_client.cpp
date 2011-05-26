#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

using namespace std;

#include "sit_client.h"
#include "common.h"
#include "timers.h"


// setup calibration table
const u32 SIT_Client::cal_table[16] = {0xFFFFFFFF,333,200,125,75,60,48,37,29,22,16,11,7,4,2,1};


/***********************************************************
*                     SIT_Client Class                     *
***********************************************************/
SIT_Client::SIT_Client(int fd, int tnum) : TCP_Client(fd, tnum) {
FUNCTION_START("::SIT_Client(int fd, int tnum) : Connection(fd)")
   // connect our netlink connection
   nl_conn = NL_Conn::newConn<SIT_Conn>(this);
   if (nl_conn == NULL) {
      deleteLater();
   } else {
      // initialize default settings

      // initial MFS settings
      // this needs to change to something like what Nathan says here
      // but for now my kludge of 'start_config' will hold a place
      /*
       * The correct method for determining if a MFS is available is to ask the kernel using the
       * NL_C_ACCESSORY netlink command. If you set the accessory_conf "request" to 1 and
       * the "acc_type" to ACC_NES_MFS you can expect a respone with the "exists" set correctly.
       * The SIT_Conn::parseData function will need to be looking for the NL_C_ACCESSORY command.
       *
       * A clean way to do this is to send the request at the initializer of the SIT client class
       * and then remember the value from parseData.
       * To actually test this you'll need to add "insmod target_generic_output.ko has_muzzle=1"
       * to your SIT script. You may want to look at target_generic_output.c and
       * define TESTING_ON_EVAL to enable the muzzle flash to use one of the LEDs on the dev board.
       */
      
      if (start_config&PD_MUZZLE){
	 doMFS(1, 1, 0, 2); // on when fully exposed, burst, no delay, 2 seconds between bursts
      } else {
	 // doMFS(0, 0, 0, 0); // Hopefully turns it of completely, or maybe we just don't call it
      }
      // TODO -- MILES SDH

      // initial hit calibration settings
      lastHitCal.seperation = 250;
      lastHitCal.sensitivity = cal_table[13]; // fairly sensitive, but not max
      lastHitCal.blank_time = 500; // half a second blanking
      lastHitCal.hits_to_fall = 1; // fall on first hit
      lastHitCal.after_fall = 0; // 0 for stay down
      lastHitCal.type = 0; // mechanical sensor
      lastHitCal.invert = 0; // don't invert sensor input line
      nl_conn->doHitCal(lastHitCal); // tell kernel

      hits = 0;
      doHits(-1); // get correct value from kernel

      lastBatteryVal = MAX_BATTERY_VAL;
      nl_conn->doBattery(); // get a correct battery value soon
   }
FUNCTION_END("::SIT_Client(int fd, int tnum) : Connection(fd)")
}

SIT_Client::~SIT_Client() {
FUNCTION_START("::~SIT_Client()")

FUNCTION_END("::~SIT_Client()")
}

// fill out 2102 status message
void SIT_Client::fillStatus2102(FASIT_2102 *msg) {
FUNCTION_START("::fillStatus2102(FASIT_2102 *msg)")

   // start with zeroes
   memset(msg, 0, sizeof(FASIT_2102));

   // fill out as response
   msg->response.rnum = resp_num;
   msg->response.rseq = resp_seq;
   resp_num = resp_seq = 0; // the next one will be unsolicited

   // exposure
   switch (exposure) {
      case 0: msg->body.exp = 0; break;
      case 1: msg->body.exp = 90; break;
      default: msg->body.exp = 45; break;
   }

   // device type
   msg->body.type = 1; // SIT. TODO -- SIT vs. SAT vs. HSAT

   doHits(-1);	// request the hit count
   
   DCMSG(YELLOW,"setting hits to  0x%X",hits) ; 

   // hit record
   msg->body.hit = htons(hits);		
   msg->body.hit_conf.on = 1; // TODO -- handle off, on/off at position
   switch (lastHitCal.after_fall) {
       case 0: // stay down
           msg->body.hit_conf.react = 0; // fall
           break;
       case 1: // bob
       case 2: // bob/stop
           msg->body.hit_conf.react = 4; // bob
           break;
       case 3: // stop
           msg->body.hit_conf.react = 3; // fall and stop
           break;
   }
   msg->body.hit_conf.tokill = htons(lastHitCal.hits_to_fall);
   for (int i=15; i>=0; i++) { // count backwards from most sensitive to least
      if (lastHitCal.sensitivity <= cal_table[i]) { // map our cal value to theirs
         msg->body.hit_conf.sens = htons(15); // found sensitivity value
         break; // done looking
      }
   }
   if (lastHitCal.seperation < 500) { // TODO -- valid seperation point for single/burst
         msg->body.hit_conf.mode = 1; // single
	 msg->body.hit_conf.burst = htons(250); // TODO -- max burst DCMSGseperation
	 DCMSG(YELLOW,"set seperation to htons 250=0x%X",msg->body.hit_conf.burst) ;
   } else {
         msg->body.hit_conf.mode = 2; // burst
         msg->body.hit_conf.burst = htons(lastHitCal.seperation); // burst seperation
	 DCMSG(YELLOW,"set seperation to  htons LHC=0x%X",msg->body.hit_conf.burst) ;
   }

   
FUNCTION_END("::fillStatus2102(FASIT_2102 *msg)")
}

// create and send a status messsage to the FASIT server
void SIT_Client::sendStatus2102() {
FUNCTION_START("::sendStatus2102()")

   FASIT_header hdr;
   FASIT_2102 msg;
   defHeader(2102, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2102));

   // fill message
   fillStatus2102(&msg); // fills in status values with current values

   // send
   queueMsg(&hdr, sizeof(FASIT_header));
   queueMsg(&msg, sizeof(FASIT_2102));
   finishMsg();


FUNCTION_END("::sendStatus2102()")
}

/***********************************************************
*                  FASIT Message Handlers                  *
***********************************************************/

//
//    Device Definition Request
//
int SIT_Client::handle_100(int start, int end) {
FUNCTION_START("::handle_100(int start, int end)")

   // do handling of message
   IMSG("Handling 100 in SIT\n");

   // map header (no body for 100)
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);

   // message 100 is Device Definition Request,
   // a PD sends back a 2111
   // a pyro device sends back a 2005
   FASIT_header rhdr;
   FASIT_2111 msg;
   defHeader(2111, &rhdr); // sets the sequence number and other data
   rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2111));
   
   // set response
   msg.response.rnum = htons(100);
   msg.response.rseq = hdr->seq;

   // fill message
   msg.body.devid = getDevID();	// MAC address
   //retrieve the PD_MUZZLE flag from the kernel like Nate described -
   //now that I used a kludge to get those options into the device when
   //the sit_client was constructed during startup
   msg.body.flags = PD_MUZZLE; // TODO -- find actual capabilities from command line
//BDR   fasit_conn  has the command line option -S  that instaniates a
//SIT handler.   It probably has to handle a bunch of possibilitys and
//this part of code has to do the right thing
// Nate said he was writing a MIT_client that handles moving targets
// so that code would be elsewhere
   
   // send
   queueMsg(&rhdr, sizeof(FASIT_header));
   queueMsg(&msg, sizeof(FASIT_2111));
   finishMsg();

   // if the msg.body.flags include PD_MUZZLE then we must also generate and send a 2112, at least I thought so for a
   // while but I can't find that in the spec


   // if it is a Pyro device it responds with a 2005 device ID and
   // capabilities here (instead of or in addition to a 2111????????)

   
FUNCTION_INT("::handle_100(int start, int end)", 0)
   return 0;
}

int SIT_Client::handle_2000(int start, int end) {
FUNCTION_START("::handle_2000(int start, int end)")

   // do handling of message
   IMSG("Handling 2000 in SIT\n");
  
FUNCTION_INT("::handle_2000(int start, int end)", 0)
   return 0;
}

int SIT_Client::handle_2004(int start, int end) {
FUNCTION_START("::handle_2004(int start, int end)")

   // do handling of message
   IMSG("Handling 2004 in SIT\n");

FUNCTION_INT("::handle_2004(int start, int end)", 0)
   return 0;
}

int SIT_Client::handle_2005(int start, int end) {
FUNCTION_START("::handle_2005(int start, int end)")

   // do handling of message
   IMSG("Handling 2005 in SIT\n");

FUNCTION_INT("::handle_2005(int start, int end)", 0)
   return 0;
}


int SIT_Client::handle_2006(int start, int end) {
FUNCTION_START("::handle_2006(int start, int end)")

   // do handling of message
   IMSG("Handling 2006 in SIT\n");

FUNCTION_INT("::handle_2006(int start, int end)", 0)
   return 0;
}

//
//  Event Command
//
int SIT_Client::handle_2100(int start, int end) {
FUNCTION_START("::handle_2100(int start, int end)")

   // do handling of message
   IMSG("Handling 2100 in SIT\n");

   // map header and body for both message and response
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2100 *msg = (FASIT_2100*)(rbuf + start + sizeof(FASIT_header));

   
   // do the event that was requested

   switch (msg->cid) {
      case CID_No_Event:
	 DCMSG(BLUE,"CID_No_Event  send 'S'uccess ack") ; 
	     // send 2101 ack
	     send_2101_ACK(hdr,'S');
		 break;

	  case CID_Reserved01:
		 // send 2101 ack
	     DCMSG(BLUE,"CID_Reserved01  send 'F'ailure ack") ;
	     send_2101_ACK(hdr,'F');

		 break;

	  case CID_Status_Request:
		 // send 2102 status
	     DCMSG(BLUE,"CID_Status_Request   send 2102 status") ; 
		 sendStatus2102();	// sends a 2102   not sure it gets the response number and sequence number right
		 // AND/OR? send 2115 MILES shootback status if supported
		 if (acc_conf.acc_type == ACC_NES_MFS){
		    DCMSG(BLUE,"we also seem to have a MFS Muzzle Flash Simulator - TODO send 2112 status eventually") ; 
		 }
		 // AND/OR? send 2112 Muzzle Flash status if supported	 

		 break;

	  case CID_Expose_Request:
	     DCMSG(BLUE,"CID_Expose_Request  send 'S'uccess ack.   msg->exp=%d",msg->exp) ; 	     
		 // send 2101 ack  (2102's will be generated at start and stop of actuator)
	     send_2101_ACK(hdr,'S');

		 switch (msg->exp) {
			case 0:
			   doConceal();
			   break;
			case 0x2D:
			   break;
			case 0x5A:
			   doExpose();
			   break;
		 }
		 break;

	  case CID_Reset_Device:
		 // send 2101 ack
	     DCMSG(BLUE,"CID_Reset_Device  send 'S'uccess ack.   set lastHitCal.* to defaults") ;
	     send_2101_ACK(hdr,'S');
		 // also supposed to reset all values to the 'initial exercise step value'
		 //  which I am not sure if it is different than ordinary inital values 
		 lastHitCal.seperation = 250;	//250;
		 lastHitCal.sensitivity = 15;
		 lastHitCal.blank_time = 500; // half a second blanking
		 lastHitCal.hits_to_fall = 1; // fall on first hit
		 lastHitCal.after_fall = 0; // 0 for stay down
		 lastHitCal.type = 0; // mechanical sensor
		 lastHitCal.invert = 0; // don't invert sensor input line
		 doHitCal(lastHitCal); // tell kernel
		 doHits(0);	// set hit count to zero
		 break;

	  case CID_Move_Request:
		 // send 2101 ack  (2102's will be generated at start and stop of actuator)
	     DCMSG(BLUE,"CID_Move_Request  send 'S'uccess ack.   TODO send the move to the kernel?") ;	     
	     send_2101_ACK(hdr,'S');
		 break;

	  case CID_Config_Hit_Sensor:
	     DCMSG(BLUE,"CID_Config_Hit_Sensor  send 'S'uccess ack.   TODO add sending a 2102?") ;	     	     
	     send_2101_ACK(hdr,'S');
	     
		 // send 2102 status - after doing what was commanded
		 // which is setting the values in the hit_calibration structure
		 // uses the lastHitCal so what we set is recorded
		 // there are fields that don't match up

	     // TODO I believe a 2100 config hit sensor is supposed to set the hit count
	     if (htons(msg->burst)) lastHitCal.seperation = htons(msg->burst);		// spec says we only set if non-Zero
	     if (htons(msg->sens))  lastHitCal.sensitivity = htons(msg->sens);
//		 lastHitCal.blank_time = 500; // half a second blanking  
	     if (htons(msg->tokill))  lastHitCal.hits_to_fall = htons(msg->tokill); 
	     if (msg->react)  lastHitCal.after_fall = msg->react;	// 0 for stay down
	     if (msg->mode)   lastHitCal.type = msg->mode;			// mechanical sensor
//		 lastHitCal.invert = 0; // don't invert sensor input line
	     doHitCal(lastHitCal); // tell kernel by calling SIT_Clients version of doHitCal
	     DCMSG(BLUE,"calling doHitCal after setting values") ;	     
	     if (htons(msg->hit)) doHits(htons(msg->hit));	// set hit count to something other than zero
	     DCMSG(BLUE,"after doHits(%d) Does that generate a 2102 automagically?",htons(msg->hit)) ;
	 
	     break;

	  case CID_GPS_Location_Request:
	     DCMSG(BLUE,"CID_GPS_Location_Request  send 'F'ailure ack  - because we don't support it") ;
	     send_2101_ACK(hdr,'F');

		 // send 2113 GPS Location
		 break;
   }

   FUNCTION_INT("::handle_2100(int start, int end)", 0)
		 return 0;
}

int SIT_Client::handle_2101(int start, int end) {
   FUNCTION_START("::handle_2101(int start, int end)")

   // do handling of message
		 IMSG("Handling 2101 in SIT\n");

   FUNCTION_INT("::handle_2101(int start, int end)", 0)
		 return 0;
}


int SIT_Client::handle_2102(int start, int end) {
FUNCTION_START("::handle_2102(int start, int end)")

   // do handling of message
   IMSG("Handling 2102 in SIT\n");

FUNCTION_INT("::handle_2102(int start, int end)", 0)
   return 0;
}

//
//  Configure Muzzle Flash
//     We respond with a 2112 to indicate the updated status if we support Muzzle flash,
//     If we don't support Mozzle flash we are to repsond with a negative command acknowledgement
//     (acknowledge response = 'F')   Not sure how to send that right now
//
int SIT_Client::handle_2110(int start, int end) {
FUNCTION_START("::handle_2110(int start, int end)")

   // do handling of message
   IMSG("Handling 2110 in SIT\n");

   // map header and body for both message and response
   FASIT_header rhdr;
   FASIT_2112 rmsg;
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2110 *msg = (FASIT_2110*)(rbuf + start + sizeof(FASIT_header));
   
   // check to see if we have muzzle flash capability -
   if (start_config&PD_MUZZLE){
      
      doMFS(msg->on,msg->mode,msg->idelay,msg->rdelay);
      
   // then respond with a 2112 of it's status
      defHeader(2112, &rhdr); // sets the sequence number and other data
      rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2112));

   // set response
      rmsg.response.rnum = htons(hdr->num);	//  pulls the message number from the header
      rmsg.response.rseq = hdr->seq;		

      // didMFS needs work
//      didMFS(&rmsg.body.on,&rmsg.body.mode,&rmsg.body.idelay,&rmsg.body.rdelay);
      queueMsg(&rhdr, sizeof(FASIT_header));	// send the response
      queueMsg(&rmsg, sizeof(FASIT_2112));
      finishMsg();

   } else {
      send_2101_ACK(hdr,'F');	// no muzzle flash capability, so send a negative ack
   }

FUNCTION_INT("::handle_2110(int start, int end)", 0)
   return 0;
}

int SIT_Client::handle_2111(int start, int end) {
   FUNCTION_START("::handle_2111(int start, int end)")

   // do handling of message
	 IMSG("Handling 2111 in SIT\n");

   FUNCTION_INT("::handle_2111(int start, int end)", 0)
	 return 0;
}


int SIT_Client::handle_2112(int start, int end) {
FUNCTION_START("::handle_2112(int start, int end)")

   // do handling of message
   IMSG("Handling 2112 in SIT\n");

FUNCTION_INT("::handle_2112(int start, int end)", 0)
   return 0;
}

int SIT_Client::handle_2113(int start, int end) {
FUNCTION_START("::handle_2113(int start, int end)")

   // do handling of message
   IMSG("Handling 2113 in SIT\n");

FUNCTION_INT("::handle_2113(int start, int end)", 0)
   return 0;
}

int SIT_Client::handle_2114(int start, int end) {
   FUNCTION_START("::handle_2114(int start, int end)")

   // do handling of message
	 IMSG("Handling 2114 in SIT\n");

   FUNCTION_INT("::handle_2114(int start, int end)", 0)
	 return 0;
}

int SIT_Client::handle_2115(int start, int end) {
   FUNCTION_START("::handle_2115(int start, int end)")

   // do handling of message
	 IMSG("Handling 2115 in SIT\n");

   FUNCTION_INT("::handle_2115(int start, int end)", 0)
	 return 0;
}


/***********************************************************
*                    Basic SIT Commands                    *
***********************************************************/
// experienced failure "type"
void SIT_Client::didFailure(int type) {
FUNCTION_START("::didFailure(int type)")

   FASIT_header hdr;
   FASIT_2102 msg;
   defHeader(2102, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2102));

   // fill message
   fillStatus2102(&msg); // fills in status values with current values

   // set fault
   msg.body.fault = htons(type);

   // send
   queueMsg(&hdr, sizeof(FASIT_header));
   queueMsg(&msg, sizeof(FASIT_2102));
   finishMsg();

FUNCTION_END("::didFailure(int type)")
}

// change position to conceal
void SIT_Client::doConceal() {
FUNCTION_START("::doConceal()")
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doConceal();
   }
FUNCTION_END("::doConceal()")
}

// position changed to conceal
void SIT_Client::didConceal() {
FUNCTION_START("::didConceal()")

   // remember that we are concealed
   exposure = CONCEAL;

   // send status message to FASIT server
   sendStatus2102();

FUNCTION_END("::didConceal()")
}

// change position to expose
void SIT_Client::doExpose() {
FUNCTION_START("::doExpose()")
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doExpose();
   }
FUNCTION_END("::doExpose()")
}

// position changed to expose
void SIT_Client::didExpose() {
FUNCTION_START("::didExpose()")

   // remember that we are exposed
   exposure = EXPOSE;

   // send status message to FASIT server
   sendStatus2102();

FUNCTION_END("::didExpose()")
}

// position changed to moving
void SIT_Client::didMoving() {
FUNCTION_START("::didMoving()")

   // remember that we are moving
   exposure = LIFTING;

   // send status message to FASIT server
   sendStatus2102();

FUNCTION_END("::didMoving()")
}

// retrieve battery value
void SIT_Client::doBattery() {
FUNCTION_START("::doBattery()")
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doBattery();
   }
FUNCTION_END("::doBattery()")
}

// current battery value
void SIT_Client::didBattery(int val) {
FUNCTION_START("::didBattery(int val)")

   // if we're low (and we were low before), we'll need to tell userspace
   if (val <= MIN_BATTERY_VAL && lastBatteryVal <= MIN_BATTERY_VAL) {
      didFailure(ERR_low_battery);
      // TODO -- check FAILURE_BATTERY_VAL and do a shutdown when necessary
      lastBatteryVal = MAX_BATTERY_VAL; // forget last value to prevent immediate resend on next didBattery() call
   }

   // save the information for the next time
   lastBatteryVal = val;

FUNCTION_END("::didBattery(int val)")
}

// immediate stop (stops accessories as well)
void SIT_Client::doStop() {
FUNCTION_START("::doStop()")
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doStop();
   }
FUNCTION_END("::doStop()")
}

// received immediate stop response
void SIT_Client::didStop() {
FUNCTION_START("::didStop()")

   // notify the front-end of the emergency stop
   didFailure(ERR_emergency_stop);

FUNCTION_END("::didStop()")
}

// change hit calibration data
void SIT_Client::doHitCal(struct hit_calibration hit_c) {
FUNCTION_START("::doHitCal(struct hit_calibration hit_c)")
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doHitCal(hit_c);
   }
FUNCTION_END("::doHitCal(struct hit_calibration hit_c)")
}

// current hit calibration data
void SIT_Client::didHitCal(struct hit_calibration hit_c) {
FUNCTION_START("::didHitCal(struct hit_calibration hit_c)")
   // ignore?
FUNCTION_END("::didHitCal(struct hit_calibration hit_c)")
}

// get last remembered hit calibration data
void SIT_Client::getHitCal(struct hit_calibration *hit_c) {
FUNCTION_START("::getHitCal(struct hit_calibration *hit_c)")
   // give the previous hit calibration data
   *hit_c = lastHitCal;
FUNCTION_END("::getHitCal(struct hit_calibration *hit_c)")
}

// get last remembered accessory config
void SIT_Client::getAcc_C(struct accessory_conf *acc_c) {
   FUNCTION_START("::getAcc_C((struct accessory_conf *acc_c)")
   // give the previous accessory config data
	 *acc_c = acc_conf;
   FUNCTION_END("::getAcc_C((struct accessory_conf *acc_c)")
}

// change received hits to "num" (usually to 0 for reset)
void SIT_Client::doHits(int num) {

   DCOLOR(CYAN) ; // change color
FUNCTION_START("SIT_Client::doHits(int num)")
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doHits(num);
   }
   FUNCTION_END("SIT_Client::doHits(int num)") ;
   DCOLOR(black) ; // change color
}

// received "num" hits
void SIT_Client::didHits(int num) {
   DCOLOR(CYAN) ; // change color
FUNCTION_START("SIT_Client::didHits(int num)")


   // are we different than our remember value?
   if (hits != num) {
      // remember new value
      hits = num;

      // if we have a hit count, send it
      if (hits != 0) {
         sendStatus2102(); // send status message to FASIT server
      }
   }

FUNCTION_END("SIT_Client::didHits(int num)")
DCOLOR(black) ; // change color

}

// change MSDH data
void SIT_Client::doMSDH(int code, int ammo, int player, int delay) {
FUNCTION_START("::doMSDH(int code, int ammo, int player, int delay)")
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doMSDH(code, ammo, player, delay);
   }
FUNCTION_END("::doMSDH(int code, int ammo, int player, int delay)")
}

// current MSDH data
void SIT_Client::didMSDH(int code, int ammo, int player, int delay) {
FUNCTION_START("::didMSDH(int code, int ammo, int player, int delay)")

   FASIT_header hdr;
   FASIT_2115 msg;
   defHeader(2115, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2115));

   // fill out response
   msg.response = getResponse(2114); // Configure MILES Shootback Command

   // fill message
   msg.body.code = code & 0xFF;
   msg.body.ammo = ammo & 0xFF;
   msg.body.player = htons(player & 0xFFFF);
   msg.body.delay = delay & 0xFF;

   // send
   queueMsg(&hdr, sizeof(FASIT_header));
   queueMsg(&msg, sizeof(FASIT_2115));
   finishMsg();

FUNCTION_END("::didMSDH(int code, int ammo, int player, int delay)")
}

// change MFS data
void SIT_Client::doMFS(int on, int mode, int idelay, int rdelay) {
FUNCTION_START("::doMFS(int on, int mode, int idelay, int rdelay)")
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doMFS(on, mode, idelay, rdelay);
   }
FUNCTION_END("::doMFS(int on, int mode, int idelay, int rdelay)")
}

// current MFS data
void SIT_Client::didMFS(int *on, int *mode, int *idelay, int *rdelay) {
   FUNCTION_START("::didMFS(int *on, int *mode, int *idelay, int *rdelay)")
	 // there needs to be some actual code here if it is going to function
   FUNCTION_END("::didMFS(int *on, int *mode, int *idelay, int *rdelay)")
}

// retrieve gps data
void SIT_Client::doGPS() {
FUNCTION_START("::doGPS()")
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doGPS();
   }
FUNCTION_END("::doGPS()")
}

// current gps data
void SIT_Client::didGPS(struct gps_conf gpc_c) {
FUNCTION_START("::didGPS(struct gps_conf gpc_c)")
FUNCTION_END("::didGPS(struct gps_conf gpc_c)")
}


/***********************************************************
*                      SIT_Conn Class                      *
***********************************************************/
SIT_Conn::SIT_Conn(struct nl_handle *handle, SIT_Client *client, int family) : NL_Conn(handle, client, family) {
FUNCTION_START("::SIT_Conn(struct nl_handle *handle, TCP_Client *client, int family) : NL_Conn(handle, client, family)")

   sit_client = client;

FUNCTION_END("::SIT_Conn(struct nl_handle *handle, TCP_Client *client, int family) : NL_Conn(handle, client, family)")
}

SIT_Conn::~SIT_Conn() {
FUNCTION_START("::~SIT_Conn()")

FUNCTION_END("::~SIT_Conn()")
}

int SIT_Conn::parseData(struct nl_msg *msg) {
FUNCTION_START("SIT_Conn::parseData(struct nl_msg *msg)")
    struct nlattr *attrs[NL_A_MAX+1];
   struct nlmsghdr *nlh = nlmsg_hdr(msg);
   struct genlmsghdr *ghdr = static_cast<genlmsghdr*>(nlmsg_data(nlh));

   DCMSG(GREEN,"parseData switch on netlink command enum of %d",ghdr->cmd) ;
   // parse message and call individual commands as needed
   switch (ghdr->cmd) {
      case NL_C_FAILURE:
         genlmsg_parse(nlh, 0, attrs, GEN_STRING_A_MAX, generic_string_policy);
	 DCMSG(RED,"parseData case NL_C_FAILURE: attrs = 0x%x",attrs[GEN_STRING_A_MSG]) ;

         // TODO -- failure messages need decodable data
         if (attrs[GEN_STRING_A_MSG]) {
            char *data = nla_get_string(attrs[GEN_STRING_A_MSG]);
            IERROR("netlink failure attribute: %s\n", data)
         }

         break;
      case NL_C_EXPOSE:
         genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);
	 DCMSG(green,"parseData case NL_C_EXPOSE: attrs = 0x%x",attrs[GEN_INT8_A_MSG]) ;
	 
         if (attrs[GEN_INT8_A_MSG]) {
            // received change in exposure
            int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
            switch (value) {
               case 0:
                  sit_client->didConceal(); // tell client
                  break;
               case 1:
                  sit_client->didExpose(); // tell client
                  break;
               default:
                  sit_client->didMoving(); // tell client
                  break;
            }
         }
         break;
      case NL_C_BATTERY:
         genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);
	 DCMSG(BLUE,"parseData case NL_C_BATTERY: attrs = 0x%x",attrs[GEN_INT8_A_MSG]) ;

         if (attrs[GEN_INT8_A_MSG]) {
            // received change in battery value
            int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
            sit_client->didBattery(value); // tell client
         }
         break;
      case NL_C_STOP:
	 DCMSG(magenta,"parseData case NL_C_STOP: ") ;
	 
         // received emergency stop response
         sit_client->didStop(); // tell client
         break;
      case NL_C_HIT_CAL:
	 genlmsg_parse(nlh, 0, attrs, HIT_A_MAX, hit_calibration_policy);
	 DCMSG(CYAN,"parseData case NL_C_HIT_CAL: attrs = 0x%x",attrs[HIT_A_MSG]) ;
	 
         if (attrs[HIT_A_MSG]) {
            // received calibration data
            struct hit_calibration *hit_c = (struct hit_calibration*)nla_data(attrs[HIT_A_MSG]);
            struct hit_calibration lastHitCal; // get calibration data
            sit_client->getHitCal(&lastHitCal);
            if (hit_c != NULL) {
               switch (hit_c->set) {
                  case HIT_OVERWRITE_ALL:
                  case HIT_OVERWRITE_NONE:
                     lastHitCal = *hit_c;
                     break;
                  case HIT_OVERWRITE_CAL:
                  case HIT_GET_CAL:
		     lastHitCal.seperation = hit_c->seperation;
		     DCMSG(BLUE,"setting lasthitcal.seperation to 0x%X",hit_c->seperation) ; 		     
                     lastHitCal.sensitivity = hit_c->sensitivity;
                     lastHitCal.blank_time = hit_c->blank_time;
                     break;
                  case HIT_OVERWRITE_OTHER:
                     lastHitCal.type = hit_c->type;
                     lastHitCal.invert = hit_c->invert;
                     lastHitCal.hits_to_fall = hit_c->hits_to_fall;
                     lastHitCal.after_fall = hit_c->after_fall;
                     break;
                  case HIT_OVERWRITE_TYPE:
                  case HIT_GET_TYPE:
                     lastHitCal.type = hit_c->type;
                     lastHitCal.invert = hit_c->invert;
                     break;
                  case HIT_OVERWRITE_FALL:
                  case HIT_GET_FALL:
                     lastHitCal.hits_to_fall = hit_c->hits_to_fall;
                     lastHitCal.after_fall = hit_c->after_fall;
                     break;
               }
               sit_client->didHitCal(lastHitCal); // tell client
            }
         }
         
         break;
      case NL_C_HITS:
         genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);
         DCMSG(RED,"parseData case NL_C_HITS: attrs = 0x%x ",attrs[GEN_INT8_A_MSG]) ;
         if (attrs[GEN_INT8_A_MSG]) {
            // received hit count
	    int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
	    
	    DCMSG(RED,"parseData got a HIT COUNT of %d",value) ;
            sit_client->didHits(value); // tell client
         }

         break;
      case NL_C_ACCESSORY:
         genlmsg_parse(nlh, 0, attrs, ACC_A_MAX, accessory_conf_policy);
	 DCMSG(MAGENTA,"parseData case NL_C_ACCESSORY: attrs = 0x%x ",attrs[ACC_A_MSG]) ;

         if (attrs[ACC_A_MSG]) {
            // received calibration data
            struct accessory_conf *acc_c = (struct accessory_conf*)nla_data(attrs[ACC_A_MSG]);
            switch (acc_c->acc_type) {
               /* TODO -- fill in support for additional accessories */
               case ACC_NES_MOON_GLOW:
               case ACC_NES_PHI:
               case ACC_SMOKE:
               case ACC_THERMAL:
               case ACC_SES:
                  DCMSG(RED,"Unsupported accesory message: %i\n", acc_c->acc_type)
                  break;
               case ACC_NES_MFS:
		  // MFS on when activate on exposure is set (flash type/burst mode in ex_data1)
// TODO   i thought didMFS was going to return these values 		  
//                  sit_client->didMFS(acc_c->on_exp, acc_c->ex_data1, acc_c->start_delay/2, acc_c->repeat_delay/2); // tell client
                  break;
               case ACC_MILES_SDH:
                  // MILES data : ex_data1 = Player ID, ex_data2 = MILES Code, ex_data3 = Ammo type, start_delay = Fire Delay
                  sit_client->didMSDH(acc_c->ex_data2, acc_c->ex_data3, acc_c->ex_data1, acc_c->start_delay/2);
                  break;
            }
         }
         break;
   }
 
FUNCTION_INT("SIT_Conn::parseData(struct nl_msg *msg)", 0)
   return 0;
}

/***********************************************************
*                   SIT Netlink Commands                   *
***********************************************************/
// change position to conceal
void SIT_Conn::doConceal() {
FUNCTION_START("::doConceal()")

   // Queue command
   queueMsgU8(NL_C_EXPOSE, CONCEAL); // conceal command

FUNCTION_END("::doConceal()")
}

// change position to expose
void SIT_Conn::doExpose() {
FUNCTION_START("::doExpose()")

   // Queue command
   queueMsgU8(NL_C_EXPOSE, EXPOSE); // expose command

FUNCTION_END("::doExpose()")
}

// get the current position
void SIT_Conn::doMoving() {
FUNCTION_START("::doMoving()")

   // Queue command
   queueMsgU8(NL_C_EXPOSE, EXPOSURE_REQ); // exposure status request

FUNCTION_END("::doMoving()")
}

// retrieve battery value
void SIT_Conn::doBattery() {
FUNCTION_START("::doBattery()")

   // Queue command
   queueMsgU8(NL_C_BATTERY, 1); // battery status request

FUNCTION_END("::doBattery()")
}

// immediate stop (stops accessories as well)
void SIT_Conn::doStop() {
FUNCTION_START("::doStop()")

   // Queue command
   queueMsgU8(NL_C_STOP, 1); // emergency stop command

FUNCTION_END("::doStop()")
}

// change hit calibration data
void SIT_Conn::doHitCal(struct hit_calibration hit_c) {
FUNCTION_START("::doHitCal(struct hit_calibration hit_c)")

   // Queue command
   queueMsg(NL_C_HIT_CAL, HIT_A_MSG, sizeof(struct hit_calibration), &hit_c); // pass structure without changing

FUNCTION_END("::doHitCal(struct hit_calibration hit_c)")
}

// change received hits to "num" (usually to 0 for reset)
void SIT_Conn::doHits(int num) {
FUNCTION_START("SIT_Conn::doHits(int num)")

    // reset or retrieve hit count
    if (num == -1) {
        queueMsgU8(NL_C_HITS, HIT_REQ); // request hit count
    } else {
        queueMsgU8(NL_C_HITS, 0); // reset to 0 (ignore num's value; we always want zero here)
    }

FUNCTION_END("SIT_Conn::doHits(int num)")
}

// change MSDH data
void SIT_Conn::doMSDH(int code, int ammo, int player, int delay) {
FUNCTION_START("::doMSDH(int code, int ammo, int player, int delay)")

   // Create attribute
   struct accessory_conf acc_c;
   memset(&acc_c, 0, sizeof(struct accessory_conf)); // start zeroed out
   acc_c.acc_type = ACC_MILES_SDH;
   acc_c.on_exp = 1; // turn on when fully exposed
   acc_c.ex_data2 = code;
   acc_c.ex_data3 = ammo;
   acc_c.ex_data1 = player;
   acc_c.start_delay = 2 * delay;

   // Queue command
   queueMsg(NL_C_ACCESSORY, ACC_A_MSG, sizeof(struct accessory_conf), &acc_c); // MSDH is an accessory

FUNCTION_END("::doMSDH(int code, int ammo, int player, int delay)")
}

// change MFS data
void SIT_Conn::doMFS(int on, int mode, int idelay, int rdelay) {
FUNCTION_START("::doMFS(int on, int mode, int idelay, int rdelay)")

struct accessory_conf acc_c;

   // Create attribute
   memset(&acc_c, 0, sizeof(struct accessory_conf)); // start zeroed out\
   sit_client->getAccC(&acc_c);

   acc_c.acc_type = ACC_NES_MFS;
   acc_c.on_exp = on;
   if (mode == 1) {
      acc_c.ex_data1 = 1; // do burst
      acc_c.ex_data2 = 5; // burst 5 times
      acc_c.on_time = 50; // on 50 milliseconds
      acc_c.off_time = 100; // off 100 milliseconds
      acc_c.repeat_delay = 2 * rdelay; // when burst, burst every rdelay*2 half-seconds
      acc_c.repeat = 63; // infinite repeat
   } else {
      acc_c.on_time = 75; // on 75 milliseconds
   }
   acc_c.start_delay = 2 * idelay; // start after idelay*2 half-seconds
   
   // Queue command
   queueMsg(NL_C_ACCESSORY, ACC_A_MSG, sizeof(struct accessory_conf), &acc_c); // MFS is an accessory

   
FUNCTION_END("::doMFS(int on, int mode, int idelay, int rdelay)")
}

// retrieve gps dataprotected:
void SIT_Conn::doGPS() {
FUNCTION_START("::doGPS()")

   // Create attribute
   struct gps_conf gps_c;
   memset(&gps_c, 0, sizeof(struct gps_conf)); // for request, everything is zeroed out

   // Queue command
   queueMsg(NL_C_GPS, GPS_A_MSG, sizeof(struct gps_conf), &gps_c);

FUNCTION_END("::doGPS()")
}


