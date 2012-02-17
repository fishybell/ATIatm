#include "mcp.h"
#include "rf.h"

int verbose;


void print_help(int exval) {
    printf("RFdump [-h] [-t port] [-v verbosity] \n\n");
    printf("  -h            print this help and exit\n");
    printf("  -t /dev/ttyS1 set serial port device\n");
    print_verbosity();    
    exit(exval);
}

/*************
 *************  RFdump.    just dump everything rxed on the radio
 *************   
 *************  if there is no argument, use defaultPORT
 *************  otherwise use the first are as the port number to listen on.
 *************
 *************  Also we first set up our connection to the RF modem on the serial port.
 *************
 *************  then we loop until we have a connection, and then when we get one we call the
 *************  handleSlaveRF routine which does all the communicating.
 *************    when the socket dies, we come back here and listen for a new MCP
 *************    
 *************/


int main(int argc, char **argv) {
    struct timespec elapsed_time, start_time, istart_time,delta_time;
    int serversock;			/* Socket descriptor for server connection */
    int MCPsock;			/* Socket descriptor to use */
    int RFfd;				/* File descriptor for RFmodem serial port */
    int opt;
    struct sockaddr_in ServAddr;	/* Local address */
    struct sockaddr_in ClntAddr;	/* Client address */
//    unsigned short RFslaveport;	/* Server port */
    unsigned int clntLen;               /* Length of client address data structure */
    char ttyport[32];	/* default to ttyS0  */
    char Rbuf[1024],buf[200];        /* Buffer MCP socket */
    char *Rptr, *Rstart;    
    int gathered,i;

    
    verbose=0;
    strcpy(ttyport,"/dev/ttyS1");
    
    while((opt = getopt(argc, argv, "hv:t:")) != -1) {
	switch(opt) {
	    case 'h':
		print_help(0);
		break;

	    case 'v':
		verbose = strtoul(optarg,NULL,16);
		break;

	    case 't':
		strcpy(ttyport,optarg);
		break;
		
	    case ':':
		fprintf(stderr, "Error - Option `%c' needs a value\n\n", optopt);
		print_help(1);
		break;

	    case '?':
		fprintf(stderr, "Error - No such option: `%c'\n\n", optopt);
		print_help(1);

		break;
	}
    }

    printf(" Watching comm port = <%s>\n",ttyport);


    // turn power to low for the radio A/B pin
    RFfd=open("/sys/class/gpio/export",O_WRONLY,"w");
    write(RFfd,"39",1);
    close(RFfd);
    RFfd=open("/sys/class/gpio/gpio39/direction",O_WRONLY,"w");
    write(RFfd,"out",3);
    close(RFfd);
    RFfd=open("/sys/class/gpio/gpio39/value",O_WRONLY,"w");
    write(RFfd,"0",1);		// a "1" here would turn on high power
    close(RFfd);
    RFfd=open("/sys/class/gpio/unexport",O_WRONLY,"w");
    write(RFfd,"39",1);		// this lets any kernel modules use the pin from now on
    close(RFfd);
    
    DCMSG(YELLOW,"A/B set for Low power.\n");
    
//   Okay,   set up the RF modem link here

   RFfd=open_port(ttyport); 
   DCMSG(RED,"opened port %s for serial link to radio as fd %d.  CURRENTLY BLOCKING IO",ttyport,RFfd);

   Rptr=Rstart=Rbuf;

    /**   loop until we lose connection  **/
   clock_gettime(CLOCK_MONOTONIC,&istart_time);	// get the intial current time

   sprintf(buf,"Segfaults without this line.  Rbuf = ");
	   DCMSG_HEXB(GREEN,buf,Rstart, 1);   
   
   DCMSG(GREEN,"\n\nElapsed Time   bytes  Packet data");   
   while(1) {
       
       gathered = gather_rf(RFfd,Rptr,Rstart,300);

       if (gathered>0) {
	   timestamp(&elapsed_time,&istart_time,&delta_time);
	   sprintf(buf,"%4ld.%09ld  %2d    ",elapsed_time.tv_sec, elapsed_time.tv_nsec,gathered);


	   printf("\x1B[3%d;%dm%s",(GREEN)&7,((GREEN)>>3)&1,buf);
	   if(gathered>1){
	       for (int i=0; i<gathered-1; i++) printf("%02x.", Rstart[i]);
	   }
	   printf("%02x\n", Rstart[i]);
	   Rptr=Rstart=Rbuf;
       }
   }
}







