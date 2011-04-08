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
#define TRACE 1
#define DEBUG 1
#define INFO 1
#define ERRORS 1

// for run time tracing of application
#ifdef TRACE
#define FUNCTION_START(arg) TRACE && printf("TRACE: Entering " arg  " in %s at line %i\n", __FILE__, __LINE__); TRACE && fflush(stdout);
#define FUNCTION_END(arg) TRACE && printf("TRACE: Leaving " arg  " in %s at line %i\n", __FILE__, __LINE__); TRACE && fflush(stdout);
#define FUNCTION_INT(arg, ret) TRACE && printf("TRACE: Returning %i from " arg  " in %s at line %i\n", ret, __FILE__, __LINE__); TRACE && fflush(stdout);
#define FUNCTION_HEX(arg, ret) TRACE && printf("TRACE: Returning 0x%08x from " arg  " in %s at line %i\n", (int)ret, __FILE__, __LINE__); TRACE && fflush(stdout);
#define FUNCTION_STR(arg, ret) TRACE && printf("TRACE: Returning '%s' from " arg  " in %s at line %i\n", ret, __FILE__, __LINE__); TRACE && fflush(stdout);
#define HERE TRACE && printf("TRACE: Here! %s %i\n", __FILE__, __LINE__); TRACE && fflush(stdout);
#define TMSG(...) TRACE && printf("TRACE: in %s at line %i:\t", __FILE__, __LINE__); TRACE && printf(__VA_ARGS__); TRACE && fflush(stdout);
#else
#define FUNCTION_START(arg)
#define FUNCTION_END(arg)
#define FUNCTION_INT(arg, ret)
#define FUNCTION_HEX(arg, ret)
#define FUNCTION_STR(arg, ret)
#define HERE
#define TMSG(...)
#endif

// for run time debugging of application
#ifdef DEBUG
#define PRINT_HEXB(data, size) DEBUG && ({ \
        printf("DEBUG: 0x"); \
        char *_data = (char*)data; \
        for (int _i=0; _i<size; _i++) { \
           printf("%02x", (__uint8_t)_data[_i]); \
        } \
        printf(" in %s at line %i\n", __FILE__, __LINE__); \
        }); DEBUG && fflush(stdout);
#define PRINT_HEX(arg) PRINT_HEXB(&arg, sizeof(arg))
#define BLIP DEBUG && printf("DEBUG: Blip! %s %i\n", __FILE__, __LINE__); DEBUG && fflush(stdout);
#define DMSG(...) DEBUG && printf("DEBUG: " __VA_ARGS__); DEBUG && fflush(stdout);
#else
#define PRINT_HEXB(data, size)
#define PRINT_HEX(arg)
#define BLIP
#define DMSG(...)
#endif

// for run time information viewing of application
#ifdef INFO
#define PROG_START INFO && printf("INFO: Starting in %s, compiled at %s %s MST\n\n", __FILE__, __DATE__, __TIME__);
#define IMSG(...) INFO && printf("INFO: " __VA_ARGS__);
#else
#define PROG_START
#define IMSG(...)
#endif

// for run time viewing of application errors
#ifdef ERRORS
#define IERROR(...) ERRORS && fprintf(stderr, "ERROR at %s %i:\n", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__);
#else
#define IERROR(...) fprintf(stderr, __VA_ARGS__);
#endif

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



#endif

