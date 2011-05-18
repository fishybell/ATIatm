#ifndef _TIMERS_H_
#define _TIMERS_H_

#include "timer.h"
#include "fasit_tcp.h"
#include "nl_conn.h"
#include "tcp_client.h"

// constant values for periodic timeouts
#define CONNECTCLIENT 200
#define RETRYNETLINK 200
#define RECONNECTION 10000

// calls FASIT_TCP::newClient
class ConnTimer : public TimeoutTimer {
public :
   ConnTimer(FASIT_TCP *server, int msec); // starts the timeout timer and installs with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately
private :
   FASIT_TCP *server;
};

// calls NL_Conn::makeWritable
class NLTimer : public TimeoutTimer {
public :
   NLTimer(NL_Conn *conn, int msec); // starts the timeout timer and install with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately
private:
   NL_Conn *conn;
};

// calls TCP_Client::handleReconnect() to attempt a reconnection
class ReconTimer : public TimeoutTimer {
public :
   ReconTimer(TCP_Client *client, int msec); // starts the timeout timer and install with the global timer
   virtual void handleTimeout(); // Timeout class determines if this timer has timed out and calls this appropriately
private:
   TCP_Client *client;
};

#endif
