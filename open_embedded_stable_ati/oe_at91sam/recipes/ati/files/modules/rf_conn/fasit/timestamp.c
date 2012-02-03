#include "mcp.h"


//  CLOCK_MONOTONIC_RAW  doesn't work on the ARM


void timestamp(struct timespec *elapsed_time, struct timespec *istart_time){
    clock_gettime(CLOCK_MONOTONIC,elapsed_time);	// get a current time
    elapsed_time->tv_sec-=istart_time->tv_sec;	// get the seconds right
    if (elapsed_time->tv_nsec<istart_time->tv_nsec){
	elapsed_time->tv_sec--;		// carry a second over for subtracting
	elapsed_time->tv_nsec+=1000000000L;	// carry a second over for subtracting
    }
    elapsed_time->tv_nsec-=istart_time->tv_nsec;	// get the useconds right
}
