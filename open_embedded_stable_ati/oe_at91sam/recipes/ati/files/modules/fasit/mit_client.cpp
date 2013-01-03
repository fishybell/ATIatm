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

#define DEBUG 1

// setup calibration table
const u32 MIT_Client::cal_table[16] = {0xFFFFFFFF,333,200,125,75,60,48,37,29,22,16,11,7,4,2,1};

/***********************************************************
*                     MIT_Client Class                     *
***********************************************************/
MIT_Client::MIT_Client(int fd, int tnum, bool armor) : TCP_Client(fd, tnum, armor) {
FUNCTION_START("::MIT_Client(int fd, int tnum) : Connection(fd)")
   // connect a fake SIT for now
   att_SIT = new attached_SIT_Client(NULL, 0, -1, armor); // invalid tnum
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
   if (armor) {
      msg->body.type = Type_MAT; // Moving Armor Target
   } else {
      msg->body.type = Type_MIT; // Moving Infantry Target
   }

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
   att_SIT = new attached_SIT_Client(this, fd, -1, armor); // invalid tnum
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
   att_SIT = new attached_SIT_Client(NULL, -1, -1, armor); // invalid tnum
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
   struct hit_calibration lastHitCal;
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
	  case CID_Dock:
	     doDock();
        needPass = false; // don't pass this message to the attached SIT
		 break;
	  case CID_Sleep:
	     doSleep();
		 break;
	  case CID_Wake:
	     doWake();
		 break;
	  case CID_Config_Hit_Sensor:
         switch (msg->on) {
             case 0: lastHitCal.enable_on = BLANK_ALWAYS; break; // hit sensor Off
             case 1: lastHitCal.enable_on = ENABLE_ALWAYS; break; // hit sensor On
             case 2: lastHitCal.enable_on = ENABLE_AT_POSITION; break; // hit sensor On at Position
             case 3: lastHitCal.enable_on = DISABLE_AT_POSITION; break; // hit sensor Off at Position
             case 4: lastHitCal.enable_on = BLANK_ON_CONCEALED; break; // hit sensor On when exposed
         }
         if (htons(msg->burst)) lastHitCal.seperation = htons(msg->burst);      // spec says we only set if non-Zero
         if (htons(msg->sens)) {
             lastHitCal.sensitivity = htons(msg->sens);
/* We do not use the cal_table anymore, see target_hit_poll
             if (htons(msg->sens) > 15) {
                 lastHitCal.sensitivity = cal_table[15];
             } else {
                 lastHitCal.sensitivity = cal_table[htons(msg->sens)];
             }
*/
         }

         if (htons(msg->tokill))  lastHitCal.hits_to_kill = htons(msg->tokill); 
         lastHitCal.after_kill = msg->react;    // 0 for stay down
         lastHitCal.type = msg->mode;           // mechanical sensor
         lastHitCal.set = HIT_OVERWRITE_ALL;    // nothing will change without this
	     doHitCal(lastHitCal);
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

      case CID_Gohome:
		 // send 2101 ack  (2102's will be generated at start and stop of actuator)
         doGoHome();
         needPass = false; // don't pass this message to the attached SIT
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
      case CID_Continuous_Move_Request:
		 // send 2101 ack  (2102's will be generated at start and stop of actuator)
         doContinuousMove(ntohf(msg->speed), 1);
         needPass = false; // don't pass this message to the attached SIT
         break;
      case CID_Status_Request:
         if (!hasSIT()) {
            // if we don't have a sit, still respond to status requests
            sendStatus2102();
         }
         break;
      case CID_MoveAway:
		 // send 2101 ack  (2102's will be generated at start and stop of actuator)
         doMoveAway(ntohf(msg->speed), 1);
         needPass = false; // don't pass this message to the attached SIT
         break;
      case CID_Expose_Request:
         printf("-------------------------\nMIT received Expose: %i, might pass on: %i:%i\n-------------------------\n", msg->exp, needPass, hasSIT());
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
void MIT_Client::doExpose(int val) {
FUNCTION_START("::doExpose(int val)")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doExpose(val);
   }
FUNCTION_END("::doExpose(int val)")
}

// sleep device
void MIT_Client::doDock() {
FUNCTION_START("::doDock()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doDock();
   }
FUNCTION_END("::doDock()")
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

void MIT_Client::doHitCal(struct hit_calibration hit_c) {
    FUNCTION_START("::doHitCal(struct hit_calibration hit_c)");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doHitCal(hit_c);
    }
    FUNCTION_END("::doHitCal(struct hit_calibration hit_c)");
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

// current expose value
void MIT_Client::didExpose(int val) {
FUNCTION_START("::didExpose(int val)")

   doExpose(val); // Tell mover expose state

FUNCTION_END("::didExpose(int val)")
}

// current battery value
void MIT_Client::didBattery(int val) {
FUNCTION_START("::didBattery(int val)")

   // if we're low we'll need to tell userspace
   if (val <= FAILURE_BATTERY_VAL) {
      didFailure(ERR_critical_battery);
   } else if (val <= MIN_BATTERY_VAL) {
      didFailure(ERR_low_battery);
   } else {
      didFailure(ERR_normal_battery);
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

// start movement or change movement
void MIT_Client::doGoHome() {
FUNCTION_START("::doGoHome()")
   DMSG("GoHome\n");
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doGoHome();
   }
FUNCTION_END("::doGoHome()")
}

// start movement or change movement
void MIT_Client::doContinuousMove(float speed, int direction) { // speed in mph, direction 1 for forward, -1 for reverse, 0 for stop
FUNCTION_START("::doContinuousMove(float speed, int direction)")
   DMSG("Moving %f %i\n", speed, direction);
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doContinuousMove(speed, direction);
   }
FUNCTION_END("::doContinuousMove(float speed, int direction)")
}

// retrieve movement values
void MIT_Client::doContinuousMove() {
FUNCTION_START("::doContinuousMove()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doContinuousMove();
   }
FUNCTION_END("::doContinuousMove()")
}

// start movement or change movement
void MIT_Client::doMoveAway(float speed, int direction) { // speed in mph, direction 1 for forward, -1 for reverse, 0 for stop
FUNCTION_START("::doMoveAway(float speed, int direction)")
   DMSG("Moving %f %i\n", speed, direction);
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doMoveAway(speed, direction);
   }
FUNCTION_END("::doMoveAway(float speed, int direction)")
}

// retrieve movement values
void MIT_Client::doMoveAway() {
FUNCTION_START("::doMoveAway()")
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doMoveAway();
   }
FUNCTION_END("::doMoveAway()")
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
attached_SIT_Client::attached_SIT_Client(MIT_Client *mit, int fd, int tnum, int armor)  : FASIT_TCP(fd, tnum, armor) {
FUNCTION_START("::attached_SIT_Client(MIT_Client *mit, int fd, int tnum, int armor)  : FASIT_TCP(fd, tnum, armor)")
   mit_client = mit;
   client = mit; // for pair()
FUNCTION_END("::attached_SIT_Client(MIT_Client *mit, int fd, int tnum, int armor)  : FASIT_TCP(fd, tnum, armor)")
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

      case NL_C_EXPOSE:
         // Movers need to know the expose state to
         // know if they can dock
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                // received expose value
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                mit_client->didExpose(value); // tell client
            }
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

// Command
void MIT_Conn::doGoHome() {
FUNCTION_START("::doGoHome()")
   DCMSG(GREEN, "MIT_Conn doGoHome ");
   
   queueMsgU8(NL_C_GOHOME, 1);

FUNCTION_END("::doGoHome()")
}

// start movement or change movement
void MIT_Conn::doContinuousMove(float speed, int direction) { // speed in mph, direction 1 for forward, -1 for reverse, 0 for stop
FUNCTION_START("::doContinuousMove(float speed, int direction)")
   DCMSG(GREEN, "MIT_Conn doContinuousMove - speed: %f", speed);
   // Queue command
   queueMsgU16(NL_C_CONTINUOUS, 32768+(speed*10));  //speed * 10

FUNCTION_END("::doContinuousMove(float speed, int direction)")
}

// retrieve movement values
void MIT_Conn::doContinuousMove() {
FUNCTION_START("::doContinuousMove()")
   // Queue command
   queueMsgU16(NL_C_CONTINUOUS, 0);

FUNCTION_END("::doContinuousMove()")
}

// start movement or change movement
void MIT_Conn::doMoveAway(float speed, int direction) { // speed in mph, direction 1 for forward, -1 for reverse, 0 for stop
FUNCTION_START("::doMoveAway(float speed, int direction)")
   DCMSG(GREEN, "MIT_Conn doMoveAway - speed: %f", speed);
   // Queue command
   queueMsgU16(NL_C_MOVEAWAY, 32768+(speed*10));  //speed * 10

FUNCTION_END("::doMoveAway(float speed, int direction)")
}

// retrieve movement values
void MIT_Conn::doMoveAway() {
FUNCTION_START("::doMoveAway()")
   // Queue command
   queueMsgU16(NL_C_MOVEAWAY, 0);

FUNCTION_END("::doMoveAway()")
}

// shutdown device
void MIT_Conn::doShutdown() {
FUNCTION_START("::doShutdown()")

   // Queue command
   queueMsgU8(NL_C_BATTERY, BATTERY_SHUTDOWN); // shutdown command

FUNCTION_END("::doShutdown()")
}

// tell mover expose state
void MIT_Conn::doExpose(int val) {
FUNCTION_START("::doExpose(int val)")

   // Queue command
   queueMsgU8(NL_C_EXPOSE, val); // tell mover expose state

FUNCTION_END("::doExpose(int val)")
}

// dock device
void MIT_Conn::doDock() {
FUNCTION_START("::doDock()")

   // Queue command
   queueMsgU8(NL_C_SLEEP, DOCK_COMMAND); // dock command

FUNCTION_END("::doDock()")
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

// change hit calibration data
void MIT_Conn::doHitCal(struct hit_calibration hit_c) {
    FUNCTION_START("::doHitCal(struct hit_calibration hit_c)");

    // Queue command
    queueMsg(NL_C_HIT_CAL, HIT_A_MSG, sizeof(struct hit_calibration), &hit_c); // pass structure without changing

    FUNCTION_END("::doHitCal(struct hit_calibration hit_c)");
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

