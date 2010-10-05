#ifndef _TIMEOUT_H_
#define _TIMEOUT_H_

// helps in the creation of a single timeout value ready for epoll_wait
// when timed out, calling handleTimedoutEvents will run all events that have passed their end time

#include "timer.h"
#include <set>

using namespace std;

class Timeout {
public :
   static void handleTimeoutEvents(); // runs all ready events
   static void addTimeoutEvent(TimeoutTimer *timer); // add an event for handling
   static int timedOut(); // returns 1 if there is a timer waiting to be processed, 0 if not, and -1 if there are no timers

   static void clearTimeout(TimeoutTimer *timer); // clear a specific event from the existing list

   static int timeoutVal(); // returns an epoll_wait ready timeout value (-1 if no timers exist) in milliseconds

private :
   static multiset<TimeoutTimer*, TimerComp> timers;
};

#endif
