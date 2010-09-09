#include "stimer.h"

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


/* function pointer for handling data outside the message handler
 * arguments : 1 = file descriptor the data is coming from
 *           : 2 = size of data
 *           : 3 = data */
typedef void (*dataHandler) (int, int, char *);

/* function pointer for handling notification of a closed file descriptor outside the message handler
 * arguments : 1 = file descriptor that was closed */
typedef void (*closeHandler) (int);

/* function pointer for handling notification of a new socket connection outside the message handler
 * arguments : 1 = new file descriptor */
typedef void (*newHandler) (int);

/* creates a file descriptor for a listening socket, ready for the message handler
 * arguments : port = port number to listen on
 * returns : -1 for error, file descriptor otherwise */
int newSocketFD(int port);

/* creates a file descriptor for a serial device, ready for the message handler
 * arguments : des = path to serial device file (e.g. "/dev/ttyS0")
 * returns : -1 for error, file descriptor otherwise */
int newSerialFD(char *des);

/* starts the message handler thread
 * arguments : serialc = number of serial file descriptors to watch for data on
 *           : serialFDs = array of serial file descriptors to watch for data on (size of serialc)
 *           : socketc = number of socket file descriptors to watch for data on
 *           : socketFDs = array of socket file descriptors to watch for data on (size of socketc)
 *           : timer = pointer to shared timer between threads
 *           : handler = data handling callback function
 *           : newC = new connection handling callback function
 *           : closer = close handling callback function
 * returns : 0 on success, other on error */
int messager(int serialc, int *serialFDs, int socketc, int *socketFDs, stimer *timer, dataHandler handler, newHandler newC, closeHandler closer);

/* queues a message for sending
 * arguments : sender = the file descriptor sending the message
 *           : fdc = the number of recipients
 *           : fds = a list of file descriptor recipients (size of fdc)
 * returns : 0 on success, other on error */
int queue(int sender, int fdc, int *fds);

/* requests the message handler for a file descriptor to be closed (any currently queued message will still be sent)
 * arguments : fd = the file descriptor to be closed */
void rClose(int fd);

