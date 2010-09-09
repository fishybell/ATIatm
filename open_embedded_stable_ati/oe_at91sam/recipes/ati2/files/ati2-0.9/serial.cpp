using namespace std;

#include "serial.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <fcntl.h>

#define BAUDRATE B19200

// define static members
SerialConnection *SerialConnection::flink = NULL;

SerialConnection::SerialConnection(char *fname) : Connection(0) {
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
      fprintf(stderr, "Could not open %s\n", fname);
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
}

SerialConnection::~SerialConnection() {
   if (fd > 0) {
      tcsetattr (fd, TCSANOW, &oldtio);
   }

   // am I the sole SerialConnection?
   if (link == NULL && flink == this) {
      // TODO -- what to do when all serial connections close? exit program? shutdown? what?
      flink = NULL;
   }

   // remove from linked list
   SerialConnection *tlink = flink;
   while(tlink->link != this) {
      tlink = tlink->link;
   }
   tlink->link = this->link; // connect neighbors in list (works for end of list too)
}

// queues a message for all serial devices
void SerialConnection::queueMsgAll(char *msg, int size) {
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
}

// the serial line needs to wait for the right time, it then sends its data uing the parent function
int SerialConnection::handleWrite(epoll_event *ev) {
   int sec = delay / 1000;
   int nows, nowm; // current time in seconds and milliseconds
   int diff = nowm - ((nows - last_time_s) * 1000 + last_time_m); // time differential in milliseconds
   timeNow(&nows, &nowm);

   // TODO -- wsize is 0, need to adjust epoll and return

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
            return 0; // wait again to make sure the device is ready for writing
         } else if (retries > 3) {
            // too many retries, send everything now come hell or high water
            resetDelay();
            return Connection::handleWrite(ev);
         } else {
            // retry again at our next slot
            delay = mdelay + rdelay;
            mdelay += rdelay + (mdelay - delay);
            retries++;
            return 0;
         }
      } else {
         // we're at the sweet spot, send just the right amount of data
         if (wsize > charMax) {
            // create a new write buffer to be consumed by Connection::handleWrite
            char *tbuf = wbuf;
            wbuf = new char[charMax];
            memcpy(wbuf, tbuf, charMax);

            // send the new buffer right now (consumed and freed)
            int ret = Connection::handleWrite(ev);

            // create a new write buffer containing just the next chunk of data
            wsize -= charMax;
            wbuf = new char[wsize];
            memcpy(wbuf, tbuf+(sizeof(char) * charMax), wsize);

            // clean out temporary buffer
            delete [] tbuf;

            // set the next time to send
            delay = mdelay + rdelay;
            mdelay += rdelay + (mdelay - delay);
            delay = rdelay;

            return ret;
         } else {
            // send it all now
            resetDelay();
            return Connection::handleWrite(ev);
         }
      }
   }

   // the time has not yet come, we should spend most of it sleeping
   if (nows - last_time_s < sec || (nows - last_time_s == sec && sec > 1)) {
      // at least two second delay, sleep for one second and return to fight again later
      sleep(1);
      return 0;
   } else if (nows - last_time_s == sec && sec > 0) {
      // at least one second delay, sleep for half a second and return to fight again later
      usleep(500);
      return 0;
   } else {
      int rest = delay - diff;
      timespec ts;
      ts.tv_sec = 0;
      if (rest > 35) {
         // delay is more than 35 milliseconds away, sleep half of that and return to fight again later
         ts.tv_nsec = rest * 500; // half of 1000 nanoseconds per millisecond
         nanosleep(&ts, NULL);
         return 0;
      } else {
         // the time is now very soon, sleep all but 2 milliseconds and return to fight again later
         // the 2 milliseconds is so epoll has enough time to re-poll the device (the radio may stop being ready)
         ts.tv_nsec = (rest-2) * 1000; // 1000 nanoseconds per millisecond
         nanosleep(&ts, NULL);
         return 0;
      }
   }
}

// called to set the current time as the time to start delays from
void SerialConnection::setTimeNow() {
   timeNow(&last_time_s, &last_time_m);
}

// resets delay related variables and the last send time variables
void SerialConnection::resetDelay() {
   charMax = mdelay = rdelay = retries = 0;
   setTimeNow();
}

// wrapper for gettimeofday
void SerialConnection::timeNow(int *sec, int *msec) {
   timeval tv;
   gettimeofday(&tv, NULL);
   if (sec)  {*sec = tv.tv_sec;}
   if (msec) {*msec = tv.tv_usec / 1000;}
}

// called to increase the delay required before the next time I can send data (in milliseconds)
void SerialConnection::addDelay(int delay) {
   this->delay += delay;
}

// called to set the minimum delay required before the next time I can send data (in milliseconds)
void SerialConnection::minDelay(int delay) {
   this->delay = max(delay, this->delay);
}

// called to set the maximum amount of time (from the minimum delay time) allotted that I can send the data in (in milliseconds)
void SerialConnection::timeslot(int delay) {
   mdelay = this->delay + delay;
}

// called to set the delay time required before retrying if the delay time is missed (in milliseconds)
void SerialConnection::retryDelay(int delay) {
   rdelay = delay;
}

// sets the maximum characters allowed to be sent in the next delay slot
void SerialConnection::setMaxChar(int max) {
   charMax = max;
}


