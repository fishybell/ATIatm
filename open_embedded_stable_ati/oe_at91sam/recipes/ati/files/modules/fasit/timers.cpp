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

/********************************************
 * ReconTimer timer                          *
 *******************************************/
ReconTimer::ReconTimer(TCP_Client *client, int msec) : TimeoutTimer(msec) {
FUNCTION_START("::ReconTimer(int msec)")
   // set the client
   this->client = client;
   // set the type and push to the main timer class
   type = recon_timer;
   push();
FUNCTION_END("::ReconTimer(int msec)")
}

void ReconTimer::handleTimeout() {
FUNCTION_START("::handleTimeout()")
   client->handleReconnect();
FUNCTION_END("::handleTimeout()")
}

