using namespace std;

#include "timeout.h"
#include "common.h"

// initialize static members
multiset<TimeoutTimer*, TimerComp> Timeout::timers;

// returns 1 if the max time has passed, 0 if not, and -1 if there is no max time
int Timeout::timedOut() {
FUNCTION_START("::timedOut()")
   // no timeout set
   if (timers.empty()) {
FUNCTION_INT("::timedOut()", -1)
      return -1;
   }

   // return the value of the earliest timer
   multiset<TimeoutTimer*, TimerComp>::iterator first = timers.begin();
   int ret = (*first)->timedOut();
FUNCTION_INT("::timedOut()", ret)
   return ret;
}

// clear a specific event from the existing list
void Timeout::clearTimeout(TimeoutTimer *timer) {
FUNCTION_START("::clearTimeout(TimeoutTimer timer)")
   // loop through all timers
   multiset<TimeoutTimer*, TimerComp>::iterator found = timers.begin();
   while (found != timers.end()) {
      // check if they match
      TimeoutTimer *tod = (*found);
      if (*tod == *timer) {
DMSG("1 deleting timer of type %i\n", tod->getType())
         timers.erase(found);
         delete tod;
         found = timers.begin();
      } else {
         found++;
      }
   }
FUNCTION_END("::clearTimeout(TimeoutTimer timer)")
}

// clear a specific event from the existing list
void Timeout::clearTimeout(timerTypes type) {
FUNCTION_START("::clearTimeout(timerTypes type)")
   // loop through all timers
   multiset<TimeoutTimer*, TimerComp>::iterator found = timers.begin();
   while (found != timers.end()) {
      // check if they match
      TimeoutTimer *tod = (*found);
      if (tod->getType() == type) {
DMSG("2 deleting timer of type %i\n", type)
         timers.erase(found);
         delete tod;
         found = timers.begin();
      } else {
         found++;
      }
   }
FUNCTION_END("::clearTimeout(timerTypes type)")
}

// add an event for handling
void Timeout::addTimeoutEvent(TimeoutTimer *timer) {
FUNCTION_START("::addTimeoutEvent(TimeoutTimer timer)")
   // insert sorted
   timers.insert(timer);
FUNCTION_END("::addTimeoutEvent(TimeoutTimer timer)")
}

// runs all ready events
void Timeout::handleTimeoutEvents() {
FUNCTION_START("::handleTimeoutEvents()")

   // start at the earliest and run all timed out events in order
   multiset<TimeoutTimer*, TimerComp>::iterator timer = timers.begin();
   while (timer != timers.end() && (*timer)->timedOut()) {
      (*timer)->handleTimeout();
      clearTimeout((*timer));
      timer = timers.begin(); // look at the next earliest
   }

FUNCTION_END("::handleTimeoutEvents()")
}

// returns an epoll_wait ready timeout value (-1 if no timers exist) in milliseconds
int Timeout::timeoutVal() {
FUNCTION_START("::timeoutVal()")
   if (timers.empty()) {
      return -1;
   }

   // grab timeout from earliest timer
   multiset<TimeoutTimer*, TimerComp>::iterator first = timers.begin();
   timeval tv = (*first)->getTimeout();
   int ret = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
   
   // add 1 millisecond in case of rounding errors and so we don't have a 0 timeout
   ret++;

FUNCTION_INT("::timeoutVal()", ret)
   return ret;
}

