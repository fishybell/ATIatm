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

int open_port(char *sport){
    int fd,speed,myspeed; /* File descriptor for the port */
    struct termios my_termios;
    struct termios new_termios;
    char buf[200];
    
    fd = open(sport, O_RDWR | O_NOCTTY | O_NDELAY);

    if (fd == -1) {
       /*
	* Could not open the port.
	*/
	strerror_r(errno,buf,200);
	DCMSG(RED,"RFmaster open_port: Unable to open %s - %s \n", sport,buf);
    } else {
	fcntl(fd, F_SETFL, 0);

	tcgetattr( fd, &my_termios );
	my_termios.c_cflag &= ~CBAUD;
	my_termios.c_cflag |= B19200;
//	my_termios.c_cflag |= CRTSCTS;	// if we had flow control
	
	tcsetattr( fd, TCSANOW, &my_termios );
	tcgetattr( fd, &new_termios );
	speed = cfgetospeed( &new_termios );
	myspeed = cfgetospeed( &my_termios );	
	if ( speed != myspeed ){
	    DCMSG(RED,"RFmaster tcsetattr: Unable to set baud to %d, currently %d \n",myspeed,speed);
	} else {
	    DCMSG(GREEN,"RFmaster  open and ready at %d baud (B19200=%d)\n",speed,B19200);
	}

	DCMSG(GREEN,"RFmaster serial port %s open and ready \n", sport);
	
    }
    
    return (fd);
}



// tcp port we'll listen to for new connections
#define defaultPORT 4004

// size of client buffer
#define CLIENT_BUFFER 1024

void DieWithError(char *errorMessage){
    char buf[200];
    strerror_r(errno,buf,200);
    DCMSG(RED,"RFmaster %s %s \n", errorMessage,buf);
    exit(1);
}


/*************
 *************  HandleRF
 *************
 *************  We have a socket to the MCP,  and Serial connection to the RF
 *************
 *************  Just pass messages between the two...
 *************  
 *************  if we have a socket to an MCP, we act as a driver/link to the RF modem
 *************  through our serial port.  for now the RFmaster will only forward comms in
 *************  both directions - TCP connection to RF, and RF to TCP.
 *************    At some point it might be best if the RFmaster does some data massaging,
 *************  but until that is clear to me what it would need to do, for now we just pass comms
 *************
 *************/


void HandleRF(int MCPsock,int RFfd){
#define MbufSize 4096
    char Mbuf[MbufSize];        /* Buffer MCP socket */
    int MsgSize,sock_ready;                    /* Size of received message */
    fd_set rf_or_mcp;
    struct timeval timeout;
    
/**   loop until we lose connection  **/
    while(1){

	/*   do a select to see if we have a message from either the RF or the MCP  */
	/* then based on the source, send the message to the destination  */

	/* create a fd_set so we can monitor both the mcp and the connection to the RCC*/
	FD_ZERO(&rf_or_mcp);
	FD_SET(MCPsock,&rf_or_mcp);		// we are interested hearing the mcp
	FD_SET(RFfd,&rf_or_mcp);		// we also want to hear from the RF world
	
	/*   actually for now we will block until we have data - no need to timeout [yet?]
	 *     I guess a timeout that will make sense later is if the radio is ready for data
	 *   and we have no MCP data to send, then we should make the rounds polling
	 *   for status.  At the moment I don't want to impliment this at the RFmaster level,
	 *   for my initial working code I am shooting for the MCP to do all the work and
	 *   the RFmaster to just pass data back and forth.
	 */

	timeout.tv_sec=0;
	timeout.tv_usec=100000;	
	sock_ready=select(FD_SETSIZE,&rf_or_mcp,(fd_set *) 0,(fd_set *) 0, NULL);

	if (sock_ready<0){
	    strerror_r(errno,Mbuf,200);
	    DCMSG(RED,"RFmaster select error: %s", Mbuf);
	    exit(-1);
	}

	//  if we have data to read from the MCP, read it then force it down the radio
	//    we will have to watch to make sure we don't force down too much for the radio
	if (FD_ISSET(MCPsock,&rf_or_mcp)){
    /* Receive message from MCP */
	    if ((MsgSize = recv(MCPsock, Mbuf, MbufSize, 0)) < 0)
		DieWithError("recv() failed");

	    // blidly write it  to the  radio
	    write(RFfd,Mbuf,MsgSize);
	    DCMSG(BLUE,"Wrote \"%s\" to the Radio",Mbuf);
	    
	}

	//  if we have data to read from the RF, read it then blast it back upstream to the MCP
	if (FD_ISSET(RFfd,&rf_or_mcp)){
	    /* Receive message from RF */
	    MsgSize = read(RFfd,Mbuf,MbufSize);
	    
	    if ((MsgSize = read(RFfd,Mbuf,MbufSize)) < 0)
		DieWithError("read(RFfd,...) failed");

	    // blidly write it  to the MCP
	    write(MCPsock,Mbuf,MsgSize);
	    DCMSG(BLUE,"Wrote \"%s\" to the MCP",Mbuf);
	}

 //   /* Send received string and receive again until end of transmission */
 //   if (send(MCPsock, Mbuf, MsgSize, 0) != MsgSize)
//	DieWithError("send() failed");

    }
    
    close(MCPsock);    /* Close socket */
    
}

/*************
 *************  RFmaster stand-alone program.
 *************
 *************  if there is no argument, use defaultPORT
 *************  otherwise use the first are as the port number to listen on.
 *************
 *************  Also we first set up our connection to the RF modem on the serial port.
 *************
 *************  then we loop until we have a connection, and then when we get one we call the
 *************  handleRF routine which does all the communicating.
 *************    when the socket dies, we come back here and listen for a new MCP
 *************    
 *************/

int main(int argc, char **argv) {
    int serversock;			/* Socket descriptor for server connection */
    int MCPsock;			/* Socket descriptor to use */
    int RFfd;				/* File descriptor for RFmodem serial port */
    struct sockaddr_in ServAddr;	/* Local address */
    struct sockaddr_in ClntAddr;	/* Client address */
    unsigned short RFmasterport;	/* Server port */
    unsigned int clntLen;               /* Length of client address data structure */
    char ttyport[32];	/* default to ttyS0  */
    

    printf(" argc=%d argv[0]=\"%s\" argv[1]=\"%s\" argv[2]=\"%s\" argv[3]=\"%s\"\n",argc,argv[0],argv[1],argv[2],argv[3]);
/*    if (argc == 1){
	RFmasterport = defaultPORT;
	printf(" Listening on default port <%d>, comm port = <%s>\n", RFmasterport,ttyport);
    }
    */
    if (argv[1]){  
	RFmasterport = atoi(argv[1]);
    } else {
	RFmasterport = defaultPORT;
    }
    if (argv[2]){
	strcpy(ttyport,argv[2]);
    } else {
	strcpy(ttyport,"/dev/ttyS0");
    }
    
    printf(" Listening on port <%d>, comm port = <%s>\n", RFmasterport,ttyport);
    
//   Okay,   set up the RF modem link here

   RFfd=open_port(ttyport); 
    

//  now Create socket for the incoming connection */

    if ((serversock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	DieWithError("socket() failed");

	/* Construct local address structure */
    memset(&ServAddr, 0, sizeof(ServAddr));   /* Zero out structure */
    ServAddr.sin_family = AF_INET;                /* Internet address family */
    ServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    ServAddr.sin_port = htons(RFmasterport);      /* Local port */

	/* Bind to the local address */
    if (bind(serversock, (struct sockaddr *) &ServAddr, sizeof(ServAddr)) < 0)
	DieWithError("bind() failed");

	/* Set the size of the in-out parameter */
    clntLen = sizeof(ClntAddr);

	/* Mark the socket so it will listen for incoming connections */
	/* the '1' for MAXPENDINF might need to be a '0')  */
    if (listen(serversock, 2) < 0)
	DieWithError("listen() failed");

    for (;;) {

	/* Wait for a client to connect */
	if ((MCPsock = accept(serversock, (struct sockaddr *) &ClntAddr,  &clntLen)) < 0)
	    DieWithError("accept() failed");

	/* MCPsock is connected to a Master Control Program! */

	DCMSG(BLUE,"Good connection to MCP %s", inet_ntoa(ClntAddr.sin_addr));
	HandleRF(MCPsock,RFfd);
	DCMSG(BLUE,"Connection to MCP closed.   listening for a new MCPs");
	
    }
    /* NOT REACHED */
}



