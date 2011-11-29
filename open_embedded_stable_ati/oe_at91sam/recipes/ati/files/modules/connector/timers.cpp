#include "common.h"
#include "timers.h"
#include "timeout.h"

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

/********************************************
 * Send Role timer                          *
 *******************************************/
SRTimer::SRTimer(Kernel_TCP *conn, int msec) : TimeoutTimer(msec) {
FUNCTION_START("::SRTimer(int msec)")
   // set the connection
   this->conn = conn;
   // set the type and push to the main timer class
   type = nl_timer;
   push();
FUNCTION_END("::SRTimer(int msec)")
}

void SRTimer::handleTimeout() {
FUNCTION_START("::handleTimeout()")
   conn->sendRole();
FUNCTION_END("::handleTimeout()")
}
