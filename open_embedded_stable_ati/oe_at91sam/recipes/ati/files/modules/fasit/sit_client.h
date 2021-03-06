#ifndef _SIT_CLIENT_H_
#define _SIT_CLIENT_H_

using namespace std;

#include "fasit.h"
#include "connection.h"
#include "common.h"
#include "fasit_tcp.h"
#include "nl_conn.h"
#include "netlink_user.h"

extern int start_config;

// class for FASIT client
// parses SIT messages
class SIT_Client : public TCP_Client {
public :
   SIT_Client(int fd, int tnum, bool armor);
   virtual ~SIT_Client();

   // individual commands
   void didFailure(int type); // experienced failure "type"
   void doConceal(); // change position to conceal
   void didConceal(); // position changed to conceal
   void doExpose(); // change position to expose
   void didExpose(); // position changed to expose
   void doMoving(); // get the current position
   void didMoving(); // position changed to moving
   void doShutdown(); // shutdown target
   void doSleep(); // sleep target
   void doWake(); // wake target
   void doHitCountReset(); // wake target
   void doBattery(); // retrieve battery value
   void didBattery(int val); // current battery value
   void didFault(int val); // current fault value
   void doStop(); // emergency stop (stops accessories as well)
   void didStop(); // received immediate stop response
   void doHitCal(struct hit_calibration hit_c); // change hit calibration data
   void didHitCal(struct hit_calibration hit_c); // current hit calibration data
   void getHitCal(struct hit_calibration *hit_c); // get last remembered hit calibration data
   void getAcc_C(struct accessory_conf *acc_c); // get last remembered hit calibration data
   void doHits(int num); // change received hits to "num" (usually to 0 for reset)
   void didHits(int num); // received "num" hits
   void didMagnitude(int mag); // received magnitude
   void doMSDH(int code, int ammo, int player, int delay); // change MSDH data
   void didMSDH(int code, int ammo, int player, int delay); // current MSDH data
   void doMFS(int on, int mode, int idelay, int rdelay); // change MFS data   
   void didMFS(int exists,int on, int mode, int idelay, int rdelay) ;
   void doMGL(int on);
   void didMGL(int exists,int on);
   void doPHI(int on);
   void didPHI(int exists,int on);
   void doTherm(int on);
   void didTherm(int exists,int on);
   void doBESFire(int zone);
   void doBlank(int blank);
   void doBob(int bob);
   void doGPS(); // retrieve gps data
   void didGPS(struct gps_conf gpc_c); // current gps data


protected:
   virtual bool hasPair() { return nl_conn != NULL;};
   virtual Connection *pair() { IERROR("Called SIT_Client::pair()"); return NULL; }; // will cause us an error if called, this TCP_Client must never have the standard handle_### messages called to prevent this

   // individual fasit message handlers, all return -1 if the connection needs to be
   //   deleted afterwards
   // the message data itself is in the read buffer from start to end
   int handle_100(int start, int end);
   int handle_2000(int start, int end);
   int handle_2004(int start, int end);
   int handle_2005(int start, int end);
   int handle_2006(int start, int end);
   int handle_2100(int start, int end);
   int handle_2101(int start, int end);
   int handle_2111(int start, int end);
   int handle_2102(int start, int end);
   int handle_2114(int start, int end);
   int handle_2115(int start, int end);
   int handle_2110(int start, int end);
   int handle_2112(int start, int end);
   int handle_2113(int start, int end);
   int handle_13000(int start, int end);
   int handle_13002(int start, int end);
   int handle_13110(int start, int end);
   int handle_13112(int start, int end);
   int handle_14110(int start, int end);
   int handle_14112(int start, int end);
   int handle_14200(int start, int end);
   int handle_14400(int start, int end);
   int handle_14401(int start, int end);
   int handle_15110(int start, int end);
   int handle_15112(int start, int end);
   
private:
   class SIT_Conn *nl_conn;

   // re-initialize to default settings
   void reInit();
   void Reset();

   // helper functions for filling out a 2102 status message
   void fillStatus2102(FASIT_2102 *msg);
   void sendStatus2102(int force);
   void setStatus2112(int on, int mode, int idelay, int rdelay);
   void sendStatus2112();
   void sendStatus13002(int tobob);
   void sendStatus13112(int on);
   void sendStatus14112(int on);
   void sendStatus15112(int on);
   void sendStatus15132(int mag);

   // remember the last command we received for responses back
   int resp_num;
   int resp_seq;

   // remember if we ever connected
   bool ever_conn;
   int skippedFault;

   // remember the battery value rather than send a response each time we get a change
   int lastBatteryVal;

   // remember if the target was asleep or awake, default is awake
   int lastWakeVal;

   // remember data to send back over and over again
   struct hit_calibration lastHitCal;
   __uint16_t fake_sens; // to remember their unreasonable sensitivity values
   struct accessory_conf acc_conf;
   int exposure;
   int hits;

   // place to save a copy of the last status message, so we can check for a change
   struct FASIT_2102b lastMsgBody;
   struct FASIT_2112b lastMFSBody;
   int sendMFSStatus;
   
   // hit calibration table (ours to theirs and back)
   static const u32 cal_table[16];
};

// for handling the kernel connection for the SIT
class SIT_Conn : public NL_Conn {
public:
   SIT_Conn(struct nl_handle *handle, class SIT_Client *client, int family); // don't call directly, call via NL_Conn::newConn
   virtual ~SIT_Conn(); // closes, cleans up, etc.
   virtual int parseData(struct nl_msg *msg); // call the correct handler in the SIT_Client

   // individual SIT commands to send to kernel
   void doConceal(); // change position to conceal
   void doExpose(); // change position to expose
   void doMoving(); // get the current position
   void doShutdown(); // shutdown target
   void doSleep(); // sleep target
   void doWake(); // wake target
   void doHitCountReset(); // wake target
   void doBattery(); // retrieve battery value
   void doStop(); // immediate stop (stops accessories as well)
   void doHitCal(struct hit_calibration hit_c); // change hit calibration data
   void doHits(int num); // change received hits to "num" (usually to 0 for reset)
   void doMSDH(int code, int ammo, int player, int delay); // change MSDH data
   void doMFS(int on, int mode, int idelay, int rdelay); // change MFS data
   void didMFS(int exists,int on, int mode, int idelay, int rdelay) ;
   void doMGL(int on);
   void didMGL(int exists,int on);
   void doPHI(int on);
   void didPHI(int exists,int on);
   void doTherm(int on);
   void didTherm(int exists,int on);
   void doBESFire(int zone);
   void doGPS(); // retrieve gps dataprotected:


private:
   SIT_Client *sit_client;
};

#endif
