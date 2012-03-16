#include "mcp.h"
#include "rf.h"

int verbose,devtype;

// tcp port we'll listen to for new connections
#define defaultPORT 4004

// size of client buffer
#define CLIENT_BUFFER 1024

void DieWithError(char *errorMessage){
    char buf[200];
    strerror_r(errno,buf,200);
    DCMSG(RED,"RFslave %s %s \n", errorMessage,buf);
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
 *************  through our serial port.  for now the RFslave will only forward comms in
 *************  both directions - TCP connection to RF, and RF to TCP.
 *************    At some point it might be best if the RFslave does some data massaging,
 *************  but until that is clear to me what it would need to do, for now we just pass comms
 *************
 *************/

#define Rxsize 1024

void HandleSlaveRF(int RFfd){
#define MbufSize 4096
    struct timespec elapsed_time, start_time, istart_time,delta_time;
    int holdoff;
    char Mbuf[MbufSize], Rbuf[1024],buf[200], hbuf[200];        /* Buffer MCP socket */
    int MsgSize,result,sock_ready,pcount=0;                    /* Size of received message */
    char *Mptr, Mstart, Mend;
    queue_t *Rx;    
    char *Rptr, *Rstart;    
    int gathered,resp;
    uint8 crc;
    
    int slottime,my_slot,total_slots,resp_slot;
    int fragment;
    int low_dev,high_dev;
    
    fd_set rf_or_mcp;
    struct timeval timeout;
    LB_packet_t *LB,rLB;
    int len,addr,cmd,RF_addr,size;

    uint32 DevID;
    LB_request_new_t *LB_new;
    LB_device_reg_t *LB_devreg;
    LB_assign_addr_t *LB_addr;
    LB_status_resp_lifter_t *LB_resp;
    LB_expose_t *LB_exp;
    
    RF_addr=2047;	//  means it is unassigned.  we will always respond to the request_new in our temp slot

    DevID=getDevID();
    DCMSG(BLUE,"RFslave: DevID = 0x%06X",DevID);

    // initialize our gathering buffer
    Rx=queue_init(Rxsize);	// incoming Rx buffer
   
    /**   loop until we lose connection  **/
    clock_gettime(CLOCK_MONOTONIC,&istart_time);	// get the intial current time
    
    while(1) {
	timestamp(&elapsed_time,&istart_time,&delta_time);
	DDCMSG(D_TIME,CYAN,"RFslave top of main loop at %5ld.%09ld timestamp, delta=%5ld.%09ld"
	       ,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);

//    MAKE SURE THE RFfd is non-blocking!!!

	gathered = gather_rf(RFfd,Rx->tail,Rx->head,300);	// gathered actually returns Queue_Depth

	if (gathered>0){  // increment the tail - later gather may take a queue as the argument
	    Rx->tail+=gathered;
	}
	/* Receive message, or continue to recieve message from RF */

	resp_slot=0;
	
	DDCMSG(D_VERY,GREEN,"RFslave: gathered %d  into Rx[%d:%d]:%d"
	      ,gathered,(int)(Rx->head-Rx->buf),(int)(Rx->tail-Rx->buf),Queue_Depth(Rx));

	fragment=0;
	while (gathered>=3&& !fragment){
	    LB=(LB_packet_t *)Rx->head;	// map the header in
	    size=RF_size(LB->cmd);
	    
	    DDCMSG(D_VERY,GREEN,"RFslave: while(gathered[%d]>=3) cmd=%d size=%d into Rx[%d:%d]:%d"
		   ,gathered,LB->cmd,size,Rx->head-Rx->buf,Rx->tail-Rx->buf,Queue_Depth(Rx));

	    // we have a chance of a compelete packet
	    if (Queue_Depth(Rx) >= size){
		//  we do have a complete packet
		// we could check the CRC and dump it here
		crc=crc8(LB);
		if (verbose&D_RF){	// don't do the sprintf if we don't need to
		    sprintf(buf,"RFslave[RFaddr=%2d]: pseq=%4d   %2d byte RFpacket Cmd=%2d crc=%d\n"
			    ,RF_addr,pcount++,RF_size(LB->cmd),LB->cmd,crc);
		    DCMSG_HEXB(GREEN,buf,Rx->head,size);
		}
		
	// only parse  if crc is good, we got one of the right commands, or our address matches
		if (!crc && ((LB->cmd==LBC_REQUEST_NEW)||(LB->cmd==LBC_ASSIGN_ADDR)||(RF_addr==LB->addr))){
		    timestamp(&elapsed_time,&istart_time,&delta_time);
		    DDCMSG(D_TIME,CYAN,"RFslave inside good packet at %5ld.%09ld timestamp, delta=%5ld.%09ld"
			   ,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
		    
	// check the CRC
	//  if good CRC, parse and respond or whatever
		    switch (LB->cmd){
			case LBC_REQUEST_NEW:
			    LB_new =(LB_request_new_t *) LB;
			    

			    /****      we have a request_new
			     ****
			     ****      We respond
			     ****      if our devid is in range AND forget_addr true
			     ****         our devid is in range AND ( !forget_addr and RF_addr==2047)
			     ****
			     ****      also,
			     ****      if our devid is in range AND forget_addr true
			     ****         we must forget our old address (change RF_addr to 2047)
			     ****      */

			    slottime=LB_new->slottime*5;	// we always pick up the latest slottime
			    low_dev=LB_new->low_dev;
			    DDCMSG(D_RF,RED,"Rxed 'request new devices'    devid=%x  low_dev=%x  RF_addr=%d",
				   DevID,low_dev, RF_addr);
			    
			    resp=0;
			    if ((DevID>=low_dev)&&(DevID<low_dev+32)){		// check range
				DDCMSG(D_RF,RED,"Rxed 'request new devices'    in range  devid=%x  low_dev=%x  RF_addr=%d",
				       DevID,low_dev, RF_addr);				
				if (LB_new->forget_addr&BV(DevID-low_dev)){	// checks if our bit is set
				    DDCMSG(D_RF,RED,"Recieved 'request new devices'   responding...  BV(%x-%x=%d)=%d AND forgetbits[%x] =%d  setting 2047",
					   DevID,low_dev,DevID-low_dev,BV(DevID-low_dev),LB_new->forget_addr,LB_new->forget_addr&BV(DevID-low_dev));	// we must forget our address
				    
				    RF_addr=2047;	// we must forget our address
				    resp=1;
				} else if (RF_addr==2047){
				    resp=1;
				}
			    }


			    // now we must respond if 'resp' is true

			    if (resp){
								
				my_slot=DevID-low_dev+1;	// the slot we should respond in
				total_slots=34;			// total number of slots with end padding

		// create a RESPONSE packet
				rLB.cmd=LBC_DEVICE_REG;
				LB_devreg =(LB_device_reg_t *)(&rLB);	// map our bitfields in
				LB_devreg->dev_type=devtype;		// use the option we were invoked with
				LB_devreg->devid=DevID;			// Actual 3 significant bytes of MAC address
				
				DDCMSG(D_RF,RED,"Recieved 'request new devices'   responding...  BV(%x-%x=%d)=%d AND forget bit=%d and RF_addr=%d",
				       DevID,low_dev,DevID-low_dev,BV(DevID-low_dev),LB_new->forget_addr&BV(DevID-low_dev), RF_addr);	// we must forget our address

				DDCMSG(D_RF,RED,"Recieved 'request new devices' set slottime=%dms total_slots=%d my_slot=%d RF_addr=%d",
				   slottime,total_slots,my_slot,RF_addr);			    			    

		// calculates the correct CRC and adds it to the end of the packet payload
				set_crc8(&rLB);

		// now send it to the RF master
		// after waiting for our timeslot:   which is slottime*(MAC&MASK) for now

				usleep(slottime*(my_slot)*1000);	// plus 1 is the holdoff
				DDCMSG(D_TIME,CYAN," pre-slot sleep for %dms.   slottime=%d total_slots=%d my_slot=%d",slottime*(my_slot),slottime,total_slots,my_slot);
			    
				result=write(RFfd,&rLB,RF_size(LB_devreg->cmd));
				if (verbose&D_RF){	// don't do the sprintf if we don't need to
				    sprintf(hbuf,"new device response to RFmaster devid=0x%06X len=%2d wrote %d\n"
					    ,LB_devreg->devid,RF_size(LB_devreg->cmd),result);
				    DCMSG_HEXB(BLUE,hbuf,&rLB,RF_size(LB_devreg->cmd));
				}
			    
				// finish waiting for the slots before proceding
				usleep(slottime*(total_slots-my_slot)*1000);
				DDCMSG(D_TIME,CYAN,"post-slot sleep for %dms.   slottime=%d total_slots=%d my_slot=%d",slottime*(total_slots-my_slot),slottime,total_slots,my_slot);

			    } else {
				DDCMSG(D_RF,RED,"Recieved 'request new devices'   NOT RESPONDING...  BV(%x-%x=%d)=%d AND forget bit=%d and RF_addr=%d",
				       DevID,low_dev,DevID-low_dev,BV(DevID-low_dev),LB_new->forget_addr&BV(DevID-low_dev), RF_addr);	// we must forget our address
							    }
			    break;

			case LBC_ASSIGN_ADDR:
			    DDCMSG(D_RF,BLUE,"Recieved 'assign address' packet.");
			    LB_addr =(LB_assign_addr_t *)(LB);	// map our bitfields in
			    
			    if ((DevID==LB_addr->devid)&& ((RF_addr==2047)||LB_addr->reregister)){   // passed the test
				RF_addr=LB_addr->new_addr;	// set our new address
				DDCMSG(D_RF,BLUE,"Assign address matches criteria, assigning new address %4d",RF_addr);
			    }
			    break;

			case LBC_EXPOSE:
			    if (LB->addr==RF_addr){
				DDCMSG(D_RF,BLUE,"Dest addr %d matches current address, cmd= %d",RF_addr,cmd);
				LB_exp =(LB_expose_t *)(LB);	// map our bitfields in

				DDCMSG(D_RF,BLUE,"Expose: exp hitmode tokill react mfs thermal\n"
				      "        %3d   %3d     %3d   %3d  %3d   %3d",LB_exp->expose,LB_exp->hitmode,LB_exp->tokill,LB_exp->react,LB_exp->mfs,LB_exp->thermal);
			    }
			    break;

			case LBC_STATUS_REQ:
			    // lets fake a status so we can send back and see the RFmaster handle it right.

			    // create a RESPONSE packet
			    LB_resp =(LB_status_resp_lifter_t *)(&rLB);	// map our bitfields in
			    LB_resp->cmd=LBC_STATUS_RESP_LIFTER;
			    LB_resp->addr=RF_addr;			    
			    LB_resp->hits=1;			// fake '1'
			    LB_resp->expose=1;			// fake '1'
			    set_crc8(&rLB);	// calculates the correct CRC and adds it to the end of the packet payload
			    DDCMSG(D_RF,BLUE,"Recieved 'Status request'.  respond with a LBC_STATUS_RESP_LIFTER");

// now send it to the RF master
// after waiting for our timeslot:
// this is only for the dumb test.
// Nates slave boss will need to be less dumb about this -
// it will have to queue packets to send and later do them at the right time.
// because this code will barf on more than one resp to it
			    usleep(slottime*(resp_slot+1)*1000);	// plus 1 is the holdoff
			    DDCMSG(D_TIME,CYAN,"msleep for %d.   slottimme=%d my_slot=%d",slottime*(my_slot+1),slottime,my_slot);

			    result=write(RFfd,&rLB,RF_size(LB_devreg->cmd));
			    if (verbose&D_RF){	// don't do the sprintf if we don't need to
				sprintf(hbuf,"new device response to RFmaster devid=0x%06X len=%2d wrote %d\n"
					,LB_devreg->devid,RF_size(LB_devreg->cmd),result);
				DCMSG_HEXB(BLUE,hbuf,&rLB,RF_size(LB_devreg->cmd));
			    }

				// finish waiting for the slots before proceding
			    usleep(slottime*(total_slots-(resp_slot+1))*1000);
			    DDCMSG(D_TIME,CYAN,"msleep for %d",slottime*(high_dev-low_dev+2));


			    
			    if (LB->addr==RF_addr){
				DDCMSG(D_RF,BLUE,"Dest addr %d matches current address, cmd= %d",RF_addr,cmd);
				LB_exp =(LB_expose_t *)(LB);	// map our bitfields in

				DDCMSG(D_RF,BLUE,"Expose: exp hitmode tokill react mfs thermal\n"
				       "        %3d   %3d     %3d   %3d  %3d   %3d",LB_exp->expose,LB_exp->hitmode,LB_exp->tokill,LB_exp->react,LB_exp->mfs,LB_exp->thermal);
			    }
			    break;

			default:

			    if (LB->addr==RF_addr){
				DDCMSG(D_RF,BLUE,"Dest addr %d matches current address, cmd = %d",RF_addr,LB->cmd);
				
			    }		    
			    break;
		    }  // switch LB cmd
		    DeQueue(Rx,size);	// delete just the packet we used
		    gathered-=size;
		    
		} else { // if our address matched and the CRC was good
		    if (crc) {// actually if the crc is bad we need to dequeue just 1 byte
			DDCMSG(D_RF,BLUE,"CRC bad.  DeQueueing 1 byte.");
			DeQueue(Rx,1);	// delete just one byte
			gathered--;
		    } else { // the packet was not for us, skip it
			if (QueuePtype(Rx)==2) resp_slot++;	// we have to count to find our slot
			DDCMSG(D_RF,BLUE,"NOT for us.  DeQueueing %d bytes.",size);
			DeQueue(Rx,size);	// delete just the packet we used
			gathered-=size;					
		    }
		}
	    } else {  // if there was a full packet
		fragment=1;	// force it out of the while
	    }
	}//  while we have gathered at least enough for a packet
    } // while forever
}// end of handleSlaveRF


void print_help(int exval) {
    printf("RFslave [-h] [-v verbosity] [-t serial_device] \n\n");
    printf("  -h            print this help and exit\n");
    printf("  -t /dev/ttyS1 set serial port device\n");
    print_verbosity();    
    exit(exval);
}

/*************
 *************  RFslave stand-alone program.
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
    int serversock;			/* Socket descriptor for server connection */
    int MCPsock;			/* Socket descriptor to use */
    int RFfd;				/* File descriptor for RFmodem serial port */
    int opt;
    struct sockaddr_in ServAddr;	/* Local address */
    struct sockaddr_in ClntAddr;	/* Client address */
//    unsigned short RFslaveport;	/* Server port */
    unsigned int clntLen;               /* Length of client address data structure */
    char ttyport[32];	/* default to ttyS0  */

    verbose=0;
    devtype=1;	// default to SIT with MFS
    strcpy(ttyport,"/dev/ttyS1");
    
    while((opt = getopt(argc, argv, "hv:t:d:")) != -1) {
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
		
	    case 'd':
		devtype=atoi(optarg);
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
    print_verbosity_bits();

   RFfd=open_port(ttyport, 0);  // 0 for non-blocking
   DCMSG(RED,"opened port %s for serial link to radio as fd %d.  ",ttyport,RFfd);
   
   HandleSlaveRF(RFfd);
   DCMSG(BLUE,"Connection to MCP closed.   listening for a new MCPs");


}






