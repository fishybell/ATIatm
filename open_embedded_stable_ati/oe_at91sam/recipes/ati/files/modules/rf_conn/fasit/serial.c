#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h> /* POSIX terminal control definitions */
#include <unistd.h>

#include "mcp.h"

    /*
     * 'open_port(char *sport)' - Open serial port device 'sport'.
     *
     * Returns the file descriptor on success or -1 on error.
     */

int open_port(char *sport, int blocking){
    int fd,speed,myspeed; /* File descriptor for the port */
    struct termios my_termios;
    struct termios new_termios;
    char buf[200];

    // open as blocking or non-blocking
    if (blocking) {
       fd = open(sport, O_RDWR | O_NOCTTY);
    } else {
       fd = open(sport, O_RDWR | O_NOCTTY | O_NONBLOCK);
    }

    if (fd == -1) {
       /*
	* Could not open the port.
	*/
	strerror_r(errno,buf,200);
	DCMSG(RED,"open_port: Unable to open %s - %s \n", sport,buf);
	return(fd);
	
    } else {

	fcntl(fd, F_SETFL, 0);
	tcgetattr( fd, &my_termios );
	my_termios.c_cflag &= ~CBAUD;
	my_termios.c_cflag |= B19200 ;
	my_termios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	my_termios.c_oflag &= ~OPOST;
	my_termios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	my_termios.c_cflag &= ~(CSIZE | PARENB);
	my_termios.c_cflag |= CS8;

	my_termios.c_cc[VTIME] = 0;     /* inter-character timer unused */
	my_termios.c_cc[VMIN] = 1;      /* read a minimum of 1 character */
	tcflush (fd, TCIFLUSH);		/* something from Nate's old code */

	tcsetattr( fd, TCSANOW, &my_termios );
	tcgetattr( fd, &new_termios );
	/*  we did not make the fd non-blocking, because we use a select to test for data on multiple fd's and
	 *  we can have a timeout there if needed.
	 *     That seems easier than having it non-blocking and testing for readiness.
	 *     */

	speed = cfgetospeed( &new_termios );
	myspeed = cfgetospeed( &my_termios );	
	if ( speed != myspeed ){
	    DCMSG(RED,"open_port: tcsetattr: Unable to set baud to %d, currently %d \n",myspeed,speed);
	} else {
//	    DCMSG(GREEN,"open_port: open and ready at %d baud (B19200=%d)\n",speed,B19200);
	}
	DCMSG(GREEN,"open_port: serial port %s open and ready \n", sport);
    }
    return (fd);
}

