#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
#define IBUFFERSIZE 1024
#define MAXPACKETSIZE 512
#define BAUDRATE B19200
#define FALSE 0
#define TRUE 1
#define TIMEOUT 100000

#define OUTPUT "8"

#define PORT 4000

volatile int STOP = FALSE;

void signal_handler_TERM (int sig, siginfo_t *siginfo, void *context);	/* terminate signal handler */

int main (int argc, char **argv)
{
    int serial_fd, socket_fd, client_fd;/* serial to modem, server socket, new connection to client */
    int c, pc, res, in=0;
    struct termios oldtio, newtio;
    char buf[BUFFERSIZE], ibuf[IBUFFERSIZE];
    char packet[MAXPACKETSIZE+1];
    struct sigaction term;		/* quit/term/int signal handler */
    int count = 0, yes = 1;
    fd_set readfs;			/* file descriptor set */
    int maxfd;
    struct timeval Timeout;		/* timeout handler for serial i/o selects */
    struct sockaddr_in serveraddr;	/* server address */

    FD_ZERO(&readfs);


    if (argc != 2) {
        printf("Usage: %s tty\n", argv[0]);
        return 1;
    }

    serial_fd = open (argv[1], O_RDWR | O_NOCTTY);
    if (serial_fd < 0) {
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
    tcgetattr (serial_fd, &oldtio);

    /* set new port settings */
    memset (&newtio, 0, sizeof (newtio));
    newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (canonical, no echo,...) */
    newtio.c_lflag = ICANON;

    newtio.c_cc[VTIME] = 0;	/* inter-character timer unused */
    newtio.c_cc[VMIN] = 5;	/* blocking read until 5 chars received */

    tcflush (serial_fd, TCIFLUSH);
    tcsetattr (serial_fd, TCSANOW, &newtio);

    /* setup socket */
    if((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return 1;
    }
    if(setsockopt(read_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("setsocket");
        return 1;
    }

    /* bind socket */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = INADDR_ANY;
    serveraddr.sin_port = htons(PORT);
    memset(&(serveraddr.sin_zero), 0, 8);

    if(bind(read_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
        perror("bind");
        return 1;
    }

    /* listen to socket */
    if(listen(socket_fd, 10) == -1) {
        perror("listen");
        return 1;
    }

    maxfd = max(serial_fd, socket_fd) + 1;	/* max file descriptor bit to watch for */

    /* loop for input */
    while (STOP == FALSE) {
         /* set timeout value within input loop */
         Timeout.tv_usec = TIMEOUT;	/* microseconds */
         Timeout.tv_sec  = 0;		/* seconds */

         /* block until input becomes available or timeout occurs */
         FD_SET(serial_fd, &readfs);  /* set testing for serial source */
         FD_SET(socket_fd, &readfs);  /* set testing for socket source */
         res = select(maxfd, &readfs, NULL, NULL, &Timeout);

         if (STOP == TRUE || res == 0) { continue; } /* timeout ... go to beginning */
         if (FD_ISSET(serial_fd, &readfs)) {    /* serial_fd is ready */
            res = read (serial_fd, buf, BUFFERSIZE);	/* returns after BUFFERSIZE chars have been input */
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
        } else if (FD_ISSET(socket_fd, &readfs)) { /* socket_fd is ready, but serial is not */
            /* (don't read socket if serials is waiting) */
            
            
        }
    }
    printf("restoring settings for %s\n", argv[1]);
    tcsetattr (serial_fd, TCSANOW, &oldtio);
}

void signal_handler_TERM (int sig, siginfo_t *siginfo, void *context) {
    STOP = TRUE;
}

