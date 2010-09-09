#ifndef _TIMEOUT_H_
#define _TIMEOUT_H_

// for manipulating a single global timeout
// TODO -- manipulate on an individual timeout basis with the global timeout being the minimum, and handleTimeoutEvents only handling ready-to-go events

class Timeout {
public :
   static void setStartTimeNow(); // set's the timeout's start time to now
   static void setMaxWait(int msec); // maximum amount of milliseconds to wait since setStartTime was called

   static struct timeval getStartTime(); // get the start time
   static struct timeval getTimeout(); // get a timeout based on the start time and maximum wait

   static int timedOut(); // returns 1 if the max time has passed, 0 if not, and -1 if there is no max time

   static void handleTimeoutEvents(); // runs all flagged events
   static void addTimeoutEvent(int event); // flag an event for handling

   static void clearTimeout(); // clears all existing timeouts
   static void clearTimeout(int event); // clear a specific event from the existing timeout

private :
   static struct timeval g_startTime;
   static struct timeval g_maxTime;
   static int events;
};

// possible events
#define BAKE_2100 0x1
// #define OTHER_1 (0x1 << 1)
// #define OTHER_2 (0x1 << 2)

#endif
