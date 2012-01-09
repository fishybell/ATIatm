#ifndef _TIMERS_H_
#define _TIMERS_H_

#include "timer.h"
#include "nl_conn.h"
#include "kernel_tcp.h"

// constant values for periodic timeouts
#define RETRYNETLINK 200
#define RECONNECTION 10000

// calls NL_Conn::makeWritable
class NLTimer : public TimeoutTimer {
public :
   NLTimer(NL_Conn *conn, int msec); // starts the timeout timer and install with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately
private:
   NL_Conn *conn;
};

// calls Kernel_TCP::sendRole
class SRTimer : public TimeoutTimer {
public :
   SRTimer(Kernel_TCP *conn, int msec); // starts the timeout timer and install with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately
private:
   Kernel_TCP *conn;
};

// calls Kernel_TCP::handleReconnect() to attempt a reconnection
class ReconTimer : public TimeoutTimer {
public :
   ReconTimer(Kernel_TCP *client, int msec); // starts the timeout timer and install with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately
private:
   Kernel_TCP *client;
};

#endif
