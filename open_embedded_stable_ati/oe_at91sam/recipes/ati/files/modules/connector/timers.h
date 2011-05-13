#ifndef _TIMERS_H_
#define _TIMERS_H_

#include "timer.h"
#include "nl_conn.h"

// constant values for periodic timeouts
#define RETRYNETLINK 200

// calls NL_Conn::makeWritable
class NLTimer : public TimeoutTimer {
public :
   NLTimer(NL_Conn *conn, int msec); // starts the timeout timer and install with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately
private:
   NL_Conn *conn;
};

#endif
