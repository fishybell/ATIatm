#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>

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


#define BUFFERSIZE 16
#define MAXPACKETSIZE 512
#define BAUDRATE B19200
#define FALSE 0
#define TRUE 1
#define TIMEOUT 100000

#define OUTPUT "8"

volatile int STOP = FALSE;

void signal_handler_TERM (int sig, siginfo_t *siginfo, void *context);	/* terminate signal handler */

int main (int argc, char **argv)
{
    int fd, c, pc, res, in=0;
    struct termios oldtio, newtio;
    char buf[BUFFERSIZE];
    char packet[MAXPACKETSIZE+1];
    struct sigaction term;		/* quit/term/int signal handler */
    int count = 0;
    fd_set readfs;			/* file descriptor set */
    int maxfd;
    struct timeval Timeout;		/* timeout handler for serial i/o selects */


    if (argc != 2) {
        printf("Usage: %s tty\n", argv[0]);
        return 1;
    }

    fd = open (argv[1], O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror (argv[1]);
        return 1;
    }

    /* install the SIGTERM hanlder */
    memset (&term, 0, sizeof (term));
    term.sa_sigaction = signal_handler_TERM;
    term.sa_flags = SA_SIGINFO;
    if (sigaction(SIGTERM,&term,NULL)) {
        perror ("SIGTERM handler");
        return 1;
    }
    if (sigaction(SIGQUIT,&term,NULL)) {
        perror ("SIGQUIT handler");
        return 1;
    }
    if (sigaction(SIGINT,&term,NULL)) {
        perror ("SIGINT handler");
        return 1;
    }

    /* save current port settings */
    tcgetattr (fd, &oldtio);

    /* set new port settings */
    memset (&newtio, 0, sizeof (newtio));
    newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (canonical, no echo,...) */
    newtio.c_lflag = ICANON;

    newtio.c_cc[VTIME] = 0;	/* inter-character timer unused */
    newtio.c_cc[VMIN] = 5;	/* blocking read until 5 chars received */

    tcflush (fd, TCIFLUSH);
    tcsetattr (fd, TCSANOW, &newtio);

    maxfd = max(fd, fd) + 1;	/* max file descriptor bit to watch for */

    /* loop for input */
    while (STOP == FALSE) {
         /* set timeout value within input loop */
         Timeout.tv_usec = TIMEOUT;	/* microseconds */
         Timeout.tv_sec  = 0;		/* seconds */

         /* block until input becomes available or timeout occurs */
         FD_SET(fd, &readfs);  /* set testing for serial source */
         res = select(maxfd, &readfs, NULL, NULL, &Timeout);

         if (STOP == TRUE || res == 0) { continue; } /* timeout ... go to beginning */
         if (FD_ISSET(fd, &readfs)) {    /* fd is ready */
            res = read (fd, buf, BUFFERSIZE);	/* returns after BUFFERSIZE chars have been input */
            for (c=0;c<BUFFERSIZE;c++) {
                /* change null characters to a '.' */
                if (buf[c] == 0) {
                    buf[c] = '.';
                }
                /* if a start character is reached (inside or out) start the packet over */
                if (buf[c] == '~') {
                    in = 1;
                    pc = 0;
                }
                /* if we're in a packet, copy until stop reached */
                if (in) {
                    packet[pc++] = buf[c];
                    if (buf[c] == '!' || pc == MAXPACKETSIZE) {
                        /* not in the packet anymore, terminate it and print it out */
                        in = 0;
                        packet[pc] = 0;
                        printf ("%i:%" OUTPUT "." OUTPUT "s:%d\n", count++, packet, pc);
                    }
                }
            }
        }
    }
    printf("restoring settings for %s\n", argv[1]);
    tcsetattr (fd, TCSANOW, &oldtio);
}

void signal_handler_TERM (int sig, siginfo_t *siginfo, void *context) {
    STOP = TRUE;
}

