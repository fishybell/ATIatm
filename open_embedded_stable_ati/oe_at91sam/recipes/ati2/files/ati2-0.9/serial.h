#ifndef _SERIAL_H_
#define _SERIAL_H_

#include "connection.h"

class SerialConnection : public Connection {
public:
   SerialConnection(char *fname); // a new connection always starts with a file name
   virtual ~SerialConnection(); // restores state of serial device

   static void queueMsgAll(char *msg, int size); // queues a message for all serial devices
   static void queueMsgAll(void *msg, int size) { queueMsgAll((char*)msg, size); }
   virtual void makeWritable(bool writable); // checks timers and potentially queues this action for later

   static void nextDelay(int msecs); // sets a wait time for all serial devices for after they send their next message
   static void nowDelay(int msecs); // sets a wait time for all serial devices for before they send their next message

   static void changeAllChannels(int channel); // change each serial radio's channel
   void changeChannel(int channel); // change this serial radio's channel
   
   void setTimeNow(); // called to set the current time as the time to start delays from
   void addDelay(int delay); // called to increase the delay required before the next time I can send data (in milliseconds)
   void minDelay(int delay); // called to set the minimum delay required before the next time I can send data (in milliseconds)
   void timeslot(int delay); // called to set the maximum amount of time (from the minimum delay time) allotted that I can send the data in (in milliseconds)
   void retryDelay(int delay); // called to set the delay time required before retrying if the delay time is missed (in milliseconds)
   void setMaxChar(int max); // sets the maximum characters allowed to be sent in the next delay slot

private:
   int handleWrite(epoll_event *ev); // overrides (and then calls) Connection::handleWrite

   // for linked list
   SerialConnection *link; // link to next
   static SerialConnection *flink; // link to first

   // precise specific functions
   static void timeNow(int *sec, int *msec); // sets the current time in seconds and milliseconds to the given variables
   void resetDelay(); // resets current time and delay variables
   void handleNextDelay(); // resets timer based on the ndelay variable

   // precise timing specific variables
   int charMax; // maximum amount of characters allowed in the allotted timeslot
   int mdelay; // max delay since last time we sent (in milliseconds)
   int delay; // min delay since last time we sent (in milliseconds)
   int rdelay; // retry delay time needed if timeslot is missed (in milliseconds)
   int retries; // amount of times retrying to send the data
   int last_time_s; // the last time we sent (seconds part)
   int last_time_m; // the last time we sent (milliseconds part)
   struct termios oldtio; // the original state of the serial device
   int ndelay; // delay for after the next message is sent
   int wdelay; // delay for before the next message is sent
};

#endif
