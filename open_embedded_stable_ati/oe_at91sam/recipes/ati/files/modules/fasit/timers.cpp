#include "common.h"
#include "timers.h"
#include "timeout.h"

/********************************************
 * ConnTimer timer                          *
 *******************************************/
ConnTimer::ConnTimer(FASIT_TCP *server, int msec) : TimeoutTimer(msec) {
FUNCTION_START("::ConnTimer(int msec)")
   // set the server
   this->server = server;
   // set the type and push to the main timer class
   type = conn_timer;
   push();
FUNCTION_END("::ConnTimer(int msec)")
}

void ConnTimer::handleTimeout() {
FUNCTION_START("::handleTimeout()")
   server->newClient();
FUNCTION_END("::handleTimeout()")
}
/********************************************
 * NLTimer timer                          *
 *******************************************/
NLTimer::NLTimer(NL_Conn *conn, int msec) : TimeoutTimer(msec) {
FUNCTION_START("::NLTimer(int msec)")
   // set the connection
   this->conn = conn;
   // set the type and push to the main timer class
   type = nl_timer;
   push();
FUNCTION_END("::NLTimer(int msec)")
}

void NLTimer::handleTimeout() {
FUNCTION_START("::handleTimeout()")
   conn->makeWritable(true);
FUNCTION_END("::handleTimeout()")
}

