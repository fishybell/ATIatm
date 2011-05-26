#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

using namespace std;

#include "mit_client.h"
#include "common.h"
#include "timers.h"


/***********************************************************
*                     MIT_Client Class                     *
***********************************************************/
MIT_Client::MIT_Client(int fd, int tnum) : TCP_Client(fd, tnum) {
FUNCTION_START("::MIT_Client(int fd, int tnum) : Connection(fd)")
   // connect a fake SIT for now
   att_SIT = new attached_SIT_Client(NULL, 0, -1); // invalid tnum
   server = att_SIT; // for pair()

   // connect our netlink connection
   nl_conn = NL_Conn::newConn<MIT_Conn>(this);
   if (nl_conn == NULL) {
      deleteLater();
   } else {
      // initialize default settings
      lastPosition = 0;
      lastSpeed = 0; // the kernel message is in whole numbers, so don't bother storing more precision
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
   msg->body.speed = htonf((float)lastSpeed);
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

// returns true when a FASIT_TCP object has been successfully attached as a SIT
bool MIT_Client::hasSIT() {
   FUNCTION_START("::hasSIT()");
   // is the attached SIT object fake or real?
   if (hasPair()) {
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
   if (hasSIT()) {
      return NULL;
   }

   // delete old fake SIT
   server = NULL;
   delete att_SIT;

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

   // create a new dummy SIT 
   att_SIT = new attached_SIT_Client(NULL, 0, -1); // invalid tnum
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
   if (lastPosition == 0 && lastDirection == 0 && lastSpeed == 0) {
      doPosition();
      doMove(0, 0); // stops the mover and grabs current values for speed and direction
   }

   // send
   queueMsg(&rhdr, sizeof(FASIT_header));
   queueMsg(&msg, sizeof(FASIT_2111));
   finishMsg();

FUNCTION_INT("::handle_100(int start, int end)", 0)
   return 0;
}

int MIT_Client::handle_2100(int start, int end) {
FUNCTION_START("::handle_2100(int start, int end)")

   // do handling of message
   IMSG("Handling 2100 in MIT\n");

   // map header and body for both message and response
   FASIT_header *hdr = (FASIT_header*)(rbuf + start);
   FASIT_2100 *msg = (FASIT_2100*)(rbuf + start + sizeof(FASIT_header));

   // TODO -- handle other commands here
   bool needPass = true;
   switch (msg->cid) {
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
   return 0;
}

/***********************************************************
*                    Basic MIT Commands                    *
***********************************************************/
// experienced failure "type"
void MIT_Client::didFailure(int type) {
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
      sendStatus2102();
   }

FUNCTION_END("::didPosition(int pos)")
}

// start movement or change movement
void MIT_Client::doMove(int speed, int direction) { // speed in mph, direction 1 for forward, -1 for reverse, 0 for stop
FUNCTION_START("::doMove(int speed, int direction)")
   DMSG("Moving %i %i\n", speed, direction);
   // pass directly to kernel for actual action
   if (nl_conn != NULL) {
      nl_conn->doMove(speed, direction);
   }
FUNCTION_END("::doMove(int speed, int direction)")
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
void MIT_Client::didMove(int speed, int direction) {
FUNCTION_START("::didMove(int direction)")

   // remember speed/direction values, send status if changed
   if (speed != lastSpeed || direction != lastDirection) {
      lastSpeed = speed;
      lastDirection = direction;
      sendStatus2102();
   }

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

   // second, stop the MIT
   doMove(0, 0);

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
         genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

         if (attrs[GEN_INT8_A_MSG]) {
            // moving at # mph
            int value = nla_get_u8(attrs[GEN_INT8_A_MSG]) - 128; // message was unsigned, fix it
            // convert to absolute speed and a direction
            if (value < 0) {
               mit_client->didMove(-1*value, -1);
            } else if (value > 0) {
               mit_client->didMove(value, 1);
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
void MIT_Conn::doMove(int speed, int direction) { // speed in mph, direction 1 for forward, -1 for reverse, 0 for stop
FUNCTION_START("::doMove(int speed, int direction)")

   // force valid value for speed
   if (speed < 0 || direction == 0) {
      speed = 0;
   }
   if (speed > 126) {
      speed = 0; // fault, so stop
   }

   // force valid value for direction
   if (speed == 0) {
      direction = 0;
   }

   // Queue command
   if (direction < 0) {
      queueMsgU8(NL_C_MOVE, 128-speed);
   } else if (direction > 0) {
      queueMsgU8(NL_C_MOVE, 128+speed);
   } else {
      queueMsgU8(NL_C_MOVE, VELOCITY_STOP);
   }

FUNCTION_END("::doMove(int speed, int direction)")
}

// retrieve movement values
void MIT_Conn::doMove() {
FUNCTION_START("::doMove()")

   // Queue command
   queueMsgU8(NL_C_MOVE, VELOCITY_REQ);

FUNCTION_END("::doMove()")
}

// retrieve battery value
void MIT_Conn::doBattery() {
FUNCTION_START("::doBattery()")

   // Queue command
   queueMsgU8(NL_C_BATTERY, 1); // battery status request

FUNCTION_END("::doBattery()")
}

// immediate stop (stops accessories as well)
void MIT_Conn::doStop() {
FUNCTION_START("::doStop()")

   // Queue command
   queueMsgU8(NL_C_STOP, 1); // emergency stop command

FUNCTION_END("::doStop()")
}

