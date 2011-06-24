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
      loop = 0; // no looping
      mode = MODE_MAINTENANCE; // defalt mode
      strncpy(track, "/media/sda1/audio/builtin/1.mp3", SES_BUFFER_SIZE); // first built-in track
      memset(uri, 0, SES_BUFFER_SIZE); // no default stream uri
      lastBatteryVal = MAX_BATTERY_VAL;
      nl_conn->doBattery(); // get a correct battery value soon
      nl_conn->doMode(); // get correct mode value
      nl_conn->doTrack(); // get correct track value
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
         loop = 0; // no looping
         mode = MODE_MAINTENANCE; // defalt mode
         strncpy(track, "/media/sda1/audio/builtin/1.mp3", SES_BUFFER_SIZE); // first built-in track
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
      doRecord();
   } else {
      doPlay();
   }

FUNCTION_END("::doPlayRecord()");
}

void SES_Client::doPlay() {
FUNCTION_START("::doPlay()");

   DCMSG(GREEN, "Playing %s", track) ;

   // start play process
   PlayProcess::playTrack(track, loop);

FUNCTION_END("::doPlay()");
}

void SES_Client::doRecord() {
FUNCTION_START("::doRecord()");

   DCMSG(GREEN, "Recording %s", track) ;

   // TODO -- start record process

FUNCTION_END("::doRecord()");
}

void SES_Client::doMode(int mode) {
FUNCTION_START("::doMode(int mode)");

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

FUNCTION_END("::doMode(int mode)");
}

void SES_Client::doLoop(unsigned int loop) {
FUNCTION_START("::doPlayRecord(int loop)");

   // set member loop to loop
   this->loop = loop;

FUNCTION_END("::doLoop(int loop)");
}

void SES_Client::doTrack(const char* track) {
FUNCTION_START("::doTrack(const char* track)");

   // check track see if it's a full path
   if (track[0] == '/') {
      // copy contents of track to member track
      strncpy(this->track, track, SES_BUFFER_SIZE);
   } else if (strncmp("builtin/", track, 8) == 0) { // check to see if it's built-in
      // add rest of path
      snprintf(this->track, SES_BUFFER_SIZE, "/media/sda1/audio/%s", track);
   } else if (strncmp("user/", track, 5) == 0) { // check to see if it's user created
      // add rest of path
      snprintf(this->track, SES_BUFFER_SIZE, "/media/sda1/audio/%s", track);
   } else { // assume user track
      // add rest of path
      snprintf(this->track, SES_BUFFER_SIZE, "/media/sda1/audio/user/%s", track);
   }

FUNCTION_END("::doTrack(const char* track)");
}

void SES_Client::doTrack(int track) {
FUNCTION_START("::doTrack(int track)");

   // check validity of track number
   if (track < 0 || track > KNOB_MAX) {
FUNCTION_END("::doTrack(int track)");
      return;
   }

   // create string for track number
   char builtin[16];
   snprintf(builtin, 16, "builtin/%i.mp3", track);

   // set the track using the string
   doTrack(builtin);

FUNCTION_END("::doTrack(int track)");
}

void SES_Client::doStream(const char* uri) {
FUNCTION_START("::doStream(const char* uri)");

   // copy contents of uri to member uri
   strncpy(this->uri, uri, SES_BUFFER_SIZE);

   // TODO -- start streaming process

FUNCTION_END("::doStream(const char* uri)");
}

void SES_Client::doStopPlay() {
FUNCTION_START("::doStopPlay()");

   // TODO -- stop play process

   // TODO -- stop record process

   // TODO -- stop stream process

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
                  if (bit_c->is_on) {
                     ses_client->doPlayRecord();
                  }
                  break;
               case BIT_KNOB:
                  // change selected track
                  ses_client->doTrack(bit_c->is_on);
                  break;
               case BIT_MODE:
                  // change mode
                  if (bit_c->is_on == MODE_STOP) {
                     ses_client->doStopPlay();
                  } else {
                     ses_client->doMode(bit_c->is_on);
                  }
                  break;
            }
         }
	 
         break;
   }
 
FUNCTION_INT("SES_Conn::parseData(struct nl_msg *msg)", 0)
   return 0;
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

