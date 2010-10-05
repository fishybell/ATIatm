#ifndef _TIMERS_H_
#define _TIMERS_H_

#include "timer.h"

// various timout timer definitions

// constant values for periodic timeouts
#define HEARTBEAT 40000
#define FIRSTHEARTBEAT 200
#define RESUBSCRIBE 50
#define DEADHEART 50000
#define SERIALWAIT 10000

// calls FASIT_TCP::Bake_2100
class Bake2100 : public TimeoutTimer {
public :
   Bake2100(int msec); // starts the timeout timer and installs with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately
};

// sends out a periodic HeartBeat message
class HeartBeat : public TimeoutTimer {
public :
   HeartBeat(int msec); // starts the timeout timer and installs with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately

   static void heartBeatReceived(bool valid); // called when the heartbeat message is received (true) or when it timed out (false)
   static bool haveHeartBeat() { return valid; } // returns whether or not there is a valid heartbeat
private :
   static __uint8_t sequence;
   static bool valid;
};

// kills connections if the periodic heartbeat hasn't been seen before the timer times out
class DeadHeart : public TimeoutTimer {
public :
   DeadHeart(int msec); // starts the timeout timer and installs with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately

   static void startWatching(); // start watching for a heartbeat signal
   static void receivedBeat(); // caught a heartbeat signal, reset the timer

private :
   static DeadHeart *deadbeat;
};

// sends out the resubscribe message
class Resubscribe : public TimeoutTimer {
public :
   Resubscribe(int msec); // starts the timeout timer and installs with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately
};

// enables a serial port for writing at a specified timeout
class SerialWrite : public TimeoutTimer {
public :
   SerialWrite(class SerialConnection *serial, int msec); // starts the timeout timer and installs with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately

private :
   SerialConnection *serial;
};

#endif
