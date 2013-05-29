#ifndef _COMMON_H_
#define _COMMON_H_

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "nl_conn.h"
#include "netlink_user.h"
#include "faults.h"

// version of __FILE__ without full path
#define __FILEX__ ((strrchr(__FILE__, '/') ? : __FILE__- 1) + 1)

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

// comment these out to remove the TRACE, etc. lines from entire program, redefine to 0 to make individual chunks of code turn it off
extern volatile int C_TRACE;
extern volatile int C_DEBUG;
extern volatile int C_INFO;
extern volatile int C_ERRORS;
extern volatile int C_KERNEL; // send messages to kernel for organized output over serial line

// helper function to send kernel netlink message
static int ignoreFromKernel(struct nl_msg *msg, void *arg) { return NL_OK; } ; // ignores everything received from kernel on this handler

static void msgToKernel(char *buf) {
   static struct nl_handle *handle = NULL;
   static int family = 0;

   // initialize once
   if (handle == NULL) {
      // connect to kernel
      handle = nl_handle_alloc();
      if (handle == NULL) { return; }
      if (genl_connect(handle) < 0) {
         nl_handle_destroy(handle);
         handle = NULL;
         return;
      }

      // prepare connection
      family = genl_ctrl_resolve(handle, "ATI");
      if (family < 0) {
         nl_handle_destroy(handle);
         handle = NULL;
         return;
      }

      // Prepare handle to receive the answer by specifying the callback (ignores everything)
      nl_socket_modify_cb(handle, NL_CB_VALID, NL_CB_CUSTOM, ignoreFromKernel, NULL);
      nl_socket_modify_cb(handle, NL_CB_FINISH, NL_CB_CUSTOM, ignoreFromKernel, NULL);
      nl_socket_modify_cb(handle, NL_CB_OVERRUN, NL_CB_CUSTOM, ignoreFromKernel, NULL);
      nl_socket_modify_cb(handle, NL_CB_SKIPPED, NL_CB_CUSTOM, ignoreFromKernel, NULL);
      nl_socket_modify_cb(handle, NL_CB_ACK, NL_CB_CUSTOM, ignoreFromKernel, NULL);
      nl_socket_modify_cb(handle, NL_CB_MSG_IN, NL_CB_CUSTOM, ignoreFromKernel, NULL);
      nl_socket_modify_cb(handle, NL_CB_MSG_OUT, NL_CB_CUSTOM, ignoreFromKernel, NULL);
      nl_socket_modify_cb(handle, NL_CB_INVALID, NL_CB_CUSTOM, ignoreFromKernel, NULL);
      nl_socket_modify_cb(handle, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, ignoreFromKernel, NULL);
      nl_socket_modify_cb(handle, NL_CB_SEND_ACK, NL_CB_CUSTOM, ignoreFromKernel, NULL);

   }

   // send to handler
   struct nl_msg *msg;
   msg = nlmsg_alloc();
   genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, NL_C_DMSG, 1);

   // add attribute (debug message)
   nla_put_string(msg, GEN_STRING_A_MSG, buf);

   // send
   nl_send_auto_complete(handle, msg); // send the message over the netlink handle

   // free the message
   nlmsg_free(msg);
}

// helper function to send to kernel or to userspace output (don't use with anything but stdout and stderr)
inline int myfprintf(_IO_FILE* file, const char *fmt, ...) {
   va_list args;
   int i;
   char buf[1024];

   va_start(args, fmt);
   i=vsnprintf(buf,1024,fmt,args);
   va_end(args);

   if (C_KERNEL) {
      msgToKernel(buf);
   } else {
      fprintf(file,buf);
   }

   return i;
}

// for run time tracing of application
#define FUNCTION_START(arg) { if (C_TRACE) { myfprintf(stdout, "TRACE: Entering " arg  " in %s at line %i\n", __FILEX__, __LINE__); fflush(stdout);}}
#define FUNCTION_END(arg) { if (C_TRACE){ myfprintf(stdout, "TRACE: Leaving " arg  " in %s at line %i\n", __FILEX__, __LINE__); fflush(stdout);}}

#define FUNCTION_INT(arg, ret) { if (C_TRACE) { myfprintf(stdout, "TRACE: Returning %i from " arg  " in %s at line %i\n", ret, __FILEX__, __LINE__); fflush(stdout);}}
#define FUNCTION_HEX(arg, ret) { if (C_TRACE) { myfprintf(stdout, "TRACE: Returning 0x%08x from " arg  " in %s at line %i\n", (int)ret, __FILEX__, __LINE__); fflush(stdout);}}
#define FUNCTION_STR(arg, ret) { if (C_TRACE) { myfprintf(stdout, "TRACE: Returning '%s' from " arg  " in %s at line %i\n", ret, __FILEX__, __LINE__); fflush(stdout);}}
#define HERE { if (C_TRACE) { myfprintf(stdout, "TRACE: Here! %s %i\n", __FILEX__, __LINE__); fflush(stdout);}}
#define TMSG(...) { if (C_TRACE) { myfprintf(stdout, "TRACE: in %s at line %i:\t", __FILEX__, __LINE__); myfprintf(stdout, __VA_ARGS__); fflush(stdout);}}

#define PRINT_HEXB(data, size) {if (C_TRACE) {{ \
        myfprintf(stdout, "DEBUG: 0x"); \
        char *_data = (char*)data; \
        for (int _i=0; _i<size; _i++) { \
           myfprintf(stdout, "%02x", (__uint8_t)_data[_i]); \
        } \
        myfprintf(stdout, " in %s at line %i\n", __FILEX__, __LINE__); \
        }; fflush(stdout);}}
#define CPRINT_HEXB(SC,data, size)  { if (C_TRACE) {{ \
       myfprintf(stdout, "DEBUG:\x1B[3%d;%dm 0x",(SC)&7,((SC)>>3)&1); \
       char *_data = (char*)data; \
       for (int _i=0; _i<size; _i++) myfprintf(stdout, "%02x", (__uint8_t)_data[_i]); \
       myfprintf(stdout, " in %s at line %i\n", __FILEX__, __LINE__); \
}; fflush(stdout); }}
#define CJUST_HEXB(SC,data, size)  { if (C_TRACE) {{ \
       myfprintf(stdout, "\x1B[3%d;%dm 0x",(SC)&7,((SC)>>3)&1); \
       char *_data = (char*)data; \
       for (int _i=0; _i<size; _i++) myfprintf(stdout, "%02x", (__uint8_t)_data[_i]); \
       myfprintf(stdout, "\x1B[3%d;%dm\n",EC&7,(EC>>3)&1); \
}; fflush(stdout); }}
   
#define PRINT_HEX(arg) PRINT_HEXB(&arg, sizeof(arg))

// for run time debugging of application
#define BLIP { if (C_DEBUG ){ myfprintf(stdout, "DEBUG: Blip! %s %i\n", __FILEX__, __LINE__); fflush(stdout);}}
#define DMSG(...) { if (C_DEBUG) { myfprintf(stdout, "DEBUG: " __VA_ARGS__); fflush(stdout);}}
#define DCMSG(SC, FMT, ...) { if (C_DEBUG) { myfprintf(stdout, "DEBUG: \x1B[3%d;%dm" FMT "\x1B[30;0m\n",SC&7,(SC>>3)&1, ##__VA_ARGS__ ); fflush(stdout);}}
#define DCCMSG(SC, EC, FMT, ...) {if (C_DEBUG){ myfprintf(stdout, "DEBUG: \x1B[3%d;%dm" FMT "\x1B[3%d;%dm\n",SC&7,(SC>>3)&1, ##__VA_ARGS__ ,EC&7,(EC>>3)&1); fflush(stdout);}}
#define DCOLOR(SC) { if (C_DEBUG){ myfprintf(stdout, "\x1B[3%d;%dm",SC&7,(SC>>3)&1); fflush(stdout);}}
   
//  here are two usage examples of DCMSG
//DCMSG(RED,"example of DCMSG macro  with arguments  enum = %d  biff=0x%x",ghdr->cmd,biff) ;
//DCMSG(blue,"example of DCMSG macro with no args") ;   
//   I always like to include the trailing ';' so my editor can indent automatically


// for run time information viewing of application
#define PROG_START { if (C_INFO) { myfprintf(stdout, "INFO: Starting in %s, compiled at %s %s MST\n\n", __FILEX__, __DATE__, __TIME__);}}
#define IMSG(...) { if (C_INFO) { myfprintf(stdout, "INFO: " __VA_ARGS__);}}

// for run time viewing of application errors
#define IERROR(...) { if (C_ERRORS) {myfprintf(stderr, "\x1B[31;1mERROR\x1B[30;0m at %s line %i: \n", __FILEX__, __LINE__);} myfprintf(stderr, __VA_ARGS__); fflush(stderr);}

// utility function to get Device ID (mac address)
__uint64_t getDevID();

// utility function to properly configure a client TCP connection
void setnonblocking(int sock, bool socket);

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
#define HALT_BATTERY_VAL 1 /* when lower than critical, halt-battery message */
#define FAILURE_BATTERY_VAL 5 /* when at 5%, send critical-battery message */
#define MIN_BATTERY_VAL 25    /* when at 25%, send low-battery message */
#define MAX_BATTERY_VAL 255   /* a battery way value beyond "good" for resetting battery watchdogs */

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

void closeListener();

#endif

