#ifndef _MIT_CLIENT_H_
#define _MIT_CLIENT_H_

using namespace std;

#include "fasit.h"
#include "connection.h"
#include "common.h"
#include "fasit_tcp.h"
#include "nl_conn.h"
#include "netlink_user.h"

// class for FASIT client
// parses MIT messages
class MIT_Client : public TCP_Client {
public :
   MIT_Client(int fd, int tnum);
   virtual ~MIT_Client();

   // SIT attachment commands
   bool hasSIT(); // returns true when a FASIT_TCP object has been successfully attached as a SIT
   FASIT_TCP *addSIT(int fd); // attempt to attach a socket as a SIT
   void delSIT(); // the SIT disconnected

   // individual commands
   void didFailure(int type); // experienced failure "type"
   void doPosition(); // retrieve position value
   void didPosition(int pos); // current position value
   void doShutdown(); // shutdown the target
   void doExpose(int val); // tell mover the lifter state
   void doDock(); // dock the target
   void doSleep(); // sleep the target
   void doWake(); // wake the target
   void doMove(float speed, int direction); // start movement or change movement
   void doMove(); // retrieve movement values
   void doContinuousMove(float speed, int direction); // start movement or change movement
   void doContinuousMove(); // retrieve movement values
   void didMove(float speed, int direction); // current direction value
   void doBattery(); // retrieve battery value
   void didBattery(int val); // current battery value
   void didFault(int val); // current fault value
   void didExpose(int val); // current expose value
   void doStop(); // emergency stop (stops accessories as well)
   void didStop(); // received immediate stop response
   void Reset(); // received immediate stop response

   void handle_2111(FASIT_2111 *msg); // called from attached_SIT_Client object
   void handle_2102(FASIT_2102 *msg); // called from attached_SIT_Client object

   // overwrite so we can stop the mover on disconnect
   virtual bool reconnect();
protected:
   // individual fasit message handlers, all return -1 if the connection needs to be
   //   deleted afterwards
   // the message data itself is in the read buffer from start to end
   int handle_100(int start, int end);
//   int handle_2000(int start, int end); just forwarded
//   int handle_2004(int start, int end); just forwarded
//   int handle_2005(int start, int end); just forwarded
//   int handle_2006(int start, int end); just forwarded
   int handle_2100(int start, int end);
//   int handle_2101(int start, int end); just forwarded
//   int handle_2111(int start, int end); just forwarded
//   int handle_2102(int start, int end); just forwarded
//   int handle_2114(int start, int end); just forwarded
//   int handle_2115(int start, int end); just forwarded
//   int handle_2110(int start, int end); just forwarded
//   int handle_2112(int start, int end); just forwarded
//   int handle_2113(int start, int end); just forwarded

private:
   class MIT_Conn *nl_conn;
   class attached_SIT_Client *att_SIT;

   // helper functions for filling out a 2102 status message
   void fillStatus2102(FASIT_2102 *msg);
   void sendStatus2102();

   // remember the last command we received for responses back
   int resp_num;
   int resp_seq;

   // remember if we ever connected
   bool ever_conn;
   int skippedFault;

   // remember the battery value rather than send a response each time we get a change
   int lastBatteryVal;

   // remember data to send back over and over again
   int lastPosition;
   float lastSpeed;
   int lastDirection;
   FASIT_2111 lastSITdevcaps;
   FASIT_2102 lastSITstatus;
};

// class for FASIT client
// parses attached SIT messages
class attached_SIT_Client : public FASIT_TCP {
public :
   attached_SIT_Client(MIT_Client *mit, int fd, int tnum);
   virtual ~attached_SIT_Client();

   // public function to determine if a SIT is actually attached
   bool attached() { return hasPair(); }

   // for when a SIT isn't really connected this will not queue anything
   virtual void queueMsg(const char *msg, int size);
   void queueMsg(const void *msg, int size) {queueMsg((const char*)msg, size);} // auto-cast for various data pointers

protected:
   // individual fasit message handlers, all return -1 if the connection needs to be
   //   deleted afterwards
   // the message data itself is in the read buffer from start to end
//   int handle_100(int start, int end); just forwarded
//   int handle_2000(int start, int end); just forwarded
//   int handle_2004(int start, int end); just forwarded
//   int handle_2005(int start, int end); just forwarded
//   int handle_2006(int start, int end); just forwarded
//   int handle_2100(int start, int end); just forwarded
//   int handle_2101(int start, int end); just forwarded
   int handle_2111(int start, int end);
   int handle_2102(int start, int end);
//   int handle_2114(int start, int end); just forwarded
//   int handle_2115(int start, int end); just forwarded
//   int handle_2110(int start, int end); just forwarded
//   int handle_2112(int start, int end); just forwarded
//   int handle_2113(int start, int end); just forwarded

private:
   class MIT_Client *mit_client;
};

// for handling the kernel connection for the MIT
class MIT_Conn : public NL_Conn {
public:
   MIT_Conn(struct nl_handle *handle, class MIT_Client *client, int family); // don't call directly, call via NL_Conn::newConn
   virtual ~MIT_Conn(); // closes, cleans up, etc.
   virtual int parseData(struct nl_msg *msg); // call the correct handler in the MIT_Client

   // individual MIT commands to send to kernel
   void doPosition(); // retrieve position value
   void doShutdown(); // shutdown the target
   void doExpose(int val); // tell mover the lifter state
   void doDock(); // dock the target
   void doSleep(); // sleep the target
   void doWake(); // wake the target
   void doMove(float speed, int direction); // start movement or change movement
   void doMove(); // retrieve movement values
   void doContinuousMove(float speed, int direction); // start movement or change movement
   void doContinuousMove(); // retrieve movement values
   void doBattery(); // retrieve battery value
   void doStop(); // immediate stop (stops accessories as well)
   void didStop(); // received immediate stop response

private:
   MIT_Client *mit_client;
};

#endif
