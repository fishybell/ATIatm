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

