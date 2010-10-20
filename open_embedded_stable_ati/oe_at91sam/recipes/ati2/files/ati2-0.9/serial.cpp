using namespace std;

#include "serial.h"
#include "common.h"
#include "timers.h"
#include "radio.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>

#define BAUDRATE B19200

// define static members
SerialConnection *SerialConnection::flink = NULL;

SerialConnection::SerialConnection(char *fname) : Connection(0) {
FUNCTION_START("::SerialConnection(char *fname) : Connection(0)")
   struct termios newtio;

   // initialize place in linked list
   link = NULL;
   if (flink == NULL) {
      // we're first
DMSG("first serial link in chain 0x%08x\n", this);
      flink = this;
   } else {
      // we're last (find old last and link from there)
      SerialConnection *tlink = flink;
      while(tlink->link != NULL) {
         tlink = tlink->link;
      }
      tlink->link = this;
DMSG("last serial link in chain 0x%08x\n", this);
   }

   // initialize local variables
   resetDelay();

   // open file and setup the serial device (copy the old settings to oldtio)
   fd = open(fname, O_RDWR | O_NOCTTY);
   if (fd < 0) {
      IERROR("Could not open %s\n", fname)
      return;
   }
   tcgetattr (fd, &oldtio);
   memset (&newtio, 0, sizeof (newtio));
   newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;
   newtio.c_iflag = 0;
   newtio.c_oflag = 0;
   newtio.c_lflag = 0;
   newtio.c_cc[VTIME] = 0;     /* inter-character timer unused */
   newtio.c_cc[VMIN] = 1;      /* read a minimum of 1 character */
   tcflush (fd, TCIFLUSH);
   tcsetattr (fd, TCSANOW, &newtio);
   setnonblocking(fd);
FUNCTION_END("::SerialConnection(char *fname) : Connection(0)")
}

SerialConnection::~SerialConnection() {
FUNCTION_START("::~SerialConnection()")
   if (fd > 0) {
      tcsetattr (fd, TCSANOW, &oldtio);
   }

   // am I the sole SerialConnection?
   if (link == NULL && flink == this) {
      flink = NULL;
   }

   // remove from linked list
   SerialConnection *tlink = flink;
   while (tlink != NULL) {
      if (tlink->link == this) {
         tlink->link = this->link; // connect to next link in chain (if last, this drops this link off chain)
         break;
      }
   }

FUNCTION_END("::~SerialConnection()")
}

// queues a message for all serial devices
void SerialConnection::queueMsgAll(char *msg, int size) {
FUNCTION_START("::queueMsgAll(char *msg, int size)")
   // no serial connection to send to
   if (flink == NULL) {
DMSG("no links");
FUNCTION_END("::queueMsgAll(char *msg, int size)")
      return;
   }

   // queue to first one
DMSG("queueing %i bytes on first 0x%08x\n", size, flink)
   flink->queueMsg(msg, size);

   // queue to the rest
   SerialConnection *tlink;
   while ((tlink = flink->link) != NULL) {
DMSG("queueing %i bytes on link 0x%08x\n", size, tlink)
      tlink->queueMsg(msg, size);
   }
FUNCTION_END("::queueMsgAll(char *msg, int size)")
}

// checks timers and potentially queues this action for later
void SerialConnection::makeWritable(bool writable) {
FUNCTION_START("::makeWritable(bool writable)")
   // turning off writable? just do it now
   if (!writable) {
      Connection::makeWritable(false);
FUNCTION_END("::makeWritable(bool writable)")
      return;
   }

   // are we receiving an active heartbeat? if not give up
   if (!HeartBeat::haveHeartBeat()) {
      Connection::makeWritable(false); // double check that we're not writable
FUNCTION_END("::makeWritable(bool writable)")
      return;
   }

   // determine time to wait based on last time, delay and mdelay
   int sec = delay / 1000;
   int nows, nowm; // current time in seconds and milliseconds
   timeNow(&nows, &nowm);
   int diff = ((nows - last_time_s) * 1000) + (nowm - last_time_m); // time differential in milliseconds

   // are we needing to delay? schedule for later
   if (delay > diff) {
DMSG("serial write delay: %i; diff: %i from (%i,%i) : (%i,%i)\n", delay, diff, nows, nowm, last_time_s, last_time_m)
      // wait the remaining time, leaving 5 milliseconds at the end
      if ((delay - diff) > 5) {
         new SerialWrite(this, delay - diff - 5);
         Connection::makeWritable(false); // double check that we're not writable
FUNCTION_END("::makeWritable(bool writable)")
         return;
      }
   }

   // no timeout required? make writable now
   Connection::makeWritable(true);
FUNCTION_END("::makeWritable(bool writable)")
}

// the serial line needs to wait for the right time, it then sends its data uing the parent function
int SerialConnection::handleWrite(epoll_event *ev) {
FUNCTION_START("::handleWrite(epoll_event *ev)")
   int nows, nowm; // current time in seconds and milliseconds
   timeNow(&nows, &nowm);
   int diff = ((nows - last_time_s) * 1000) + (nowm - last_time_m); // time differential in milliseconds

   // wsize is 0, need to adjust epoll and return
   if (wsize <= 0) {
      int ret = Connection::handleWrite(ev);
FUNCTION_INT("::handleWrite(epoll_event *ev)", ret)
      return ret;
   }

DMSG("MinDelay: %i, MaxDelay: %i, Diff: %i\n", delay, mdelay, diff);
   // have we reached our allotted time?
   if (delay <= diff) {
      // are we past our allotted time?
      if (mdelay > 0 && diff >= mdelay) {
         if (retries > 3) {
DMSG("retry way too many times: %i\n", retries)
            // too many retries, send everything now come hell or high water
            resetDelay();
            int ret = Connection::handleWrite(ev);
FUNCTION_INT("::handleWrite(epoll_event *ev)", ret)
            return ret;
         } else {
DMSG("retry later: %i\n", retries)
            // retry again at our next slot
            int space = mdelay - delay;
            delay = mdelay + rdelay;
            mdelay = delay + space;
            retries++;
FUNCTION_INT("::handleWrite(epoll_event *ev)", 0)
            return 0;
         }
      } else {
         // we're at the sweet spot, send just the right amount of data
         if (wsize > charMax) {
            // create a new write buffer to be consumed by Connection::handleWrite
            char *tbuf = wbuf;
            wbuf = new char[charMax];
            memcpy(wbuf, tbuf, charMax);
            int twsize = wsize;
            wsize = charMax;

            // send the new buffer right now (consumed and freed)
            int ret = Connection::handleWrite(ev);

            // create a new write buffer containing just the next chunk of data
            wsize = twsize - charMax;
            wbuf = new char[wsize];
            memcpy(wbuf, tbuf+(sizeof(char) * charMax), wsize);

            // clean out temporary buffer
            delete [] tbuf;

            // set the next time to send
            int space = mdelay - delay;
            delay = mdelay + rdelay;
            mdelay = delay + space;

            // did we max out the radio's buffer?
            if (charMax == 256) {
               addDelay(427);
            }

            // count this as an attempt since we only sent some
            retries++;

FUNCTION_INT("::handleWrite(epoll_event *ev)", ret)
            return ret;
         } else {
            // send it all now
            resetDelay();

            // will we max out the radio's buffer?
            if (wsize == 256) {
               addDelay(427);
            }

            int ret = Connection::handleWrite(ev);
FUNCTION_INT("::handleWrite(epoll_event *ev)", ret)
            return ret;
         }
      }
   }

   // the time has not yet come, we should spend most of it sleeping
   makeWritable(true); // this will create a timeout for the remaining time and then mark the efd to watch for reads only
}

// called to set the current time as the time to start delays from
void SerialConnection::setTimeNow() {
FUNCTION_START("::setTimeNow()")
   timeNow(&last_time_s, &last_time_m);
FUNCTION_END("::setTimeNow()")
}

// sets a wait time for all serial devices for before they send their next message
void SerialConnection::nowDelay(int msecs) {
FUNCTION_START("::nextDelay()")
   SerialConnection *c = flink;
   while (c != NULL) {
      c->addDelay(msecs);
      c = c->link;
   };
FUNCTION_END("::nextDelay()")
}

// sets a wait time for all serial devices for after they send their next message
void SerialConnection::nextDelay(int msecs) {
FUNCTION_START("::nextDelay()")
   SerialConnection *c = flink;
   while (c != NULL) {
      c->ndelay = msecs;
      c = c->link;
   };
FUNCTION_END("::nextDelay()")
}

// resets timer based on the nextDelay variable
void SerialConnection::handleNextDelay() {
FUNCTION_START("::handleNextDelay()")
   if (ndelay > 0) {
      addDelay(ndelay);
      ndelay = 0;
   }
FUNCTION_END("::handleNextDelay()")
}

// resets delay related variables and the last send time variables
void SerialConnection::resetDelay() {
FUNCTION_START("::resetDelay()")
   charMax = 256;
   delay = (3 * getTnum() % 512) + (getTnum() > 512 ? 2 : 0);
   mdelay = rdelay = retries = 0;
   setTimeNow();
   handleNextDelay();
FUNCTION_END("::resetDelay()")
}

// wrapper for gettimeofday
void SerialConnection::timeNow(int *sec, int *msec) {
FUNCTION_START("::timeNow(int *sec, int *msec)")
   timeval tv;
   gettimeofday(&tv, NULL);
   if (sec)  {*sec = tv.tv_sec;}
   if (msec) {*msec = tv.tv_usec / 1000;}
FUNCTION_END("::timeNow(int *sec, int *msec)")
}

// called to increase the delay required before the next time I can send data (in milliseconds)
void SerialConnection::addDelay(int delay) {
FUNCTION_START("::addDelay(int delay)")
   this->delay += delay;
   this->mdelay += delay;
FUNCTION_END("::addDelay(int delay)")
}

// called to set the minimum delay required before the next time I can send data (in milliseconds)
void SerialConnection::minDelay(int delay) {
FUNCTION_START("::minDelay(int delay)")
   this->delay = max(delay, this->delay);
FUNCTION_END("::minDelay(int delay)")
}

// called to set the maximum amount of time (from the minimum delay time) allotted that I can send the data in (in milliseconds)
void SerialConnection::timeslot(int delay) {
FUNCTION_START("::timeslot(int delay)")
   mdelay = this->delay + delay;
FUNCTION_END("::timeslot(int delay)")
}

// called to set the delay time required before retrying if the delay time is missed (in milliseconds)
void SerialConnection::retryDelay(int delay) {
FUNCTION_START("::retryDelay(int delay)")
   rdelay = delay;
FUNCTION_END("::retryDelay(int delay)")
}

// sets the maximum characters allowed to be sent in the next delay slot
void SerialConnection::setMaxChar(int max) {
FUNCTION_START("::setMaxChar(int max)")
   charMax = max;
FUNCTION_END("::setMaxChar(int max)")
}

// change each serial radio's channel
void SerialConnection::changeAllChannels(int channel) {
FUNCTION_START("::changeAllChannels(int channel)")
   // loop through linked list and change all available channels
   SerialConnection *tlink = flink;
   while (tlink != NULL) {
      tlink->changeChannel(channel); // TODO -- all change to the same channel?
      tlink = tlink->link;
   }
FUNCTION_END("::changeAllChannels(int channel)")
}

// change this serial radio's channel
void SerialConnection::changeChannel(int channel) {
FUNCTION_START("::changeChannel(int channel)")
   // change channel (will block for several seconds)
   Radio radio(fd);
   radio.changeChannel(channel);
FUNCTION_END("::changeChannel(int channel)")
}

