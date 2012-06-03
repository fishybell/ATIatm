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

/** hard flow bits:
 * x01 is hardwareflow
 * x02 is blocking
 * x04 is IGNBRK
 * x08 is read-only
 * x10 is write-only
 **/
int open_port(char *sport, int hardflow){
   int fd,speed,myspeed; /* File descriptor for the port */
   struct termios my_termios;
   struct termios new_termios;
   char buf[200];
   char sbuf[100];
   int rw = O_RDWR;

   sbuf[0]=0;

   if (hardflow&8) {
      rw = O_RDONLY; // we're just reading
   } else if (hardflow&0x10) {
      rw = O_WRONLY; // we're just writing
   }

   if (hardflow&2) {
      fd = open(sport, O_RDWR | O_NOCTTY);     //blocking
      strcat(sbuf,"Blocking ");
   } else {
      fd = open(sport, O_RDWR | O_NOCTTY | O_NONBLOCK); //nonblocking
      strcat(sbuf,"Non-Blocking ");
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

      if (hardflow&4) {
         //   code that seems like it should be 'better', but was 'different'
         my_termios.c_iflag &= ~( BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
         my_termios.c_iflag |= (IGNBRK );   // some documentation says we want this
         // the IGNBRK (ignore BREAK) is getting turned on
         my_termios.c_lflag &= ~(ECHO | ECHONL | ECHOE | ICANON | ISIG | IEXTEN);
         strcat(sbuf,"IGNBRK set ");
      } else {
         //   the old code.
         my_termios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
         // the IGNCR (ignore carrage returns) is getting turned off
         // the IGNBRK (ignore BREAK) is getting turned off
         my_termios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
         strcat(sbuf,"IGNBRK cleared ");
      }
      
      my_termios.c_oflag &= ~OPOST;
      my_termios.c_cflag &= ~(CSIZE | PARENB);
      my_termios.c_cflag |= CS8;

      if (hardflow&1) {
         my_termios.c_cflag |= CRTSCTS;         // turn  on hardware flow control  RTS CTS
         strcat(sbuf,"CRTSCTS set ");
      } else {
         my_termios.c_cflag &= ~CRTSCTS;        // turn off hardware flow control  RTS CTS
         strcat(sbuf,"CRTSCTS cleared ");
      }

      my_termios.c_cc[VTIME] = 0;     /* inter-character timer unused */

      // if we want it to be non-blocking, set vmin to 0 or else it blocks
      if (hardflow&2) { 
         my_termios.c_cc[VMIN] = 1;      /* read a minimum of 1 character */
      } else {
         my_termios.c_cc[VMIN] = 0;      /* NON-BLOCKING!   needs to be zero */
      }         
      tcflush (fd, TCIFLUSH);           /* something from Nate's old code */

      tcsetattr( fd, TCSANOW, &my_termios );
      tcgetattr( fd, &new_termios );

      speed = cfgetospeed( &new_termios );
      myspeed = cfgetospeed( &my_termios );     
      if ( speed != myspeed ){
         DCMSG(RED,"open_port: tcsetattr: Unable to set baud to %d, currently %d \n",myspeed,speed);
      }
      DCMSG(GREEN,"open_port: serial port %s open and ready with %s \n", sport,sbuf);
   }
   return (fd);
}

