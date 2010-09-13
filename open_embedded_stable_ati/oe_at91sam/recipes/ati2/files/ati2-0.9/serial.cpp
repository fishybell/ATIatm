using namespace std;

#include "serial.h"
#include "common.h"
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
      flink = this;
   } else {
      // we're last (find old last and link from there)
      SerialConnection *tlink = flink;
      while(tlink->link != NULL) {
         tlink = tlink->link;
      }
      tlink->link = this;
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
   newtio.c_iflag = IGNPAR;
   newtio.c_oflag = 0;
   newtio.c_lflag = ICANON;    /* set input mode (canonical, no echo,...) */
   newtio.c_cc[VTIME] = 0;     /* inter-character timer unused */
   newtio.c_cc[VMIN] = 5;      /* blocking read until 5 chars received */
   tcflush (fd, TCIFLUSH);
   tcsetattr (fd, TCSANOW, &newtio);
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
   while(tlink->link != this) {
      tlink = tlink->link;
   }
   tlink->link = this->link; // connect neighbors in list (works for end of list too)
FUNCTION_END("::~SerialConnection()")
}

// queues a message for all serial devices
void SerialConnection::queueMsgAll(char *msg, int size) {
FUNCTION_START("::queueMsgAll(char *msg, int size)")
   // no serial connection to send to
   if (flink == NULL) {
      return;
   }

   // queue to first one
   flink->queueMsg(msg, size);

   // queue to the rest
   SerialConnection *tlink;
   while ((tlink = flink->link) != NULL) {
      tlink->queueMsg(msg, size);
   }
FUNCTION_END("::queueMsgAll(char *msg, int size)")
}

// the serial line needs to wait for the right time, it then sends its data uing the parent function
int SerialConnection::handleWrite(epoll_event *ev) {
FUNCTION_START("::handleWrite(epoll_event *ev)")
   int sec = delay / 1000;
   int nows, nowm; // current time in seconds and milliseconds
   int diff = nowm - ((nows - last_time_s) * 1000 + last_time_m); // time differential in milliseconds
   timeNow(&nows, &nowm);

   // wsize is 0, need to adjust epoll and return
   if (wsize <= 0) {
      return Connection::handleWrite(ev);
   }

   // have we reached our allotted time?
   if (delay > diff) {
      // are we past our allotted time?
      if (diff >= mdelay) {
         if (retries > 2) {
            // too many retries, delay a random amount of time one last time so as to not send at the same time as other radios
            timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = rand() % 20000; // 20 millisecond max delay
            nanosleep(&ts, NULL);
FUNCTION_INT("::handleWrite(epoll_event *ev)", 0)
            return 0; // wait again to make sure the device is ready for writing
         } else if (retries > 3) {
            // too many retries, send everything now come hell or high water
            resetDelay();
            int ret = Connection::handleWrite(ev);
FUNCTION_INT("::handleWrite(epoll_event *ev)", ret)
            return ret;
         } else {
            // retry again at our next slot
            delay = mdelay + rdelay;
            mdelay += rdelay + (mdelay - delay);
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
            delay = mdelay + rdelay;
            mdelay += rdelay + (mdelay - delay);

            // did we max out the radio's buffer?
            if (charMax == 256) {
               addDelay(427);
            }

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
   if (nows - last_time_s < sec || (nows - last_time_s == sec && sec > 1)) {
      // at least two second delay, sleep for one second and return to fight again later
      sleep(1);
FUNCTION_INT("::handleWrite(epoll_event *ev)", 0)
      return 0;
   } else if (nows - last_time_s == sec && sec > 0) {
      // at least one second delay, sleep for half a second and return to fight again later
      usleep(500);
FUNCTION_INT("::handleWrite(epoll_event *ev)", 0)
      return 0;
   } else {
      int rest = delay - diff;
      timespec ts;
      ts.tv_sec = 0;
      if (rest > 35) {
         // delay is more than 35 milliseconds away, sleep half of that and return to fight again later
         ts.tv_nsec = rest * 500; // half of 1000 nanoseconds per millisecond
         nanosleep(&ts, NULL);
FUNCTION_INT("::handleWrite(epoll_event *ev)", 0)
         return 0;
      } else {
         // the time is now very soon, sleep all but 2 milliseconds and return to fight again later
         // the 2 milliseconds is so epoll has enough time to re-poll the device (the radio may stop being ready)
         ts.tv_nsec = (rest-2) * 1000; // 1000 nanoseconds per millisecond
         nanosleep(&ts, NULL);
FUNCTION_INT("::handleWrite(epoll_event *ev)", 0)
         return 0;
      }
   }
}

// called to set the current time as the time to start delays from
void SerialConnection::setTimeNow() {
FUNCTION_START("::setTimeNow()")
   timeNow(&last_time_s, &last_time_m);
FUNCTION_END("::setTimeNow()")
}

// resets delay related variables and the last send time variables
void SerialConnection::resetDelay() {
FUNCTION_START("::resetDelay()")
   charMax = 256;
   delay = (3 * getTnum() % 512) + (getTnum() > 512 ? 2 : 0);
   mdelay = rdelay = retries = 0;
   setTimeNow();
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


