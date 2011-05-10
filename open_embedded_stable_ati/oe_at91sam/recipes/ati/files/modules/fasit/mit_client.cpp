#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

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
void MIT_Client::fillStatus(FASIT_2102 *msg) {
FUNCTION_START("::fillStatus(FASIT_2102 *msg)")

   // TODO -- fill with MIT status and lastSITstatus

FUNCTION_END("::fillStatus(FASIT_2102 *msg)")
}


// create and send a status messsage to the FASIT server
void MIT_Client::sendStatus() {
FUNCTION_START("::sendStatus()")

   FASIT_header hdr;
   FASIT_2102 msg;
   defHeader(2102, &hdr); // sets the sequence number and other data
   hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2102));

   // fill message
   fillStatus(&msg); // fills in status values with current values

   // send
   queueMsg(&hdr, sizeof(FASIT_header));
   queueMsg(&msg, sizeof(FASIT_2102));

FUNCTION_END("::sendStatus()")
}

// returns true when a FASIT_TCP object has been successfully attached as a SIT
bool MIT_Client::hasSIT() {
FUNCTION_START("::hasSIT()")
   // is the attached SIT object fake or real?
   if (hasPair()) {
      return att_SIT->attached();
   }
FUNCTION_INT("::hasSIT()", false)
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
   queueMsg(&hdr, sizeof(FASIT_header));

FUNCTION_HEX("::addSIT(int fd)", att_SIT)
   return (FASIT_TCP*)att_SIT;
}


/***********************************************************
*                  FASIT Message Handlers                  *
***********************************************************/
void MIT_Client::handle_2111(FASIT_2111 *msg) {
FUNCTION_START("::handle_2111(FASIT_2111 *msg)")

   // TODO -- if we're not a SIT, detach and force to be a proxy

   // remember 2111 values
   lastSITdevcaps = *msg;

   // TODO -- disconnect from range computer and reconnect

FUNCTION_END("::handle_2111(FASIT_2111 *msg)")
}

void MIT_Client::handle_2102(FASIT_2102 *msg) {
FUNCTION_START("::handle_2102(FASIT_2102 *msg)")

   // remember 2102 values
   lastSITstatus = *msg;

   // pass on to range computer
   sendStatus();

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
   // TODO -- fill with lastSITdevcaps

   // grab current values from kernel
   if (lastPosition == 0 && lastDirection == 0 && lastSpeed == 0) {
      doPosition();
      doMove(0, 0); // stops the mover and grabs current values for speed and direction
   }

   // send
   queueMsg(&rhdr, sizeof(FASIT_header));
   queueMsg(&msg, sizeof(FASIT_2111));

FUNCTION_INT("::handle_100(int start, int end)", 0)
   return 0;
}

int MIT_Client::handle_2100(int start, int end) {
FUNCTION_START("::handle_2100(int start, int end)")

   // do handling of message
   IMSG("Handling 2100 in MIT\n");

   // TODO -- handle move commands here
   // TODO -- pass lift commands to SIT

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
   fillStatus(&msg); // fills in status values with current values

   // set fault
   msg.body.fault = htons(type);

   // send
   queueMsg(&hdr, sizeof(FASIT_header));
   queueMsg(&msg, sizeof(FASIT_2102));

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

   // TODO -- remember position value, send status if changed

FUNCTION_END("::didPosition(int pos)")
}

// start movement or change movement
void MIT_Client::doMove(int speed, int direction) { // speed in mph, direction 1 for forward, -1 for reverse, 0 for stop
FUNCTION_START("::doMove(int speed, int direction)")
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

   // TODO -- remember speed/direction values, send status if changed

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


/***********************************************************
*                      MIT_Conn Class                      *
***********************************************************/
MIT_Conn::MIT_Conn(struct nl_handle *handle, MIT_Client *client, int family) : NL_Conn(handle, client, family) {
FUNCTION_START("::MIT_Conn(struct nl_handle *handle, TCP_Client *client, int family) : NL_Conn(handle, client, family)")

   sit_client = client;

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
      case NL_C_BATTERY:
         genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

         if (attrs[GEN_INT8_A_MSG]) {
            // received change in battery value
            int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
            sit_client->didBattery(value); // tell client
         }
         break;
      case NL_C_STOP:
         // received emergency stop response
         sit_client->didStop(); // tell client
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

