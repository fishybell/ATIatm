#include "mcp.h"


//  CLOCK_MONOTONIC_RAW  doesn't work on the ARM


void timestamp(struct timespec *elapsed_time, struct timespec *istart_time, struct timespec *delta_time){
    static struct timespec last_time;
    struct timespec current_time;


    clock_gettime(CLOCK_MONOTONIC,&current_time);	// get a current time
    delta_time->tv_sec=current_time.tv_sec;
    delta_time->tv_nsec=current_time.tv_nsec;		// make a copy
    elapsed_time->tv_sec=current_time.tv_sec;
    elapsed_time->tv_nsec=current_time.tv_nsec;		// make another copy

    // elapsed time is current time - initial time
    elapsed_time->tv_sec-=istart_time->tv_sec;	// get the seconds right
    if (elapsed_time->tv_nsec<istart_time->tv_nsec){
	elapsed_time->tv_sec--;			// carry a second over for subtracting
	elapsed_time->tv_nsec+=1000000000L;	// carry a second over for subtracting
    }
    elapsed_time->tv_nsec-=istart_time->tv_nsec;	// get the useconds right

    // now calculate the time since the last timestamp in addition to the initial time difference 
    delta_time->tv_sec-=last_time.tv_sec;	// get the seconds right
    if (delta_time->tv_nsec<last_time.tv_nsec){
	delta_time->tv_sec--;			// carry a second over for subtracting
	delta_time->tv_nsec+=1000000000L;	// carry a second over for subtracting
    }
    delta_time->tv_nsec-=last_time.tv_nsec;	// get the useconds right

    last_time.tv_sec=current_time.tv_sec;
    last_time.tv_nsec=current_time.tv_nsec;	// update our time

}

int ts2ms(struct timespec *ts) {
   return (ts->tv_sec * 1000) + (ts->tv_nsec/1000000l);
}

void ms2ts(int ms, struct timespec *ts) {
   ts->tv_sec = (ms / 1000);
   ts->tv_nsec = (ms % 1000) * 1000000l;
}

void ts_minus_ts(struct timespec *in1, struct timespec *in2, struct timespec *out) {
   int ms1, ms2, ms3;
   ms1 = ts2ms(in1);
   ms2 = ts2ms(in2);
   ms3 = ms1 - ms2;
   ms2ts(ms3, out);
}

