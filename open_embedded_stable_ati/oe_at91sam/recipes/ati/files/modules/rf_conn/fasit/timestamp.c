#include "mcp.h"


//  CLOCK_MONOTONIC_RAW  doesn't work on the ARM


void timestamp(minion_time_t *mt){
    struct timespec current_time;


    clock_gettime(CLOCK_MONOTONIC,&current_time);	// get a current time
    mt->delta_time.tv_sec=current_time.tv_sec;
    mt->delta_time.tv_nsec=current_time.tv_nsec;		// make a copy
    mt->elapsed_time.tv_sec=current_time.tv_sec;
    mt->elapsed_time.tv_nsec=current_time.tv_nsec;		// make another copy

    // elapsed time is current time - initial time
    mt->elapsed_time.tv_sec-=mt->istart_time.tv_sec;	// get the seconds right
    if (mt->elapsed_time.tv_nsec<mt->istart_time.tv_nsec){
	mt->elapsed_time.tv_sec--;			// carry a second over for subtracting
	mt->elapsed_time.tv_nsec+=1000000000L;	// carry a second over for subtracting
    }
    mt->elapsed_time.tv_nsec-=mt->istart_time.tv_nsec;	// get the useconds right

    // now calculate the time since the last timestamp in addition to the initial time difference 
    mt->delta_time.tv_sec-=mt->last_time.tv_sec;	// get the seconds right
    if (mt->delta_time.tv_nsec<mt->last_time.tv_nsec){
	mt->delta_time.tv_sec--;			// carry a second over for subtracting
	mt->delta_time.tv_nsec+=1000000000L;	// carry a second over for subtracting
    }
    mt->delta_time.tv_nsec-=mt->last_time.tv_nsec;	// get the useconds right

    mt->last_time.tv_sec=current_time.tv_sec;
    mt->last_time.tv_nsec=current_time.tv_nsec;	// update our time

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

