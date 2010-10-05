#ifndef _TIMER_H_
#define _TIMER_H_

#include <sys/time.h>

// timer types
enum timerTypes {
   b2100,
   heartBeat,
   deadHeart,
   resubscribe,
   serialWrite
};

// for manipulating an individual timeout

class TimeoutTimer {
public :
   friend struct TimerComp;

   TimeoutTimer(int msec); // starts the timeout timer and installs with the global timer

   struct timeval getStartTime() { return startTime; } // get the start time
   struct timeval getTimeout(); // get a timeout based on the start time and maximum wait
   int timedOut(); // returns 1 if the end time has passed, 0 if not

   virtual void handleTimeout() = 0; // Timeout class determines if this timer has timed out and calls this appropriately

   bool operator==(const TimeoutTimer& other); // checks start time, end time, and type

protected :
   void push(); // called by the base constructor to push the event onto the main timout class
   struct timeval startTime;
   struct timeval endTime;
   timerTypes type;
};

// comparator class for timers : lets the timers be sorted from earliest ending to latest
struct TimerComp {
  bool operator() (TimeoutTimer* const &lhs, TimeoutTimer* const &rhs) const {
     if (lhs->endTime.tv_sec < rhs->endTime.tv_sec) {
        return true;
     } else {
        return lhs->endTime.tv_usec < rhs->endTime.tv_usec;
     }
  }
};


#endif
