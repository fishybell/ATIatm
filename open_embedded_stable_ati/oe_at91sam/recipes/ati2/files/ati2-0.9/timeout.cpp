using namespace std;

#include "timeout.h"
#include "fasit_tcp.h"
#include "tcp_factory.h"
#include <sys/time.h>

// disable tracing for now
#ifdef TRACE
#undef TRACE
#define TRACE 0
#endif

// initialize static members
timeval Timeout::g_startTime;
timeval Timeout::g_maxTime;
int Timeout::events = 0;

// set's the timeout's start time to now
void Timeout::setStartTimeNow() {
FUNCTION_START("::setStartTimeNow()")
    gettimeofday(&g_startTime, NULL);
FUNCTION_END("::setStartTimeNow()")
}

// get the start time
timeval Timeout::getStartTime() {
FUNCTION_START("::getStartTime()")
FUNCTION_HEX("::getStartTime()", &g_startTime)
   return g_startTime;
}

// maximum amount of milliseconds to wait since setStartTime was called
void Timeout::setMaxWait(int msec) {
FUNCTION_START("::setMaxWait(int msec)")
   g_maxTime.tv_sec = g_startTime.tv_sec;
   g_maxTime.tv_usec = g_startTime.tv_usec + (msec * 1000);
   if (g_maxTime.tv_usec >= 1000000) {
      // did this push us to the next second (or more) ?
      g_maxTime.tv_sec += g_maxTime.tv_usec / 1000000;
      g_maxTime.tv_usec %= 1000000;
   }
FUNCTION_END("::setMaxWait(int msec)")
}

// get a timeout based on the start time and maximum wait
timeval Timeout::getTimeout() {
FUNCTION_START("::getTimeout()")
   // subtract current time from max time to acquire the timeout value
   timeval timeout;

   // do we have a timeout ?
   if (timedOut() != -1) {
      gettimeofday(&timeout, NULL);

      // subtract seconds straight out
      timeout.tv_sec = g_maxTime.tv_sec - timeout.tv_sec;

      // subtract microseconds, possibly "carrying the one"
      if (g_maxTime.tv_usec < timeout.tv_usec) {
         // we need to carry
         timeout.tv_usec -= g_maxTime.tv_usec;
         timeout.tv_sec--;
      } else {
         // we don't need to carry
         timeout.tv_usec = g_maxTime.tv_usec - timeout.tv_usec;
      }
   } else {
      timeout.tv_sec = timeout.tv_usec = 0;
   }

   // can't have a negative timeout, this means we've finished : create a 1 microsecond timeout
   if (timeout.tv_sec < 0) {
      timeout.tv_sec = 0;
      timeout.tv_usec = 1;
   }
FUNCTION_HEX("::getTimeout()", &timeout)
   return timeout;
}

// non globally accessible functions follow

// returns 1 if the max time has passed, 0 if not, and -1 if there is no max time
int Timeout::timedOut() {
FUNCTION_START("::timedOut()")
   // no timeout set
   if (g_maxTime.tv_sec + g_maxTime.tv_usec == 0) {
FUNCTION_INT("::timedOut()", -1)
      return -1;
   }

   // get the current time
   timeval ct;
   gettimeofday(&ct, NULL);

   if (ct.tv_sec > g_maxTime.tv_sec) {
FUNCTION_INT("::timedOut()", 1)
      return 1;
   } else if (ct.tv_sec == g_maxTime.tv_sec) {
      if (ct.tv_usec >= g_maxTime.tv_usec) {
FUNCTION_INT("::timedOut()", 1)
         return 1;
      } 
   }

FUNCTION_INT("::timedOut()", 0)
   return 0;
}

void Timeout::clearTimeout() {
FUNCTION_START("::clearTimeout()")
   g_maxTime.tv_sec = 0;
   g_maxTime.tv_usec = 0;
   events = 0;
FUNCTION_END("::clearTimeout()")
}

void Timeout::clearTimeout(int event) {
FUNCTION_START("::clearTimeout(int event)")
   // remove this specific event flag
   if (events & event) {
      events ^= event;
   }

   // no more flags? no more timeout
   if (events == 0) {
      clearTimeout();
   }
FUNCTION_END("::clearTimeout(int event)")
}

void Timeout::addTimeoutEvent(int event) {
FUNCTION_START("::addTimeoutEvent(int event)")
   events |= event;
FUNCTION_END("::addTimeoutEvent(int event)")
}

void Timeout::handleTimeoutEvents() {
FUNCTION_START("::handleTimeoutEvents()")
   // clear the timeout first as the individual events may set a new timeout
   int ev = events;
   clearTimeout();

   // handle individual events
   if (ev & BAKE_2100) {
      FASIT_TCP::Bake_2100();
   }
   if (ev & RESUBSCRIBE) {
      FASIT_TCP_Factory::SendResubscribe();
   }

FUNCTION_END("::handleTimeoutEvents()")
}

