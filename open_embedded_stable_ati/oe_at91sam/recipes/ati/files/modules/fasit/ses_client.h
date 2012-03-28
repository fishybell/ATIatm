#ifndef _SES_CLIENT_H_
#define _SES_CLIENT_H_

using namespace std;

#include "fasit.h"
#include "connection.h"
#include "common.h"
#include "fasit_tcp.h"
#include "nl_conn.h"
#include "netlink_user.h"

extern int start_config;

#define SES_BUFFER_SIZE 2048
#define NO_LOOP 1
#define INFINITE_LOOP 0xFFFFFFFF

// class for FASIT client
// parses SES messages
class SES_Client : public TCP_Client {
public :
   SES_Client(int fd, int tnum);
   virtual ~SES_Client();

   // individual commands
   void didFailure(int type); // experienced failure "type"
   void doShutdown(); // shutdown target
   void doSleep(); // sleep target
   void doWake(); // wake target
   void doBattery(); // retrieve battery value
   void didBattery(int val); // current battery value
   void didFault(int val); // current fault value
   void doStop(); // emergency stop (stops accessories as well)
   void didStop(); // received immediate stop response

   void doPlayRecord(); // play or record over selected track
   void doPlay(); // play selected track
   void doRecord(); // record over selected track
   void doEncode(); // encode ongoing recording
   void doRecAbort(); // abort ongoing recording
   void doRecordButton(); // record over selected track (call repeatedly to step through full recording process)
   void didMode(int mode); // changed playback mode
   void doMode(int mode); // changes playback mode
   void doMode(); // get mode from kernel
   void doLoop(unsigned int loop); // set the loop value
   void doTrack(const char* track, int length); // select an arbirtrary track
   void doTrack(int track); // select a built-in track
   void doTrack(); // get track from kernel
   void doStream(const char* uri, int length); // stream audio from a selected uri
   void doStopPlay(); // stop playback/record
   void doCopyStart(const char* track, int length); // prepare for copying track
   void doCopyChunk(const char* chunk, int length); // write out file chunk
   void doCopyAbort(); // abort copying of track

   void finishedRecording(); // called from the recording/encoding processes to notify when it is done
   void sendStatus14401();

protected:
   virtual bool hasPair() { return nl_conn != NULL;};
   virtual Connection *pair() { IERROR("Called SES_Client::pair()"); return NULL; }; // will cause us an error if called, this TCP_Client must never have the standard handle_### messages called to prevent this

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
   class SES_Conn *nl_conn;

   // helper functions for filling out a 2102 status message
   void fillStatus2102(FASIT_2102 *msg);
   void sendStatus2102();

   // helper function for filling out a 14401 status message
   void fillStatus14401(FASIT_14401 *msg);

   // remember the last command we received for responses back
   int resp_num;
   int resp_seq;

   // playback values
   unsigned int loop; // loop count
   int mode; // playback mode
   char track[SES_BUFFER_SIZE]; // selected track
   char uri[SES_BUFFER_SIZE]; // selected stream uri
   int knob; // knob selection

   // copy values
   int copyMode; // mode before copying started
   FILE *copyfile; // file to copy data to

   // remember the battery value rather than send a response each time we get a change
   int lastBatteryVal;
};

// for handling the kernel connection for the SES
class SES_Conn : public NL_Conn {
public:
   SES_Conn(struct nl_handle *handle, class SES_Client *client, int family); // don't call directly, call via NL_Conn::newConn
   virtual ~SES_Conn(); // closes, cleans up, etc.
   virtual int parseData(struct nl_msg *msg); // call the correct handler in the SES_Client

   // individual SES commands to send to kernel
   void doShutdown(); // shutdown target
   void doSleep(); // sleep target
   void doWake(); // wake target
   void doBattery(); // retrieve battery value
   void doStop(); // immediate stop (stops accessories as well)
   void doMode(int mode); // changes playback mode
   void doMode(); // retrieve mode value
   void doTrack(); // retrieve track value

private:
   SES_Client *ses_client;
};

#endif
