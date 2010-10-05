using namespace std;

#include "timeout.h"
#include "timer.h"
#include "common.h"
#include <sys/time.h>

TimeoutTimer::TimeoutTimer(int msec) {
FUNCTION_START("::TimeoutTimer(int msec)")
   // grab start time
   gettimeofday(&startTime, NULL);

   // calculate the end time
   endTime.tv_sec = startTime.tv_sec;
   endTime.tv_usec = startTime.tv_usec + (msec * 1000);
   while (endTime.tv_usec >= 1000000) {
      // did this push us to the next second (or more) ?
      endTime.tv_sec++;
      endTime.tv_usec -= 1000000;
   }
DMSG("Timeout of %i: (%i,%i) => (%i,%i)\n", msec, startTime.tv_sec, startTime.tv_usec, endTime.tv_sec, endTime.tv_usec)
FUNCTION_END("::TimeoutTimer(int msec)")
}

void TimeoutTimer::push() {
FUNCTION_START("::push()")
   // push onto global timeout class
   Timeout::addTimeoutEvent(this);
FUNCTION_END("::push()")
}

// get a timeout based on the start time and end time
timeval TimeoutTimer::getTimeout() {
FUNCTION_START("::getTimeout()")
   // subtract current time from end time to acquire the timeout value
   timeval timeout;
   gettimeofday(&timeout, NULL);
DMSG("finding timeout for (%i,%i) from (%i,%i)\n", endTime.tv_sec, endTime.tv_usec, timeout.tv_sec, timeout.tv_usec)

   // subtract seconds straight out
   timeout.tv_sec = endTime.tv_sec - timeout.tv_sec;

   // subtract microseconds, possibly "carrying the one"
   if (endTime.tv_usec < timeout.tv_usec) {
      // we need to carry
      timeout.tv_usec -= 1000000;
      timeout.tv_sec--;
   }

   // subtract the miscroseconds
   timeout.tv_usec = endTime.tv_usec - timeout.tv_usec;

DMSG("returning timeout of (%i,%i)\n", timeout.tv_sec, timeout.tv_usec)

   // can't have a negative timeout, this means we've finished : create a 1 microsecond timeout
   if (timeout.tv_sec < 0) {
      timeout.tv_sec = 0;
      timeout.tv_usec = 1;
   }
FUNCTION_HEX("::getTimeout()", &timeout)
   return timeout;
}

// non globally accessible functions follow

// returns 1 if the end time has passed, 0 if not
int TimeoutTimer::timedOut() {
FUNCTION_START("::timedOut()")
   // get the current time
   timeval ct;
   gettimeofday(&ct, NULL);

   // check to see if we've finished
   if (ct.tv_sec > endTime.tv_sec) {
FUNCTION_INT("::timedOut()", 1)
      return 1;
   } else if (ct.tv_sec == endTime.tv_sec) {
      if (ct.tv_usec >= endTime.tv_usec) {
FUNCTION_INT("::timedOut()", 1)
         return 1;
      } 
   }

FUNCTION_INT("::timedOut()", 0)
   return 0;
}

// checks start time, end time, and type
bool TimeoutTimer::operator==(const TimeoutTimer& other) {
FUNCTION_START("::operator==(const TimeoutTimer& other)")
   // first check type
   bool ret = type == other.type;

   // next check start time
   if (ret) {
      ret = startTime.tv_sec == other.startTime.tv_sec && startTime.tv_usec == other.startTime.tv_usec;
   }

   // last check end time
   if (ret) {
      ret = endTime.tv_sec == other.endTime.tv_sec && endTime.tv_usec == other.endTime.tv_usec;
   }
FUNCTION_INT("::operator==(const TimeoutTimer& other)", ret)
   return ret;
}

