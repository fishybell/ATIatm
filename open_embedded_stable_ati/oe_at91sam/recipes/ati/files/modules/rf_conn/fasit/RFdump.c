#include "mcp.h"
#include "rf.h"

int verbose;

const char *__PROGRAM__ = "RFdump ";


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
    int opt,xmit,hardflow;
    struct sockaddr_in ServAddr;	/* Local address */
    struct sockaddr_in ClntAddr;	/* Client address */
//    unsigned short RFslaveport;	/* Server port */
    unsigned int clntLen;               /* Length of client address data structure */
    char ttyport[32];	/* default to ttyS0  */
    char Rbuf[1024],buf[200];        /* Buffer MCP socket */
    char *Rptr, *Rstart;    
    int gathered,i;
    LB_packet_t LB;
    LB_request_new_t *RQ;
    int result,size;
    RQ=(LB_request_new_t *)&LB;

    xmit=0;
    verbose=0;
    hardflow=6; // default is hardware flow control on and blocking
    strcpy(ttyport,"/dev/ttyS1");
    
    while((opt = getopt(argc, argv, "hv:f:t:x:")) != -1) {
	switch(opt) {
	    case 'h':
		print_help(0);
		break;

            case 'f':
               hardflow=7 & atoi(optarg);            
               break;

	    case 'v':
		verbose = strtoul(optarg,NULL,16);
		break;

	    case 'x':
		xmit = strtoul(optarg,NULL,10);
		break;

	    case 't':
		strcpy(ttyport,optarg);
		break;
		
	    case ':':
		EMSG("Error - Option `%c' needs a value\n\n", optopt);
		print_help(1);
		break;

	    case '?':
		EMSG("Error - No such option: `%c'\n\n", optopt);
		print_help(1);

		break;
	}
    }

    printf(" Verbosity = 0x%x\n",verbose);
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

   RFfd=open_port(ttyport, hardflow); // 1 for hardware flow control
   DCMSG(RED,"opened port %s for serial link to radio as fd %d.  ",ttyport,RFfd);

   Rptr=Rstart=Rbuf;

   RQ->cmd=LBC_REQUEST_NEW;
	// calculates the correct CRC and adds it to the end of the packet payload
	// also fills in the length field
   size = RF_size(RQ->cmd);
   set_crc8(RQ);

    /**   loop until we lose connection  **/
   clock_gettime(CLOCK_MONOTONIC,&istart_time);	// get the intial current time

   memset(buf,0,100);
//   sprintf(buf,"Segfaults without this line.  Rbuf = ");
//   DCMSG_HEXB(GREEN,buf,Rstart, 1);   

   DCMSG(GREEN,"\n\nElapsed Time   bytes  Packet data");
   
   while(1){
       if(xmit){
       usleep(xmit*1000);
       // now send it to the RF master
       result=write(RFfd,RQ,size);

       timestamp(&elapsed_time,&istart_time,&delta_time);
       sprintf(buf,"%4ld.%03ld  Xmit-> [%d:%d]   ",elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000,size,result);
       printf("\x1B[3%d;%dm%s",(BLUE)&7,((BLUE)>>3)&1,buf);
       for (int i=0; i<2; i++) printf("%02x.", ((uint8 *)RQ)[i]);
       printf("%02x\n", ((uint8 *)RQ)[3]);

       }
       gathered = gather_rf(RFfd,Rptr,300);

       if (gathered>0) {
	   timestamp(&elapsed_time,&istart_time,&delta_time);
	   sprintf(buf,"%4ld.%03ld  %2d    ",elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000,gathered);

//	   printf("\x1B[3%d;%dm%s",(GREEN)&7,((GREEN)>>3)&1,buf);
           printf("%s",buf);
           printf("\x1B[3%d;%dm",(GREEN)&7,((GREEN)>>3)&1);
	   if(gathered>1){
	       for (int i=0; i<gathered-1; i++) printf("%02x.", Rstart[i]);
	   }
	   printf("%02x\n", Rstart[gathered-1]);

	   DDpacket(Rstart,gathered);
	   
	   Rptr=Rstart=Rbuf;
       }

   }
}







