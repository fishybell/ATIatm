#ifndef _TIMERS_H_
#define _TIMERS_H_

#include "timer.h"
#include "fasit_tcp.h"

// constant values for periodic timeouts
#define CONNECTCLIENT 200

// calls FASIT_TCP::newClient
class ConnTimer : public TimeoutTimer {
public :
   ConnTimer(FASIT_TCP *server, int msec); // starts the timeout timer and installs with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately
private :
   FASIT_TCP *server;
};

#endif
