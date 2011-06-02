#ifndef _COMMON_H_
#define _COMMON_H_

#include <string.h>
#include <stdio.h>

/*
 * min()/max() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define min(x,y) ({ \
        const typeof(x) _x = (x);       \
        const typeof(y) _y = (y);       \
        (void) (&_x == &_y);            \
        _x < _y ? _x : _y; })

#define max(x,y) ({ \
        const typeof(x) _x = (x);       \
        const typeof(y) _y = (y);       \
        (void) (&_x == &_y);            \
        _x > _y ? _x : _y; })

// comment these out to remove the TRACE, etc. lines from entire program, redefine to 0 to make individual chunks of code turn it off
static int C_TRACE=0;
static int C_DEBUG=0;
static int C_INFO=0;
static int C_ERRORS=0;
#define TRACE 1
#define DEBUG 1
#define INFO 1
#define ERRORS 1

// for run time tracing of application
#ifdef TRACE
#define FUNCTION_START(arg) C_TRACE && printf("TRACE: Entering " arg  " in %s at line %i\n", __FILE__, __LINE__); C_TRACE && fflush(stdout);
#define FUNCTION_END(arg) C_TRACE && printf("TRACE: Leaving " arg  " in %s at line %i\n", __FILE__, __LINE__); C_TRACE && fflush(stdout);
#define FUNCTION_INT(arg, ret) C_TRACE && printf("TRACE: Returning %i from " arg  " in %s at line %i\n", ret, __FILE__, __LINE__); C_TRACE && fflush(stdout);
#define FUNCTION_HEX(arg, ret) C_TRACE && printf("TRACE: Returning 0x%08x from " arg  " in %s at line %i\n", (int)ret, __FILE__, __LINE__); C_TRACE && fflush(stdout);
#define FUNCTION_STR(arg, ret) C_TRACE && printf("TRACE: Returning '%s' from " arg  " in %s at line %i\n", ret, __FILE__, __LINE__); C_TRACE && fflush(stdout);
#define HERE C_TRACE && printf("TRACE: Here! %s %i\n", __FILE__, __LINE__); C_TRACE && fflush(stdout);
#define TMSG(...) C_TRACE && printf("TRACE: in %s at line %i:\t", __FILE__, __LINE__); C_TRACE && printf(__VA_ARGS__); C_TRACE && fflush(stdout);
#else
#define FUNCTION_START(arg)
#define FUNCTION_END(arg)
#define FUNCTION_INT(arg, ret)
#define FUNCTION_HEX(arg, ret)
#define FUNCTION_STR(arg, ret)
#define HERE
#define TMSG(...)
#endif

//  colors for the DCMSG  
#define black	0
#define red	1
#define green	2
#define yellow	3
#define blue	4
#define magenta 5
#define cyan	6
#define gray	7

//  these are the 'bold/bright' colors
#define BLACK	8
#define RED	9
#define GREEN	10
#define YELLOW	11
#define BLUE	12
#define MAGENTA 13
#define CYAN	14
#define GRAY	15

// for run time debugging of application
#ifdef DEBUG
#define PRINT_HEXB(data, size) C_DEBUG && ({ \
        printf("DEBUG: 0x"); \
        char *_data = (char*)data; \
        for (int _i=0; _i<size; _i++) { \
           printf("%02x", (__uint8_t)_data[_i]); \
        } \
        printf(" in %s at line %i\n", __FILE__, __LINE__); \
        }); C_DEBUG && fflush(stdout);
#define CPRINT_HEXB(SC,data, size)  { \
    C_DEBUG && ({ \
       printf("DEBUG:\x1B[3%d;%dm 0x",(SC)&7,((SC)>>3)&1); \
       char *_data = (char*)data; \
       for (int _i=0; _i<size; _i++) printf("%02x", (__uint8_t)_data[_i]); \
       printf(" in %s at line %i\n", __FILE__, __LINE__); \
}); C_DEBUG && fflush(stdout); }
   
#define PRINT_HEX(arg) PRINT_HEXB(&arg, sizeof(arg))
#define BLIP C_DEBUG && printf("DEBUG: Blip! %s %i\n", __FILE__, __LINE__); C_DEBUG && fflush(stdout);
#define DMSG(...) C_DEBUG && printf("DEBUG: " __VA_ARGS__); C_DEBUG && fflush(stdout);
#define DCMSG(SC, FMT, ...) C_DEBUG && printf("DEBUG: \x1B[3%d;%dm" FMT "\x1B[30;0m\n",SC&7,(SC>>3)&1, ##__VA_ARGS__ ); C_DEBUG && fflush(stdout);
#define DCCMSG(SC, EC, FMT, ...) C_DEBUG && printf("DEBUG: \x1B[3%d;%dm" FMT "\x1B[3%d;%dm\n",SC&7,(SC>>3)&1, ##__VA_ARGS__ ,EC&7,(EC>>3)&1); C_DEBUG && fflush(stdout);
#define DCOLOR(SC) C_DEBUG && printf("\x1B[3%d;%dm",SC&7,(SC>>3)&1); C_DEBUG && fflush(stdout);
   
//  here are two usage examples of DCMSG
//DCMSG(RED,"example of DCMSG macro  with arguments  enum = %d  biff=0x%x",ghdr->cmd,biff) ;
//DCMSG(blue,"example of DCMSG macro with no args") ;   
//   I always like to include the trailing ';' so my editor can indent automatically

#else
#define PRINT_HEXB(data, size)
#define PRINT_HEX(arg)
#define BLIP
#define DMSG(...)
#define DCMSG(SC, FMT, ...)
#define DCCMSG(SC, EC, FMT, ...)
#define DCOLOR(SC)   
#endif

// for run time information viewing of application
#ifdef INFO
#define PROG_START C_INFO && printf("INFO: Starting in %s, compiled at %s %s MST\n\n", __FILE__, __DATE__, __TIME__);
#define IMSG(...) C_INFO && printf("INFO: " __VA_ARGS__);
#else
#define PROG_START
#define IMSG(...)
#endif

// for run time viewing of application errors
#ifdef ERRORS
#define IERROR(...) C_ERRORS && fprintf(stderr, "\x1B[31;1mERROR at %s %i: \x1B[30;0m\n", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fflush(stderr);
#else
#define IERROR(...) fprintf(stderr, __VA_ARGS__); fflush(stderr);
#endif

// utility function to get Device ID (mac address)
__uint64_t getDevID();

// utility function to properly configure a client TCP connection
void setnonblocking(int sock);

// utility class for comparing structures used as keys in maps
template <class T>
class struct_comp {
public :
   bool operator( )(T const & x, T const & y) const {
      int r = memcmp(&x, &y, sizeof(T));
      return r < 0;
   };
};

// global defines about targets
#define FAILURE_BATTERY_VAL 1 /* when at 1%, shutdown system */
#define MIN_BATTERY_VAL 25    /* when at 25%, send low-battery message */
#define MAX_BATTERY_VAL 255   /* a battery way value beyond "good" for resetting battery watchdogs */

// PD error codes
enum {
   ERR_normal,
   ERR_both_limits_active,
   ERR_invalid_direction_req,
   ERR_invalid_speed_req,
   ERR_speed_zero_req,
   ERR_stop_right_limit,
   ERR_stop_left_limit,
   ERR_stop_by_distance,
   ERR_emergency_stop,
   ERR_no_movement,
   ERR_over_speed,
   ERR_unassigned,
   ERR_wrong_direction,
   ERR_stop,
   ERR_lifter_stuck_at_limit,
   ERR_actuation_not_complete,
   ERR_not_leave_conceal,
   ERR_not_leave_expose,
   ERR_not_reach_expose,
   ERR_not_reach_conceal,
   ERR_low_battery,
   ERR_engine_stop,
   ERR_IR_failure,
   ERR_audio_failure,
   ERR_miles_failure,
   ERR_thermal_failure,
   ERR_hit_sensor_failure,
   ERR_invalid_target_type,
   ERR_bad_RF_packet,
   ERR_bad_checksum,
   ERR_unsupported_command,
   ERR_invalid_exception,
};

// for some reason we have a ntohs/htons, but no ntohf/htonf
inline float ntohf(const float &f) {
   __uint32_t holder = *(__uint32_t*)(&f), after;
   // byte starts as 1, 2, 3, 4, ends as 4, 3, 2, 1
   after = ((holder & 0x000000ff) << 24) | \
           ((holder & 0x0000ff00) << 8) | \
           ((holder & 0x00ff0000) >> 8) | \
           ((holder & 0xff000000) >> 24);

   DCMSG(BLUE, "Float changed from %f (0x%08X) to %f (0x%08X)\n", f, holder, *(float*)(&after), after);
   return *(float*)(&after);
}

#define htonf(f) (ntohf(f))

// helper function to convert feet to meters
inline int feetToMeters(const int &feet) {
   // fixed point feet/3.28
   int meters = feet * 100;
   meters /= 328;
   return meters;
}

// helper function to convert meters to feet
inline int metersToFeet(const int &meters) {
   // fixed point meters*3.28
   int feet = meters * 328;
   feet /= 100;
   return feet;
}

#endif

