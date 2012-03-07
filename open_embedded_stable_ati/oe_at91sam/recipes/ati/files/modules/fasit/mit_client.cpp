#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <signal.h>

using namespace std;

#include "mit_client.h"
#include "common.h"
#include "timers.h"
#include "eeprom.h"
#include "defaults.h"


/***********************************************************
*                     MIT_Client Class                     *
***********************************************************/
MIT_Client::MIT_Client(int fd, int tnum) : TCP_Client(fd, tnum) {
FUNCTION_START("::MIT_Client(int fd, int tnum) : Connection(fd)")
   // connect a fake SIT for now
   att_SIT = new attached_SIT_Client(NULL, 0, -1); // invalid tnum
   server = att_SIT; // for pair()

   // we have not yet connected to SmartRange/TRACR
   ever_conn = false;
   skippedFault = 0; // haven't skipped any faults


   // connect our netlink connection
   nl_conn = NL_Conn::newConn<MIT_Conn>(this);
   if (nl_conn == NULL) {
      deleteLater();
   } else {
      // initialize default settings
      lastPosition = 0;
      lastSpeed = 0.0; // the kernel message is in whole numbers, so don't bother storing more precision
      lastDirection = 0;
      memset(&lastSITdevcaps, 0, sizeof(FASIT_2111));
      memset(&lastSITstatus, 0, sizeof(FASIT_2102));

      lastBatteryVal = MAX_BATTERY_VAL;
      nl_conn->doBattery(); // get a correct battery value soon
   }
FUNCTION_END("::MIT_Client(int fd, int tnum) : Connection(fd)")
}

MIT_Client::~MIT_Client() {
FUNCTION_START("::~MIT_Client()")

FUNCTION_END("::~MIT_Client()")
}

void MIT_Client::Reset() {
FUNCTION_START("::Reset()");
      signal(SIGCHLD, SIG_IGN);
      if (!fork()) {
        sleep(2);
        DCMSG(BLUE,"Preparing to REBOOT");
         nl_conn->~NL_Conn();
         if (fd >= 0) close(fd);
         closeListener();
         execl("/usr/bin/restart", "restart", (char *)0 );
         exit(0);
      }
//      pid = getpid();
//      kill(pid, SIGQUIT);
FUNCTION_END("::Reset()");
}

// fill out 2102 status message
void MIT_Client::fillStatus2102(FASIT_2102 *msg) {
FUNCTION_START("::fillStatus2102(FASIT_2102 *msg)")

   // start with zeroes
   memset(msg, 0, sizeof(FASIT_2102));

   // fill out as response
   msg->response.rnum = resp_num;
   msg->response.rseq = resp_seq;
   resp_num = resp_seq = 0; // the next one will be unsolicited

   // exposure
   msg->body.exp = lastSITstatus.body.exp;
   msg->body.asp = lastSITstatus.body.asp;
   
   // movement
   msg->body.dir = 0; // not a trackless design, never facing any direction
   switch (lastDirection) {
      case 0: msg->body.move = 0; break; // stopped
      case 1: msg->body.move = 1; break; // forward
      case -1: msg->body.move = 2; break; // reverse
   }
   DCMSG(GREEN, "Last speed in fillStatus2012: %f\n", htonf(lastSpeed));
   msg->body.speed = htonf(lastSpeed);
   msg->body.pos = htons(feetToMeters(lastPosition));

   // device type
   msg->body.type = 2; // MIT. TODO -- MIT vs. MAT

   // hit record
   msg->body.hit = lastSITstatus.body.hit;
   msg->body.hit_conf = lastSITstatus.body.hit_conf;

FUNCTION_END("::fillStatus2102(FASIT_2102 *msg)")
}


// create and send a status messsage to the FASIT server
void MIT_Client::sendStatus2102() {
FUNCTION_START("::sendStatus2102()")

   // ignore if we haven't connected yet
   if (!ever_conn) {
      FUNCTION_END("::sendStatus2102()");
      return;
   }

   FASIT_header hdr; FASIT_2102 msg;
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

// returns true when a FASIT_TCP object has been successfully attached as a SIT
bool MIT_Client::hasSIT() {
   FUNCTION_START("::hasSIT()");
   // is the attached SIT object fake or real?
   if (hasPair()) {
      if (att_SIT == NULL) return false;
      bool ret = att_SIT->attached();
      FUNCTION_INT("::hasSIT()", ret);
      return ret;
   }
   FUNCTION_INT("::hasSIT()", false);
   return false;
}

// attempt to attach a socket as a SIT
FASIT_TCP *MIT_Client::addSIT(int fd) {
FUNCTION_START("::addSIT(int fd)")

   // are we already attached to a SIT?
/*   if (hasSIT()) {
      return NULL;
   } */

   // delete old fake SIT
   server = NULL;
   delete att_SIT;

   memset(&lastSITdevcaps, 0, sizeof(FASIT_2111));
   memset(&lastSITstatus, 0, sizeof(FASIT_2102));
   // connect with valid socket
   att_SIT = new attached_SIT_Client(this, fd, -1); // invalid tnum
   server = att_SIT;

   // send Device Definition Request (message 100) to attached SIT client
   FASIT_header hdr;
   defHeader(100, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header));

   // send
   att_SIT->queueMsg(&hdr, sizeof(FASIT_header));
   att_SIT->finishMsg();

FUNCTION_HEX("::addSIT(int fd)", att_SIT)
   return (FASIT_TCP*)att_SIT;
}

// the SIT has become disconnected
void MIT_Client::delSIT() {
FUNCTION_START("::delSIT()")

   // delete SIT connection points
   // TODO -- clear lastSIT* ?

   didFailure(ERR_disconnected_SIT);

   memset(&lastSITdevcaps, 0, sizeof(FASIT_2111));
   memset(&lastSITstatus, 0, sizeof(FASIT_2102));
   // create a new dummy SIT 
   att_SIT = new attached_SIT_Client(NULL, -1, -1); // invalid tnum
   server = att_SIT;

FUNCTION_END("::delSIT()")
}


/***********************************************************
*                  FASIT Message Handlers                  *
***********************************************************/
void MIT_Client::handle_2111(FASIT_2111 *msg) {
FUNCTION_START("::handle_2111(FASIT_2111 *msg)")

   // TODO -- if we're not a SIT, detach and force to be a proxy

   // remember 2111 values
   lastSITdevcaps = *msg;

   // disconnect from range computer and reconnect
   //reconnect(); // this will force the server to re-request the 2111 message, but now it will be filled in

   // send unsolicited 2111 with updated data -- TODO -- see how badly this violates the spec
   FASIT_header rhdr;
   FASIT_2111 rmsg;
   defHeader(2111, &rhdr); // sets the sequence number and other data
   rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2111));
   
   // set response (unsolicited)
   rmsg.response.rnum = 0;
   rmsg.response.rseq = 0;

   // fill message
   rmsg.body.devid = getDevID();
   rmsg.body.flags = lastSITdevcaps.body.flags; // the MIT has no flags of its own -- TODO -- unless we have GPS

   // send
   queueMsg(&rhdr, sizeof(FASIT_header));
   queueMsg(&rmsg, sizeof(FASIT_2111));
   finishMsg();

FUNCTION_END("::handle_2111(FASIT_2111 *msg)")
}

void MIT_Client::handle_2102(FASIT_2102 *msg) {
FUNCTION_START("::handle_2102(FASIT_2102 *msg)")

   // remember 2102 values
   lastSITstatus = *msg;

   // pass on to range computer
   sendStatus2102();

FUNCTION_END("::handle_2102(FASIT_2102 *msg)")
}

int MIT_Client::handle_100(int start, int end) {
FUNCTION_START("::handle_100(int start, int end)")

   // do handling of message
   IMSG("Handling 100 in MIT\n");

   // map header (no body for 100)
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);

   // message 100 is Device Definition Request, send back 2111
   FASIT_header rhdr;
   FASIT_2111 msg;
   defHeader(2111, &rhdr); // sets the sequence number and other data
   rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2111));
   
   // set response
   msg.response.rnum = htons(100);
   msg.response.rseq = hdr->seq;

   // fill message
   msg.body.devid = getDevID();
   msg.body.flags = lastSITdevcaps.body.flags; // the MIT has no flags of its own -- TODO -- unless we have GPS

   // grab current values from kernel
   if (lastPosition == 0 && lastDirection == 0 && lastSpeed < 0.00001 && lastSpeed > -0.00001) {
      doPosition();
      doMove(0, 0); // stops the mover and grabs current values for speed and direction
   }

   // send
   queueMsg(&rhdr, sizeof(FASIT_header));
   queueMsg(&msg, sizeof(FASIT_2111));
   finishMsg();

   // we were connected at some point in time
   ever_conn = true;

   // get around to skipped faults now
   if (skippedFault != 0) {
      didFailure(skippedFault);
   }

FUNCTION_INT("::handle_100(int start, int end)", 0)
   return 0;
}

int MIT_Client::handle_2100(int start, int end) {
   int pid;
   int resetMIT = false;
FUNCTION_START("::handle_2100(int start, int end)")

   // do handling of message
   IMSG("Handling 2100 in MIT\n");

   // map header and body for both message and response
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2100 *msg = (FASIT_2100*)(rbuf + start + sizeof(FASIT_header));

   IMSG("handle_2100: %f\n", ntohf(msg->speed));
   // TODO -- handle other commands here
   bool needPass = true;
   switch (msg->cid) {
      case CID_Stop:
         doStop();
         break;
      case CID_Shutdown:
         doShutdown();
         break;
	  case CID_Sleep:
	     doSleep();
		 break;
	  case CID_Wake:
	     doWake();
		 break;
     case CID_Reset_Device:
         // send 2101 ack
         DCMSG(RED,"CID_Reset_Device  send 'S'uccess ack.   set lastHitCal.* to defaults") ;
         send_2101_ACK(hdr,'S');
         // also supposed to reset all values to the 'initial exercise step value'
         //  which I am not sure if it is different than ordinary inital values 
         Reset();
         resetMIT = true;
         break;

      case CID_Move_Request:
		 // send 2101 ack  (2102's will be generated at start and stop of actuator)
	     send_2101_ACK(hdr,'S');
         
         switch (msg->move) {
            case 0:
               doMove(0, 0);
               break;
            case 1:
               doMove(ntohf(msg->speed), 1);
               break;
            case 2:
               doMove(ntohf(msg->speed), -1);
               break;
         }
         needPass = false; // don't pass this message to the attached SIT
         break;
   }

   // pass lift commands to SIT
   if (needPass && hasSIT()) {
      DMSG("Passing command to SIT\n");
      att_SIT->queueMsg(hdr, sizeof(FASIT_header));
      att_SIT->queueMsg(msg, sizeof(FASIT_2100));
      att_SIT->finishMsg();
   }

FUNCTION_INT("::handle_2100(int start, int end)", 0)
   if (resetMIT) {
      att_SIT->forceQueueDump();
      pid = getpid();
      kill(pid, SIGQUIT);
   }
   return 0;
}

/***********************************************************
*                    Basic MIT Commands                    *
***********************************************************/
// experienced failure "type"
void MIT_Client::didFailure(int type) {
FUNCTION_START("::didFailure(int type)")

   // ignore if we haven't connected yet
   if (!ever_conn) {
      skippedFault = type; // send after connection
      FUNCTION_END("::didFailure(int type)")
      return;
   }

   FASIT_header hdr;
   FASIT_2102 msg;
   DCMSG(BLUE,"Prepared to send 2102 failure packet: %i", type);
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
void MIT_Client::doShutdown() {
FUNCTION_START("::doShutdown()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doShutdown();
   }
FUNCTION_END("::doShutdown()")
}

// sleep device
void MIT_Client::doSleep() {
FUNCTION_START("::doSleep()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doSleep();
   }
FUNCTION_END("::doSleep()")
}

// wake device
void MIT_Client::doWake() {
FUNCTION_START("::doWake()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doWake();
   }
FUNCTION_END("::doWake()")
}

// retrieve battery value
void MIT_Client::doBattery() {
FUNCTION_START("::doBattery()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doBattery();
   }
FUNCTION_END("::doBattery()")
}

// current battery value
void MIT_Client::didBattery(int val) {
FUNCTION_START("::didBattery(int val)")

   // if we're low we'll need to tell userspace
   if (val <= FAILURE_BATTERY_VAL) {
      didFailure(ERR_critical_battery);
   } else if (val <= MIN_BATTERY_VAL) {
      didFailure(ERR_low_battery);
   }

   // save the information for the next time
   lastBatteryVal = val;

FUNCTION_END("::didBattery(int val)")
}

// current fault value
void MIT_Client::didFault(int val) {
    FUNCTION_START("::didFault(int val)");

    didFailure(val);

    FUNCTION_END("::didFault(int val)");
}

// retrieve position value
void MIT_Client::doPosition() {
FUNCTION_START("::doPosition()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doPosition();
   }
FUNCTION_END("::doPosition()")
}

// current position value
void MIT_Client::didPosition(int pos) {
FUNCTION_START("::didPosition(int pos)")

   // remember position value, send status if changed
   if (pos != lastPosition) {
      lastPosition = pos;
      DCMSG(GREEN,"didPosition is forcing a 2102 status") ;      
      sendStatus2102();
   }

FUNCTION_END("::didPosition(int pos)")
}

// start movement or change movement
void MIT_Client::doMove(float speed, int direction) { // speed in mph, direction 1 for forward, -1 for reverse, 0 for stop
FUNCTION_START("::doMove(float speed, int direction)")
   DMSG("Moving %f %i\n", speed, direction);
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doMove(speed, direction);
   }
FUNCTION_END("::doMove(float speed, int direction)")
}

// retrieve movement values
void MIT_Client::doMove() {
FUNCTION_START("::doMove()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doMove();
   }
FUNCTION_END("::doMove()")
}

// current direction value
void MIT_Client::didMove(float speed, int direction) {
FUNCTION_START("::didMove(int direction)")

   // remember speed/direction values, send status if changed
   //if ((fabs(speed - lastSpeed) > 0.0001) || direction != lastDirection) {
	  DCMSG(RED, "lastSpeed and speed in didMove: %f, %f", lastSpeed, speed);
      lastSpeed = speed;
      lastDirection = direction;
      DCMSG(GREEN,"didMove is forcing a 2102 status") ;      
      sendStatus2102();
   //}

FUNCTION_END("::didMove(int direction)")
}

// immediate stop (stops accessories as well)
void MIT_Client::doStop() {
FUNCTION_START("::doStop()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doStop();
   }
FUNCTION_END("::doStop()")
}

// received immediate stop response
void MIT_Client::didStop() {
FUNCTION_START("::didStop()")

   // notify the front-end of the emergency stop
   didFailure(ERR_emergency_stop);

FUNCTION_END("::didStop()")
}

/***********************************************************
*                attached_SIT_Client class                 *
***********************************************************/
attached_SIT_Client::attached_SIT_Client(MIT_Client *mit, int fd, int tnum)  : FASIT_TCP(fd, tnum) {
FUNCTION_START("::attached_SIT_Client(MIT_Client *mit, int fd, int tnum)  : FASIT_TCP(fd, tnum)")
   mit_client = mit;
   client = mit; // for pair()
FUNCTION_END("::attached_SIT_Client(MIT_Client *mit, int fd, int tnum)  : FASIT_TCP(fd, tnum)")
}

attached_SIT_Client::~attached_SIT_Client() {
FUNCTION_START("::~attached_SIT_Client()")
   // tell MIT we've been lost
   if (mit_client != NULL) {
      mit_client->delSIT();
      client = NULL; // so we don't propogate our delete
   }
FUNCTION_END("::~attached_SIT_Client()")
}

void attached_SIT_Client::queueMsg(const char *msg, int size) {
FUNCTION_START("::queueMsg(const char *msg, int size)")
   // check for attached pair
   if (!hasPair()) {
      return; // no pair; ignore
   }

   // pass to parent class
   Connection::queueMsg(msg, size);
FUNCTION_END("::queueMsg(const char *msg, int size)")
}

/***********************************************************
*                  FASIT Message Handlers                  *
***********************************************************/
int attached_SIT_Client::handle_2111(int start, int end) {
FUNCTION_START("::handle_2111(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2111 *msg = (FASIT_2111*)(rbuf + start + sizeof(FASIT_header));

   // handle in MIT class
   if (hasPair()) {
      mit_client->handle_2111(msg);
   }

FUNCTION_INT("::handle_2111(int start, int end)", 0)
   return 0;
}

int attached_SIT_Client::handle_2102(int start, int end) {
FUNCTION_START("::handle_2102(int start, int end)")
   // map header and message
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2102 *msg = (FASIT_2102*)(rbuf + start + sizeof(FASIT_header));

   // send via client
   if (hasPair()) {
      if (ntohs(msg->body.fault) != ERR_normal) {
         mit_client->didFault(ntohs(msg->body.fault));
      }
      mit_client->handle_2102(msg);
   }

FUNCTION_INT("::handle_2102(int start, int end)", 0)
   return 0;
}

// stop the mover on disconnect
bool MIT_Client::reconnect() {
   FUNCTION_START("::reconnect()");

   // first off, handle the real reconnect
   bool retval = TCP_Client::reconnect();

   // second, stop the MIT if we were ever connected
   if (ever_conn) {
      doMove(0, 0);
   }

   FUNCTION_INT("::reconnect()", retval);
   return retval;
}

/***********************************************************
*                      MIT_Conn Class                      *
***********************************************************/
MIT_Conn::MIT_Conn(struct nl_handle *handle, MIT_Client *client, int family) : NL_Conn(handle, client, family) {
FUNCTION_START("::MIT_Conn(struct nl_handle *handle, TCP_Client *client, int family) : NL_Conn(handle, client, family)")

   mit_client = client;

FUNCTION_END("::MIT_Conn(struct nl_handle *handle, TCP_Client *client, int family) : NL_Conn(handle, client, family)")
}

MIT_Conn::~MIT_Conn() {
FUNCTION_START("::~MIT_Conn()")

FUNCTION_END("::~MIT_Conn()")
}

int MIT_Conn::parseData(struct nl_msg *msg) {
FUNCTION_START("::parseData(struct nl_msg *msg)")
    struct nlattr *attrs[NL_A_MAX+1];
   struct nlmsghdr *nlh = nlmsg_hdr(msg);
   struct genlmsghdr *ghdr = static_cast<genlmsghdr*>(nlmsg_data(nlh));

   // parse message and call individual commands as needed
   switch (ghdr->cmd) {
      case NL_C_FAILURE:
         genlmsg_parse(nlh, 0, attrs, GEN_STRING_A_MAX, generic_string_policy);

         // TODO -- failure messages need decodable data
         if (attrs[GEN_STRING_A_MSG]) {
            char *data = nla_get_string(attrs[GEN_STRING_A_MSG]);
            IERROR("netlink failure attribute: %s\n", data)
         }

         break;

      case NL_C_MOVE :
         genlmsg_parse(nlh, 0, attrs, GEN_INT16_A_MAX, generic_int16_policy);

         if (attrs[GEN_INT16_A_MSG]) {
            // moving at # mph
            int value = nla_get_u16(attrs[GEN_INT16_A_MSG]) - 32768; // message was unsigned, fix it
		DCMSG(RED, "int value: %i", value);
	    float fvalue = (float)value;
		DCMSG(RED, "float value: %f", fvalue);
            fvalue = fvalue / 10;
            // convert to absolute speed and a direction
            if (fvalue < -0.00001) {
               mit_client->didMove(-1*fvalue, -1);
            } else if (fvalue > 0.00001) {
               mit_client->didMove(fvalue, 1);
            } else {
               mit_client->didMove(0, 0);
            }
         }
         break;

      case NL_C_POSITION :
         genlmsg_parse(nlh, 0, attrs, GEN_INT16_A_MAX, generic_int16_policy);

         if (attrs[GEN_INT16_A_MSG]) {
            // # feet from home
            int value = nla_get_u16(attrs[GEN_INT16_A_MSG]) - 0x8000; // message was unsigned, fix it
            mit_client->didPosition(value);
         }

         break;

      case NL_C_BATTERY:
         genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

         if (attrs[GEN_INT8_A_MSG]) {
            // received change in battery value
            int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
            mit_client->didBattery(value); // tell client
         }
         break;

        case NL_C_FAULT:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                // received fault value
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                mit_client->didFault(value); // tell client
            }
            break;

      case NL_C_STOP:
         // received emergency stop response
         mit_client->didStop(); // tell client
         break;
   }
 
FUNCTION_INT("::parseData(struct nl_msg *msg)", 0)
   return 0;
}

/***********************************************************
*                   MIT Netlink Commands                   *
***********************************************************/
// retrieve position value
void MIT_Conn::doPosition() {
FUNCTION_START("::doPosition()")
FUNCTION_END("::doPosition()")
}

// start movement or change movement
void MIT_Conn::doMove(float speed, int direction) { // speed in mph, direction 1 for forward, -1 for reverse, 0 for stop
FUNCTION_START("::doMove(float speed, int direction)")
   DCMSG(GREEN, "MIT_Conn doMove - speed: %f", speed);
   // force valid value for speed
   if (speed < 0 || direction == 0) {
      DCMSG(RED, "First if speed: %f", speed);
      speed = 0;
   }
   if (speed > 32766) {
      DCMSG(RED, "Second if speed: %f", speed);
      speed = 0; // fault, so stop
   }

   // force valid value for direction
   if (speed == 0) {
      direction = 0;
   }

   //Multiply the speed by 10 to get rid of the decimal
   
   // Queue command
   if (direction < 0) {
      queueMsgU16(NL_C_MOVE, 32768-(speed*10));  //speed * 10
   } else if (direction > 0) {
      queueMsgU16(NL_C_MOVE, 32768+(speed*10));  //speed * 10
   } else {
      queueMsgU16(NL_C_MOVE, VELOCITY_STOP);
   }

FUNCTION_END("::doMove(float speed, int direction)")
}

// retrieve movement values
void MIT_Conn::doMove() {
FUNCTION_START("::doMove()")
   // Queue command
   queueMsgU16(NL_C_MOVE, VELOCITY_REQ);

FUNCTION_END("::doMove()")
}

// shutdown device
void MIT_Conn::doShutdown() {
FUNCTION_START("::doShutdown()")

   // Queue command
   queueMsgU8(NL_C_BATTERY, BATTERY_SHUTDOWN); // shutdown command

FUNCTION_END("::doShutdown()")
}

// sleep device
void MIT_Conn::doSleep() {
FUNCTION_START("::doSleep()")

   // Queue command
   queueMsgU8(NL_C_SLEEP, SLEEP_COMMAND); // sleep command

FUNCTION_END("::doSleep()")
}

// wake device
void MIT_Conn::doWake() {
FUNCTION_START("::doWake()")

   // Queue command
   queueMsgU8(NL_C_SLEEP, WAKE_COMMAND); // wake command

FUNCTION_END("::doWake()")
}

// retrieve battery value
void MIT_Conn::doBattery() {
FUNCTION_START("::doBattery()")

   // Queue command
   queueMsgU8(NL_C_BATTERY, BATTERY_REQUEST); // battery status request

FUNCTION_END("::doBattery()")
}

// immediate stop (stops accessories as well)
void MIT_Conn::doStop() {
FUNCTION_START("::doStop()")

   // Queue command
   queueMsgU8(NL_C_STOP, 1); // emergency stop command

FUNCTION_END("::doStop()")
}

