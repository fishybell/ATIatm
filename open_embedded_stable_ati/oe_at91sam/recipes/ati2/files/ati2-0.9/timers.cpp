#include "common.h"
#include "timers.h"
#include "timeout.h"
#include "fasit_tcp.h"
#include "tcp_factory.h"
#include "fasit_serial.h"

// static declarations
__uint8_t HeartBeat::sequence = 0;
bool HeartBeat::valid = false;
DeadHeart *DeadHeart::deadbeat = NULL;

/********************************************
 * Bake2100 timer                           *
 *******************************************/
Bake2100::Bake2100(int msec) : TimeoutTimer(msec) {
FUNCTION_START("::Bake2100(int msec)")
   // set the type and push to the main timer class
   type = b2100;
   push();
FUNCTION_END("::Bake2100(int msec)")
}

void Bake2100::handleTimeout() {
FUNCTION_START("::handleTimeout()")
   FASIT_TCP::Bake_2100();
FUNCTION_END("::handleTimeout()")
}

/********************************************
 * HeartBeat timer                          *
 *******************************************/
HeartBeat::HeartBeat(int msec) : TimeoutTimer(msec) {
FUNCTION_START("::HeartBeat(int msec)")
   // if we're sending the heartbeat, then we're valid
   valid = true;

   // set the type and push to the main timer class
   type = heartBeat;
   push();
FUNCTION_END("::HeartBeat(int msec)")
}

void HeartBeat::handleTimeout() {
FUNCTION_START("::handleTimeout()")
   // send the periodic heartbeat
   ATI_16007 msg;
   msg.sequence = sequence++;
   ATI_header hdr = FASIT::createHeader(16007, UNASSIGNED, &msg, sizeof(ATI_16007)); // source unassigned for 16007
   SerialConnection::queueMsgAll(&hdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&msg, sizeof(ATI_16007));
   IMSG("HeartBeat sent\n")

   // start another HeartBeat timer
   new HeartBeat(HEARTBEAT);

FUNCTION_END("::handleTimeout()")
}

void HeartBeat::heartBeatReceived(bool valid) {
FUNCTION_START("::heartBeatReceived(bool valid)")
   // set validity
   HeartBeat::valid = valid;

   // if valid, tell DeadHeart timer
   if (valid) {
      IMSG("HeartBeat received\n")
      DeadHeart::receivedBeat();
   } else {
      IMSG("HeartBeat dead\n")
   }
FUNCTION_END("::heartBeatReceived(bool valid)")
}

/********************************************
 * DeadHeart timer                          *
 *******************************************/
DeadHeart::DeadHeart(int msec) : TimeoutTimer(msec) {
FUNCTION_START("::DeadHeart(int msec)")
   // set the type and 
   type = deadHeart;

   // push to the main timer class if we have a timeout
   if (msec > 0) { push(); }
FUNCTION_END("::DeadHeart(int msec)")
}

void DeadHeart::handleTimeout() {
FUNCTION_START("::handleTimeout()")
   // invalidate heartbeat
   HeartBeat::heartBeatReceived(false);

   // cancel connections
   FASIT_TCP::clearSubscribers();

   // don't restart timer, if we get a heartbeat signal it will start then
FUNCTION_END("::handleTimeout()")
}

void DeadHeart::startWatching() {
FUNCTION_START("::startWatching()")
   deadbeat = new DeadHeart(DEADHEART);
FUNCTION_END("::startWatching()")
}

void DeadHeart::receivedBeat() {
FUNCTION_START("::receivedBeat()")
   // clear out the old timer...
   Timeout::clearTimeout(deadbeat);

   // ...and start a new one
   startWatching();
FUNCTION_END("::receivedBeat()")
}

/********************************************
 * Resubscribe timer                        *
 *******************************************/
Resubscribe::Resubscribe(int msec) : TimeoutTimer(msec) {
FUNCTION_START("::Resubscribe(int msec)")
   // set the type and push to the main timer class
   type = resubscribe;
   push();
FUNCTION_END("::Resubscribe(int msec)")
}

void Resubscribe::handleTimeout() {
FUNCTION_START("::handleTimeout()")
   FASIT_TCP_Factory::SendResubscribe();
FUNCTION_END("::handleTimeout()")
}

/********************************************
 * SerialWrite timer                        *
 *******************************************/
SerialWrite::SerialWrite(SerialConnection *serial, int msec) : TimeoutTimer(msec) {
FUNCTION_START("::SerialWrite(SerialConnection *serial, int msec)")
   this->serial = serial;

   // set the type and push to the main timer class
   type = serialWrite;
   push();
FUNCTION_END("::SerialWrite(SerialConnection *serial, int msec)")
}

void SerialWrite::handleTimeout() {
FUNCTION_START("::handleTimeout()")
   serial->makeWritable(true); // if we're still not close enough this will create another timer
FUNCTION_END("::handleTimeout()")
}

/********************************************
 * ChangeChannel timer                      *
 *******************************************/
ChangeChannel::ChangeChannel(int channel, int msec) : TimeoutTimer(msec) {
FUNCTION_START("::ChangeChannel(int channel, int msec)")
   this->channel = channel;

   // set the type and push to the main timer class
   type = changeChannel;
   push();
FUNCTION_END("::ChangeChannel(int channel, int msec)")
}

void ChangeChannel::handleTimeout() {
FUNCTION_START("::handleTimeout()")
   // change all radios to the new channel
   SerialConnection::changeAllChannels(channel);
FUNCTION_END("::handleTimeout()")
}

/********************************************
 * SendChangeChannel timer                  *
 *******************************************/
SendChangeChannel::SendChangeChannel(int source, ATI_16005 msg, int count, int msec) : TimeoutTimer(msec) {
FUNCTION_START("::SendChangeChannel(int source, ATI_16005 msg, int count, int msec)")
   this->source = source;
   this->msg = msg;
   this->count = count;

   // set the type and push to the main timer class
   type = changeChannel;
   push();
FUNCTION_END("::SendChangeChannel(int source, ATI_16005 msg, int count, int msec)")
}

void SendChangeChannel::handleTimeout() {
FUNCTION_START("::handleTimeout()")
   // create header
   ATI_header hdr = FASIT::createHeader(16005, source, &msg, sizeof(ATI_16005));

   // send message on all serial devices
   SerialConnection::queueMsgAll(&hdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&msg, sizeof(ATI_16005));
   
   // send again?
   if (--count) {
      new SendChangeChannel(source, msg, count, SENDCHANGECHANNEL);
   }
FUNCTION_END("::handleTimeout()")
}

