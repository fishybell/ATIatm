#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h> /* POSIX terminal control definitions */

#include "mcp.h"



    /*
     * 'open_port()' - Open serial port 1.
     *
     * Returns the file descriptor on success or -1 on error.
     */

int open_port(void){
    int fd; /* File descriptor for the port */
    char sport[]="/dev/ttyS0",buf[200];
    
    fd = open(sport, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
       /*
	* Could not open the port.
	*/
	strerror_r(errno,buf,200);
	DCMSG(RED,"RFmaster open_port: Unable to open %s - %s \n", sport,buf);
    } else {
	fcntl(fd, F_SETFL, 0);
	DCMSG(RED,"RFmaster serial port %s open and ready \n", sport);
	
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
    int MsgSize=1;                    /* Size of received message */

/**   loop until we lose connection  **/
    while(MsgSize){

	/*   do a select to see if we have a message from either the RF or the MCP  */
	/* then based on the source, send the message to the destination  */
	
    /* Receive message from MCP */
    if ((MsgSize = recv(MCPsock, Mbuf, MbufSize, 0)) < 0)
	DieWithError("recv() failed");

    /* Send received string and receive again until end of transmission */
    if (send(MCPsock, Mbuf, MsgSize, 0) != MsgSize)
	DieWithError("send() failed");

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
    int serversock;			 /* Socket descriptor for server connection */
    int MCPsock;			 /* Socket descriptor to use */
    int RFfd;				 /* File descriptor for RFmodem serial port */
    struct sockaddr_in ServAddr;	 /* Local address */
    struct sockaddr_in ClntAddr;	 /* Client address */
    unsigned short RFmasterport;	 /* Server port */
    unsigned int clntLen;                /* Length of client address data structure */

    if (argc == 1){     /* Test for correct number of arguments */
	RFmasterport = defaultPORT;
	printf(" Listening on default port <%d>\n", RFmasterport);
    } else if (argc != 2){     /* Test for correct number of arguments */
	fprintf(stderr, "Usage:  %s <Server Port>\n", argv[0]);
	exit(1);
    } else {	
	RFmasterport = atoi(argv[1]);
	if ((RFmasterport<1)||(RFmasterport>65534)){
	    printf(" Listening on default port <%d>\n", RFmasterport);
	} else {
	    fprintf(stderr, "Port out of range\n",RFmasterport);
	    exit(1);
	}
    }

//   Okay,   set up the RF modem link here

   RFfd=open_port(); 
    

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



