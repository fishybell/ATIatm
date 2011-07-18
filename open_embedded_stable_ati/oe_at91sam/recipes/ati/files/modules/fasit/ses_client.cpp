#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

using namespace std;

#include "ses_procs.h"
#include "ses_client.h"
#include "common.h"
#include "timers.h"

#include "target_ses_interface.h"


/***********************************************************
*                     SES_Client Class                     *
***********************************************************/
SES_Client::SES_Client(int fd, int tnum) : TCP_Client(fd, tnum) {
FUNCTION_START("::SES_Client(int fd, int tnum) : Connection(fd)")
   // connect our netlink connection
   nl_conn = NL_Conn::newConn<SES_Conn>(this);
   if (nl_conn == NULL) {
      deleteLater();
   } else {
      // initialize default settings
      loop = NO_LOOP; // no looping
      knob = 0; // unknown knob value, will retrieve later
      didMode(MODE_MAINTENANCE); // defalt mode
      strncpy(track, "/media/sda1/audio/builtin/0.mp3", SES_BUFFER_SIZE); // first built-in track
      memset(uri, 0, SES_BUFFER_SIZE); // no default stream uri
      lastBatteryVal = MAX_BATTERY_VAL;
      nl_conn->doBattery(); // get a correct battery value soon
      nl_conn->doMode(); // get correct mode value
      nl_conn->doTrack(); // get correct track/knob value
   }
FUNCTION_END("::SES_Client(int fd, int tnum) : Connection(fd)")
}

SES_Client::~SES_Client() {
FUNCTION_START("::~SES_Client()")

FUNCTION_END("::~SES_Client()")
}

// fill out 2102 status message
void SES_Client::fillStatus2102(FASIT_2102 *msg) {
FUNCTION_START("::fillStatus2102(FASIT_2102 *msg)")

   // start with zeroes
   memset(msg, 0, sizeof(FASIT_2102));

   // fill out as response
   msg->response.rnum = resp_num;
   msg->response.rseq = resp_seq;
   resp_num = resp_seq = 0; // the next one will be unsolicited

   // device type
   msg->body.type = 200; // SES

   // TODO -- subvert values for own demented purposes?

FUNCTION_END("::fillStatus2102(FASIT_2102 *msg)")
}


// create and send a status messsage to the FASIT server
void SES_Client::sendStatus2102() {
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

// fill out 14401 status message
void SES_Client::fillStatus14401(FASIT_14401 *msg) {
FUNCTION_START("::fillStatus14401(FASIT_14401 *msg)")

   // start with zeroes
   memset(msg, 0, sizeof(FASIT_14401));

   // fill out as response
   msg->response.rnum = resp_num;
   msg->response.rseq = resp_seq;
   resp_num = resp_seq = 0; // the next one will be unsolicited

   // fill values
   msg->body.mode = mode;
   switch (mode) {
      case MODE_MAINTENANCE:
      case MODE_TESTING:
      case MODE_LIVEFIRE:
         // playback, either streaming or playing
         if (StreamProcess::isStreaming()) {
            // streaming
            msg->body.status = 4; // streaming
         } else {
            // TODO -- are we currently playing?
            // TODO -- were we stopped?
            msg->body.status = 3; // play ready
         }
         break;
      case MODE_REC_START:
      case MODE_ENC_START:
      case MODE_REC_DONE:
      case MODE_RECORD:
         // recording, encoding, or ready to record
         if (RecordProcess::isRecording()) {
            msg->body.status = 5; // recording
         } else if (EncodeProcess::isEncoding()) {
            msg->body.status = 6; // streaming
         } else {
            msg->body.status = 7; // ready to record
         }
         break;
   }
   msg->body.track = knob;

FUNCTION_END("::fillStatus14401(FASIT_14401 *msg)")
}


// create and send a status messsage to the FASIT server
void SES_Client::sendStatus14401() {
FUNCTION_START("::sendStatus14401()")

   FASIT_header hdr;
   FASIT_14401 msg;
   defHeader(14401, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_14401));

   // fill message
   fillStatus14401(&msg); // fills in status values with current values

   // send
   queueMsg(&hdr, sizeof(FASIT_header));
   queueMsg(&msg, sizeof(FASIT_14401));
   finishMsg();

FUNCTION_END("::sendStatus14401()")
}

/***********************************************************
*                  FASIT Message Handlers                  *
***********************************************************/

//
//    Device Definition Request
//
int SES_Client::handle_100(int start, int end) {
FUNCTION_START("::handle_100(int start, int end)")

   // do handling of message
   IMSG("Handling 100 in SES\n");

   // map header (no body for 100)
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   DCMSG(RED,"************************************** Report Device Capabilities *****************************************************************************************************");
   DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htons(hdr->seq),htons(hdr->rsrvd),htons(hdr->length));
   
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
   msg.body.flags = 0;

   DCMSG(BLUE,"Prepared to send 2111 device capabilites message:");
   DCMSG(BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n"\
	               "%4d    %d.%d     %5d    %3d     %3d",htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htons(rhdr.seq),htons(rhdr.rsrvd),htons(rhdr.length));
   DCMSG(BLUE,"\t\t\t\t\t\t\tmessage body\n Device ID (mac address backwards) | flag_bits == GPS=4,Muzzle Flash=2,MILES Shootback=1\n0x%8.8llx           0x%2x",msg.body.devid,msg.body.flags);

   // send
   queueMsg(&rhdr, sizeof(FASIT_header));
   queueMsg(&msg, sizeof(FASIT_2111));
   finishMsg();

FUNCTION_INT("::handle_100(int start, int end)", 0);
   return 0;
}

int SES_Client::handle_2000(int start, int end) {
FUNCTION_START("::handle_2000(int start, int end)");

   // do handling of message
   IMSG("Handling 2000 in SES\n");
  
FUNCTION_INT("::handle_2000(int start, int end)", 0);
   return 0;
}

int SES_Client::handle_2004(int start, int end) {
FUNCTION_START("::handle_2004(int start, int end)")

   // do handling of message
   IMSG("Handling 2004 in SES\n");

FUNCTION_INT("::handle_2004(int start, int end)", 0)
   return 0;
}

int SES_Client::handle_2005(int start, int end) {
FUNCTION_START("::handle_2005(int start, int end)")

   // do handling of message
   IMSG("Handling 2005 in SES\n");

FUNCTION_INT("::handle_2005(int start, int end)", 0)
   return 0;
}


int SES_Client::handle_2006(int start, int end) {
FUNCTION_START("::handle_2006(int start, int end)")

   // do handling of message
   IMSG("Handling 2006 in SES\n");

FUNCTION_INT("::handle_2006(int start, int end)", 0)
   return 0;
}

//
//  Event Command
//
int SES_Client::handle_2100(int start, int end) {
FUNCTION_START("::handle_2100(int start, int end)");

   // map header and body for both message and response
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2100 *msg = (FASIT_2100*)(rbuf + start + sizeof(FASIT_header));


   // save response numbers
   resp_num = hdr->num;	//  pulls the message number from the header  (htons was wrong here)
   resp_seq = hdr->seq;

   // Just parse out the command for now and print a pretty message
   switch (msg->cid) {
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
	 DCMSG(RED,"CID_Expose_Request:  msg->exp=%d",msg->exp) ;
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

   DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htons(hdr->seq),htons(hdr->rsrvd),htons(hdr->length));
   DCMSG(RED,"\t\t\t\t\t\t\tmessage body\n"\
	 "C-ID | Expos | Aspct |  Dir | Move |  Speed | On/Off | Hits | React | ToKill | Sens | Mode | Burst\n"\
	 "%3d    %3d     %3d     %2d    %3d    %7.2f     %4d     %2d     %3d     %3d     %3d    %3d   %5d ",
	 msg->cid,msg->exp,msg->asp,msg->dir,msg->move,msg->speed,msg->on,htons(msg->hit),msg->react,htons(msg->tokill),htons(msg->sens),msg->mode,htons(msg->burst));
   
   // do the event that was requested

   switch (msg->cid) {
      case CID_No_Event:
	 DCMSG(RED,"CID_No_Event  send 'S'uccess ack") ; 
	     // send 2101 ack
	     send_2101_ACK(hdr,'S');
		 break;

	  case CID_Reserved01:
		 // send 2101 ack
	     DCMSG(RED,"CID_Reserved01  send 'F'ailure ack") ;
	     send_2101_ACK(hdr,'F');

		 break;

	  case CID_Status_Request:
		 // send 2102 status
	     DCMSG(RED,"CID_Status_Request   send 2102 status") ; 
		 sendStatus2102();	// forces sending of a 2102
		 break;

	  case CID_Reset_Device:
		 // send 2101 ack
	     DCMSG(RED,"CID_Reset_Device  send 'S'uccess ack.   set lastHitCal.* to defaults") ;
	     send_2101_ACK(hdr,'S');

         // stop playback
         doStopPlay();

         // reset various values back to start
         loop = NO_LOOP; // no looping
         didMode(MODE_MAINTENANCE); // defalt mode
         strncpy(track, "/media/sda1/audio/builtin/0.mp3", SES_BUFFER_SIZE); // first built-in track
         nl_conn->doTrack(); // get correct track value
         memset(uri, 0, SES_BUFFER_SIZE); // no default stream uri
		 break;

	  case CID_Shutdown:
	     DCMSG(RED,"CID_Shutdown...shutting down") ; 
	     doShutdown();
		 break;
	  case CID_Sleep:
	     DCMSG(RED,"CID_Sleep...sleeping") ; 
	     doSleep();
		 break;
	  case CID_Wake:
	     DCMSG(RED,"CID_Wake...waking") ; 
	     doWake();
		 break;
   }

   FUNCTION_INT("::handle_2100(int start, int end)", 0)
		 return 0;
}

int SES_Client::handle_2101(int start, int end) {
   FUNCTION_START("::handle_2101(int start, int end)")

   // do handling of message
		 IMSG("Handling 2101 in SES\n");

   FUNCTION_INT("::handle_2101(int start, int end)", 0)
		 return 0;
}


int SES_Client::handle_2102(int start, int end) {
FUNCTION_START("::handle_2102(int start, int end)")

   // do handling of message
   IMSG("Handling 2102 in SES\n");

FUNCTION_INT("::handle_2102(int start, int end)", 0)
   return 0;
}

int SES_Client::handle_2110(int start, int end) {
FUNCTION_START("::handle_2110(int start, int end)")

   // do handling of message
   IMSG("Handling 2110 in SES\n");

FUNCTION_INT("::handle_2110(int start, int end)", 0)
   return 0;
}

int SES_Client::handle_2111(int start, int end) {
   FUNCTION_START("::handle_2111(int start, int end)")

   // do handling of message
	 IMSG("Handling 2111 in SES\n");

   FUNCTION_INT("::handle_2111(int start, int end)", 0)
	 return 0;
}


int SES_Client::handle_2112(int start, int end) {
FUNCTION_START("::handle_2112(int start, int end)")

   // do handling of message
   IMSG("Handling 2112 in SES\n");

FUNCTION_INT("::handle_2112(int start, int end)", 0)
   return 0;
}

int SES_Client::handle_2113(int start, int end) {
FUNCTION_START("::handle_2113(int start, int end)")

   // do handling of message
   IMSG("Handling 2113 in SES\n");

FUNCTION_INT("::handle_2113(int start, int end)", 0)
   return 0;
}

int SES_Client::handle_2114(int start, int end) {
   FUNCTION_START("::handle_2114(int start, int end)")

   // do handling of message
	 IMSG("Handling 2114 in SES\n");

   FUNCTION_INT("::handle_2114(int start, int end)", 0)
	 return 0;
}

int SES_Client::handle_2115(int start, int end) {
   FUNCTION_START("::handle_2115(int start, int end)")

   // do handling of message
	 IMSG("Handling 2115 in SES\n");

   FUNCTION_INT("::handle_2115(int start, int end)", 0)
	 return 0;
}

int SES_Client::handle_13110(int start, int end) {
	FUNCTION_START("::handle_13110(int start, int end)")

	// do handling of message
			IMSG("Handling 13110 in SES\n");

	FUNCTION_INT("::handle_13110(int start, int end)", 0)
			return 0;
}

int SES_Client::handle_13112(int start, int end) {
	FUNCTION_START("::handle_13112(int start, int end)")

	// do handling of message
			IMSG("Handling 13112 in SES\n");

	FUNCTION_INT("::handle_13112(int start, int end)", 0)
			return 0;
}

int SES_Client::handle_14110(int start, int end) {
	FUNCTION_START("::handle_14110(int start, int end)")

	// do handling of message
			IMSG("Handling 14110 in SES\n");

	FUNCTION_INT("::handle_14110(int start, int end)", 0)
			return 0;
}

int SES_Client::handle_14112(int start, int end) {
	FUNCTION_START("::handle_14112(int start, int end)")

	// do handling of message
			IMSG("Handling 14112 in SES\n");

	FUNCTION_INT("::handle_14112(int start, int end)", 0)
			return 0;
}

int SES_Client::handle_14200(int start, int end) {
	FUNCTION_START("::handle_14200(int start, int end)")

	// do handling of message
			IMSG("Handling 14200 in SES\n");

	FUNCTION_INT("::handle_14200(int start, int end)", 0)
			return 0;
}

int SES_Client::handle_14400(int start, int end) {
FUNCTION_START("::handle_14400(int start, int end)")

   // do handling of message
   IMSG("Handling 14400 in SES\n");

   // map header and body for both message and response
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_14400 *msg = (FASIT_14400*)(rbuf + start + sizeof(FASIT_header));


   // save response numbers
   resp_num = hdr->num;	//  pulls the message number from the header  (htons was wrong here)
   resp_seq = hdr->seq;

   // print pretty outpu
   DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htons(hdr->seq),htons(hdr->rsrvd),htons(hdr->length));
   DCMSG(RED,"\t\t\t\t\t\t\tmessage body\n"\
	 "C-ID | Length | Data\n"\
	  "%3d    %5d   %s",
	 msg->cid,htons(msg->length), msg->data);
   
   
   // do the event that was requested
   switch (msg->cid) {
      case SES_No_Event:
         // don't do anything, return
         FUNCTION_INT("::handle_14400(int start, int end)", 0)
         return 0;
         break;
      case SES_Request_Status:
         // don't do anything, break out and send send status
         break;
      case SES_Play_Track:
         doTrack((const char*)msg->data, htons(msg->length)); // set track
         doPlay(); // start playing
         break;
      case SES_Record_Track:
         doTrack((const char*)msg->data, htons(msg->length)); // set track
         doMode(MODE_RECORD); // notify kernel we're changing mode
         didMode(MODE_RECORD); // change mode
         doRecord(); // start recording
         break;
      case SES_Play_Stream:
         doStream((const char*)msg->data, htons(msg->length)); // stream uri
         break;
      case SES_Stop_Playback:
         doStopPlay(); // stop everything
         break;
      case SES_Encode_Recording:
         doEncode();
         break;
      case SES_Abort_Recording:
         doRecAbort();
         break;
      case SES_Copy_Start:
         // TODO -- impliment track copying
         break;
      case SES_Copy_Chunk:
         // TODO -- impliment track copying
         break;
      case SES_Copy_Abort:
         // TODO -- impliment track copying
         break;
      case SES_Maint_Volume:
         didMode(MODE_MAINTENANCE); // change volume
         doMode(MODE_MAINTENANCE); // tell kernel
         break;
      case SES_Test_Volume:
         didMode(MODE_TESTING); // change volume
         doMode(MODE_TESTING); // tell kernel
         break;
      case SES_Livefire_Volume:
         didMode(MODE_LIVEFIRE); // change volume
         doMode(MODE_LIVEFIRE); // tell kernel
         break;
      case SES_Loop:
         DCMSG(BLUE, "Looping: %s", msg->data);
         if (strncmp((const char*)msg->data, "infinite", 8) == 0) {
            doLoop(INFINITE_LOOP);
         } else {
            char *endptr;
            long lloop = strtol((const char*)msg->data, &endptr, 10);
            if (msg->data[0] != '\0' && *endptr == '\0') {
               // parsed only a number
               doLoop(lloop);
            }
         }
         break;
   }

   // send back status
   sendStatus14401();

FUNCTION_INT("::handle_14400(int start, int end)", 0)
   return 0;
}

int SES_Client::handle_14401(int start, int end) {
FUNCTION_START("::handle_14401(int start, int end)")

   // do handling of message
   IMSG("Handling 14401 in SES\n");

FUNCTION_INT("::handle_14401(int start, int end)", 0)
   return 0;
}


/***********************************************************
*                    Basic SES Commands                    *
***********************************************************/
// experienced failure "type"
void SES_Client::didFailure(int type) {
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

// shutdown device
void SES_Client::doShutdown() {
FUNCTION_START("::doShutdown()");
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doShutdown();
   }
FUNCTION_END("::doShutdown()");
}

// sleep device
void SES_Client::doSleep() {
FUNCTION_START("::doSleep()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doSleep();
   }
FUNCTION_END("::doSleep()")
}

// wake device
void SES_Client::doWake() {
FUNCTION_START("::doWake()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doWake();
   }
FUNCTION_END("::doWake()")
}

// retrieve battery value
void SES_Client::doBattery() {
FUNCTION_START("::doBattery()");
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doBattery();
   }
FUNCTION_END("::doBattery()");
}

// current battery value
void SES_Client::didBattery(int val) {
FUNCTION_START("::didBattery(int val)");

   // if we're low we'll need to tell userspace
   if (val <= FAILURE_BATTERY_VAL) {
      didFailure(ERR_critical_battery);
   } else if (val <= MIN_BATTERY_VAL) {
      didFailure(ERR_low_battery);
   }

   // save the information for the next time
   lastBatteryVal = val;

FUNCTION_END("::didBattery(int val)");
}

// immediate stop (stops accessories as well)
void SES_Client::doStop() {
FUNCTION_START("::doStop()");
   // pass directly to kernel for actual action
   if (hasPair()) {
      nl_conn->doStop();
   }
FUNCTION_END("::doStop()");
}

// received immediate stop response
void SES_Client::didStop() {
FUNCTION_START("::didStop()");

   // notify the front-end of the emergency stop
   didFailure(ERR_emergency_stop);

FUNCTION_END("::didStop()");
}

void SES_Client::doPlayRecord() {
FUNCTION_START("::doPlayRecord()");

   // play or record based on mode
   if (mode == MODE_RECORD) {
      doRecordButton();
   } else {
      doPlay();
   }

FUNCTION_END("::doPlayRecord()");
}

void SES_Client::doPlay() {
FUNCTION_START("::doPlay()");

   DCMSG(GREEN, "Playing %s", track) ;

   // start play process
   if (!StreamProcess::isStreaming()) {
      PlayProcess::playTrack(track, loop);
   }

FUNCTION_END("::doPlay()");
}

void SES_Client::doRecordButton() {
FUNCTION_START("::doRecordButton()");

   DCMSG(GREEN, "Recording %s", track) ;

   // start record process if not doing any encoding right now
   if (!EncodeProcess::isEncoding()) {
      if (!RecordProcess::isRecording()) {
         // first button press is record
         doRecord();
      } else {
         // second button press is encode
         doEncode();
      }
   } else {
      // third button press (not usually done) is abort
      doRecAbort();
   }

FUNCTION_END("::doRecordButton()");
}

void SES_Client::doRecord() {
FUNCTION_START("::doRecord()");

   // not recording or encoding, start recording
   if (!EncodeProcess::isEncoding() && !RecordProcess::isRecording()) {
      RecordProcess::recordTrack(track, this); // pass "this" along so it knows who to notify when it's done encoding
      doMode(MODE_REC_START); // notify kernel we're recording
   }

FUNCTION_END("::doRecord()");
}

void SES_Client::doEncode() {
FUNCTION_START("::doEncode()");

   // stop recording, start encoding
   if (!EncodeProcess::isEncoding()) {
      RecordProcess::StartEncoding();
      doMode(MODE_ENC_START); // notify kernel we're encoding
   }

FUNCTION_END("::doEncode()");
}

void SES_Client::doRecAbort() {
FUNCTION_START("::doRecAbort()");

   // abort recording/encoding
   RecordProcess::StopRecording();
   EncodeProcess::StopEncoding();
   if (mode == MODE_RECORD || mode == MODE_REC_START || mode == MODE_REC_DONE || mode == MODE_REC_START) {
      doMode(MODE_REC_DONE); // notify kernel we're done recording
   }

FUNCTION_END("::doRecAbort()");
}

void SES_Client::didMode(int mode) {
FUNCTION_START("::didMode(int mode)");

   // set member mode to mode
   this->mode = mode;

   // start volume process -- TODO -- make these selectable on startup (preferably in eeprom)
   switch (mode) {
      case MODE_MAINTENANCE:
         BackgroundProcess::newProc("amixer set 'Speaker',0 8"); // 5% of 151
         break;
      case MODE_TESTING:
         BackgroundProcess::newProc("amixer set 'Speaker',0 53"); // 35% of 151
         break;
      case MODE_LIVEFIRE:
         BackgroundProcess::newProc("amixer set 'Speaker',0 151"); // 100% of 151
         break;
      case MODE_RECORD:
         BackgroundProcess::newProc("amixer set 'Mic',0 32"); // 100% of 32
         break;
   }

FUNCTION_END("::didMode(int mode)");
}

void SES_Client::doLoop(unsigned int loop) {
FUNCTION_START("::doLoop(int loop)");

   // set member loop to loop
   this->loop = loop;

FUNCTION_END("::doLoop(int loop)");
}

void SES_Client::doTrack(const char* track, int length) {
FUNCTION_START("::doTrack(const char* track, int length)");

   // fix length
   if (length > SES_BUFFER_SIZE-1) {
      length = SES_BUFFER_SIZE-1;
   }

   // check track see if it's a full path
   if (track[0] == '/') {
      // copy contents of track to member track
      strncpy(this->track, track, length);
   } else if (strncmp("builtin/", track, 8) == 0) { // check to see if it's built-in
      // add rest of path
      snprintf(this->track, 19+length, "/media/sda1/audio/%s", track);
   } else if (strncmp("user/", track, 5) == 0) { // check to see if it's user created
      // add rest of path
      snprintf(this->track, 19+length, "/media/sda1/audio/%s", track);
   } else { // assume user track
      // add rest of path
      snprintf(this->track, 24+length, "/media/sda1/audio/user/%s", track);
   }

FUNCTION_END("::doTrack(const char* track, int length)");
}

void SES_Client::doTrack(int track) {
FUNCTION_START("::doTrack(int track)");

   // check validity of track number
   if (track < 0 || track > KNOB_MAX) {
FUNCTION_END("::doTrack(int track)");
      return;
   }

   // remember track knob number
   knob = track;

   // create string for track number
   char builtin[16];
   snprintf(builtin, 16, "builtin/%i.mp3", track);

   // set the track using the string
   doTrack(builtin, 16);

FUNCTION_END("::doTrack(int track)");
}

void SES_Client::doStream(const char* uri, int length) {
FUNCTION_START("::doStream(const char* uri, int length)");

   // fix length
   if (length > SES_BUFFER_SIZE-1) {
      length = SES_BUFFER_SIZE-1;
   }

   // start streaming process
   if (!StreamProcess::isStreaming()) { // nothing streaming?
      // remember new stream uri
      strncpy(this->uri, uri, length);

      // stop track playback and recording
      doRecAbort();

      // play stream
      StreamProcess::streamURI(uri);
   } else if (strncmp(this->uri, uri, length) != 0) { // do we have a new uri?
      // remember new stream uri
      strncpy(this->uri, uri, length);

      // change stream
      StreamProcess::changeURI(uri);
   } // otherwise, just continue playing

FUNCTION_END("::doStream(const char* uri, int length)");
}

void SES_Client::doStopPlay() {
FUNCTION_START("::doStopPlay()");

   DCMSG(RED, "Stopping playback, recording, and streaming");

   // stop play process
   PlayProcess::StopPlayback();

   // stop record process
   RecordProcess::StopRecording();
   if (mode == MODE_RECORD || mode == MODE_REC_START || mode == MODE_REC_DONE || mode == MODE_REC_START) {
      doMode(MODE_REC_DONE); // notify kernel we're done recording
   }

   // stop encoding process
   EncodeProcess::StopEncoding();

   // stop stream process
   StreamProcess::StopStreaming();

FUNCTION_END("::doStopPlay()");
}

void SES_Client::doMode() {
FUNCTION_START("::doMode()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doMode();
   }
FUNCTION_END("::doMode()")
}

void SES_Client::doTrack() {
FUNCTION_START("::doTrack()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doTrack();
   }
FUNCTION_END("::doTrack()")
}

void SES_Client::doMode(int mode) {
FUNCTION_START("::doMode(int mode)")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doMode(mode);
   }
FUNCTION_END("::doMode(int mode)")
}

/***********************************************************
*                      SES_Conn Class                      *
***********************************************************/
SES_Conn::SES_Conn(struct nl_handle *handle, SES_Client *client, int family) : NL_Conn(handle, client, family) {
FUNCTION_START("::SES_Conn(struct nl_handle *handle, TCP_Client *client, int family) : NL_Conn(handle, client, family)")

   ses_client = client;

FUNCTION_END("::SES_Conn(struct nl_handle *handle, TCP_Client *client, int family) : NL_Conn(handle, client, family)")
}

SES_Conn::~SES_Conn() {
FUNCTION_START("::~SES_Conn()")

FUNCTION_END("::~SES_Conn()")
}

int SES_Conn::parseData(struct nl_msg *msg) {
FUNCTION_START("SES_Conn::parseData(struct nl_msg *msg)")
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
      case NL_C_BATTERY:
         genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);
	 DCMSG(BLUE,"parseData case NL_C_BATTERY: attrs = 0x%x",attrs[GEN_INT8_A_MSG]) ;

         if (attrs[GEN_INT8_A_MSG]) {
            // received change in battery value
            int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
            ses_client->didBattery(value); // tell client
         }
         break;
      case NL_C_STOP:
	 DCMSG(magenta,"parseData case NL_C_STOP: ") ;
	 
         // received emergency stop response
         ses_client->didStop(); // tell client
         break;
      case NL_C_BIT:
         genlmsg_parse(nlh, 0, attrs, BIT_A_MAX, bit_event_policy);
         DCMSG(RED,"parseData case NL_C_EVENT: attrs = 0x%x ",attrs[BIT_A_MSG]) ;
         if (attrs[BIT_A_MSG]) {
            // received BIT from the kernel
            bit_event *bit_c = (struct bit_event*)nla_data(attrs[BIT_A_MSG]);
            switch (bit_c->bit_type) {
               case BIT_TEST:
                  // play or record selected track
                  if (bit_c->is_on) { // only play when button is pressed, not released
                     ses_client->doPlayRecord();
                  }
                  break;
               case BIT_KNOB:
                  // change selected track
                  ses_client->doTrack(bit_c->is_on);
                  // ses_client->sendStatus14401(); -- ignored anyway, don't send
                  break;
               case BIT_MODE:
                  // change mode
                  if (bit_c->is_on == MODE_STOP) {
                     ses_client->doStopPlay();
                  } else {
                     ses_client->didMode(bit_c->is_on);
                  }
                  ses_client->sendStatus14401();
                  break;
            }
         }
	 
         break;
   }
 
FUNCTION_INT("SES_Conn::parseData(struct nl_msg *msg)", 0)
   return 0;
}

void SES_Client::finishedRecording() {
FUNCTION_START("::finishedRecording()")

   doMode(MODE_REC_DONE); // notify kernel
   didMode(MODE_REC_DONE); // remember new mode (somewhat temporarily)
   sendStatus14401(); // notify server

FUNCTION_END("::finishedRecording()")
}

/***********************************************************
*                   SES Netlink Commands                   *
***********************************************************/
// shutdown device
void SES_Conn::doShutdown() {
FUNCTION_START("::doShutdown()")

   // Queue command
   queueMsgU8(NL_C_BATTERY, BATTERY_SHUTDOWN); // shutdown command

FUNCTION_END("::doShutdown()")
}

// sleep device
void SES_Conn::doSleep() {
FUNCTION_START("::doSleep()")

   // Queue command
   queueMsgU8(NL_C_SLEEP, SLEEP_COMMAND); // sleep command

FUNCTION_END("::doSleep()")
}

// wake device
void SES_Conn::doWake() {
FUNCTION_START("::doWake()")

   // Queue command
   queueMsgU8(NL_C_SLEEP, WAKE_COMMAND); // wake command

FUNCTION_END("::doWake()")
}

// retrieve battery value
void SES_Conn::doBattery() {
FUNCTION_START("::doBattery()")

   // Queue command
   queueMsgU8(NL_C_BATTERY, BATTERY_REQUEST); // battery status request

FUNCTION_END("::doBattery()")
}

// immediate stop (stops accessories as well)
void SES_Conn::doStop() {
FUNCTION_START("::doStop()")

   // Queue command
   queueMsgU8(NL_C_STOP, 1); // emergency stop command

FUNCTION_END("::doStop()")
}

// retrieves correct mode value
void SES_Conn::doMode() {
FUNCTION_START("::doMode()")

   // build request
   struct bit_event bit_c;
   bit_c.bit_type = BIT_MODE_REQ;
   bit_c.is_on = 0;

   // queue
   queueMsg(NL_C_BIT, BIT_A_MSG, sizeof(struct bit_event), &bit_c);

FUNCTION_END("::doMode()")
}

// sets kernel mode value
void SES_Conn::doMode(int mode) {
FUNCTION_START("::doMode(int mode)")

   // build command
   struct bit_event bit_c;
   bit_c.bit_type = BIT_MODE;
   bit_c.is_on = mode;

   // queue
   queueMsg(NL_C_BIT, BIT_A_MSG, sizeof(struct bit_event), &bit_c);

FUNCTION_END("::doMode(int mode)")
}

// retrieves correct track (knob) value
void SES_Conn::doTrack() {
FUNCTION_START("::doTrack()")

   // build request
   struct bit_event bit_c;
   bit_c.bit_type = BIT_KNOB_REQ;
   bit_c.is_on = 0;

   // queue
   queueMsg(NL_C_BIT, BIT_A_MSG, sizeof(struct bit_event), &bit_c);

FUNCTION_END("::doTrack()")
}

