#include "mcp.h"
#include "fasit_c.h"
#include "rf.h"

thread_data_t minions[MAX_NUM_Minions];
struct sockaddr_in fasit_addr;
int verbose;
int slottime,total_slots;
int low_dev,high_dev;

void print_help(int exval) {
    printf("mcp [-h] [-v num] [-f ip] [-p port] [-r ip] [-m port] [-n minioncount]\n\n");
    printf("  -h            print this help and exit\n");
    printf("  -f 127.0.0.1  set FASIT server IP address\n");
    printf("  -p 4000       set FASIT server port address\n");
    printf("  -r 127.0.0.1  set RFmaster server IP address\n");
    printf("  -m 4004       set RFmaster server port address\n");
    printf("  -s 150        slottime in ms (5ms granules, 1275 ms max\n");
    printf("  -d 0x20       Lowest devID to find\n");
    printf("  -D 0x30       Highest devID to find\n");
    print_verbosity();
    exit(exval);
}

#define BufSize 1024

int main(int argc, char **argv) {
    struct epoll_event ev, events[MAX_NUM_Minions];    
    int opt,fds,nfds,minion_fd;
    int i, rc, mID,child,result,msglen,maxminion,minnum,error;
    char buf[BufSize];
    char RFbuf[BufSize];
    char cbuf[BufSize];
    char hbuf[100];
    struct sockaddr_in RF_addr;
    int RF_sock;
    int ready_fd_count,ready_fd,rf_addr;
    int timeout,taddr_cnt;
    taddr_t taddr[100];
    int cmd,crcFlag,plength,dev_addr;
    thread_data_t *minion;

    LB_packet_t *LB,LB_buf;

    LB_request_new_t *LB_new;
    LB_device_reg_t *LB_devreg;
    LB_assign_addr_t *LB_addr;
    

    // process the arguments
    //  -f 192.168.10.203   RCC ip address
    //  -p 4000		RCC port number
    //  -r 127.0.0.1	RFmaster ip address
    //  -m 4004		RFmaster port number
    //  -n 1		Number of minions to fire up
    //  -v 1		Verbosity level

    // MAX_NUM_Minions is defined in mcp.h, and minnum - the current number of minions.
    minnum = 0;	// now start with no minions
    verbose=0;
    /* start with a clean address structures */
    memset(&fasit_addr, 0, sizeof(struct sockaddr_in));
    fasit_addr.sin_family = AF_INET;
    fasit_addr.sin_addr.s_addr = inet_addr("192.168.10.203");	// fasit server the minions will connect to
    fasit_addr.sin_port = htons(4000);				// fasit server port number
    memset(&RF_addr, 0, sizeof(struct sockaddr_in));
    RF_addr.sin_family = AF_INET;
    RF_addr.sin_addr.s_addr = inet_addr("127.0.0.1");		// RFmaster server the MCP connects to
    RF_addr.sin_port = htons(4004);				// RFmaster server port number

    while((opt = getopt(argc, argv, "hv:m:r:p:f:s:d:D:")) != -1) {
	switch(opt) {
	    case 'h':
		print_help(0);
		break;

	    case 'v':
		verbose = strtoul(optarg,NULL,16);
		break;

	    case 's':
		slottime = atoi(optarg);	// leave it in ms until it gets sent in a packet
		break;

	    case 'd':
		low_dev = strtoul(optarg,NULL,16);
		break;

	    case 'D':
		high_dev = strtoul(optarg,NULL,16);
		break;

	    case 'f':
		fasit_addr.sin_addr.s_addr = inet_addr(optarg);
		break;

	    case 'p':
		fasit_addr.sin_port = htons(atoi(optarg));
		break;

	    case 'r':
		RF_addr.sin_addr.s_addr = inet_addr(optarg);
		break;

	    case 'm':
		RF_addr.sin_port = htons(atoi(optarg));
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
    DCMSG(BLUE,"MCP: verbosity is set to 0x%x", verbose);
    DCMSG(BLUE,"MCP: FASIT SERVER address = %s:%d", inet_ntoa(fasit_addr.sin_addr),htons(fasit_addr.sin_port));
    DCMSG(BLUE,"MCP: RFmaster SERVER address = %s:%d", inet_ntoa(RF_addr.sin_addr),htons(RF_addr.sin_port));

    total_slots=high_dev-low_dev+2;
    if (!slottime || (high_dev<low_dev)){
	DCMSG(RED,"\nMCP: slottime=%d must be set and high_dev=%d cannot be less than low_dev=%d",slottime,high_dev,low_dev);
	exit(-1);
    } else {
	
	DCMSG(RED,"MCP: slottime=%d  high_dev=%d low_dev=%d  total_slots=%d",slottime,high_dev,low_dev,total_slots);
    }

    /****************************************************************
     ******
     ******   connect to the RF Master process.
     ******     it might reside on different hardware
     ******   (ie a lifter board with a radio)
     ******    
     ******   we send stuff to it that we want to go out the RF
     ******   and we listen to it for responses from RF
     ******
     ****************************************************************/

    RF_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(RF_sock < 0)   {
	perror("socket() failed");
    }

    // use the RF_addr structure that was set by default or option arguments
    result=connect(RF_sock,(struct sockaddr *) &RF_addr, sizeof(struct sockaddr_in));
    if (result<0){
	strerror_r(errno,buf,BufSize);
	DCMSG(RED,"MCP RF Master server not found! connect(...,%s:%d,...) error : %s  ", inet_ntoa(RF_addr.sin_addr),htons(RF_addr.sin_port),buf);
	exit(-1);
    }

    // we now have a socket to the RF Master.
    DCMSG(RED,"MCP has a socket to the RF Master server");

    /****************************************************************
     ******
     ******    Acutally we need to wait for the RF master to report
     ******  registered Slaves, and then we spawn a minion for each
     ******  new one of those.
     ******
     ******
     ****************************************************************/

    /* set up polling so we can monitor the minions and the RFmaster*/
    fds = epoll_create(MAX_NUM_Minions);
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    // listen to the RFmaster
    ev.data.fd = RF_sock; // indicates listener fd
    if (epoll_ctl(fds, EPOLL_CTL_ADD, RF_sock, &ev) < 0) {
	perror("epoll RF_sock insertion error:\n");
	return 1;
    }

    mID=0;
    timeout = 3;	// quick the first time
    
    //    DCMSG(RED,"MCP all the minions have been De-Rezzed.   ");
    // loop until we lose connection to the rfmaster.
    while(1) {

	// wait for data from either the RFmaster or from a minion
//	DDCMSG(D_POLL,RED,"epoll_wait with %i timeout", timeout);

	ready_fd_count = epoll_wait(fds, events, MAX_NUM_Minions, timeout);
	DDCMSG(D_POLL,RED,"epoll_wait over   ready_fd_count = %d",ready_fd_count);

	if (timeout<3000) timeout = 30000;
	
	//  if we have no minions, or we have been idle long enough - so we
	//      build a LB packet "request new devices"
	//  send_LB();   // send it
	//
	if (!ready_fd_count /* || idle long enough */  ){
	    DDCMSG(D_RF,RED,"MCP:  Build a LB request new devices messages");
	    //   for now the request new devices will just use payload len=0
	    LB_new=(LB_request_new_t *) &LB_buf;
	    LB_new->cmd=LBC_REQUEST_NEW;
	    LB_new->low_dev=low_dev;
	    LB_new->high_dev=high_dev;
	    LB_new->slottime=slottime/5;	// adjust to 5ms granularity
	    
	    // calculates the correct CRC and adds it to the end of the packet payload
	    // also fills in the length field
	    set_crc8(LB_new);
	    // now send it to the RF master
	    result=write(RF_sock,LB_new,RF_size(LB_new->cmd));
	    if (verbose&D_RF){	// don't do the sprintf if we don't need to
		sprintf(hbuf,"MCP: sent %d (%d) bytes  LB request_newdev  ",result,RF_size(LB_new->cmd));
		DCMSG_HEXB(YELLOW,hbuf,LB_new, RF_size(LB_new->cmd));
	    }
	    
	} else { // we have some fd's ready
	    // check for ready minions or RF
	    for (ready_fd=0; ready_fd<ready_fd_count; ready_fd++) {
		// if we have data from RF, it is a LB packet we need to decode and pass on,
		// or one of our 'special packets' that means something else - irregardless we have to
		// have a case statement to process it properly
//////////////////////////////////////////////////////////////////////////////////////////////		
		if (RF_sock==events[ready_fd].data.fd){
		    msglen=read(RF_sock, buf, 1023);

		    LB=(LB_packet_t *)buf;
		    // do some checking - like to see if the CRC is OKAY
		    // although the CRC could be done in the RFmaster process

		    // actually can't check until we know how long it is
//		    DDCMSG(D_RF,RED,"crc=%d  checked = %d",LB->crc,crc8(&LB,2));
		    // Iff the CRC is okay we can process it, otherwise throw it away

		    if (verbose&D_RF){	// don't do the sprintf if we don't need to
			sprintf(hbuf,"MCP: read of LB packet from RFmaster address=%d  cmd=%d  msglen=%d \n",LB->addr,LB->cmd,msglen);
			DCMSG_HEXB(YELLOW,hbuf,buf,msglen);
		    }
		    //      process recieved LB packets 
		    //  mcp only handles registration and addressing packets,
		    //  mcp also has to pass new_address LB packets on to the minion so it can figure out it's own RF_address
		    //  mcp passes all other LB packets on to the minions they are destined for

		    if  (LB->cmd==LBC_DEVICE_REG){
			LB_devreg =(LB_device_reg_t *)(LB);	// change our pointer to the correct packet type
			//LB_devreg->length=RF_size(LB_devreg->cmd);	

			DDCMSG(D_RF,YELLOW,"MCP: RFslave sent LB_DEVICE_REG packet.   devtype=%d devid=0x%06x"
			       ,LB_devreg->dev_type,LB_devreg->devid);
			
			/***
			 ***   AFTER CHECKING if we already have a minion for this devid slave
			 ***     and ignoring? this packet if we do
			 ***
			 ***  we have a newly registered slave,
			 ***  create a minion for it
			 ***   create a new minion - mID = slave registration address
			 ***                       devid = MAC address
			 ****************************************************************************/
		    
/////////   CHECK if we already have a minion with this devID
			taddr_cnt=1;		//  step through to check if we already had this one
			while ((taddr_cnt<=mID)&&(minions[taddr_cnt].devid!=LB_devreg->devid)) taddr_cnt++;

			if (taddr_cnt>mID){  // we don't have this one yet.

			    mID+=1;	// increment our minion count

			    /* open a bidirectional pipe for communication with the minion  */
			    if (socketpair(AF_UNIX,SOCK_STREAM,0,((int *) &minions[mID].mcp_sock))){
				perror("opening stream socket pair");
				minions[mID].status=S_closed;
			    } else {
				minions[mID].status=S_open;
			    }

			    /****
			     ****    Initialize the minion thread data -
			     ****/

			    minions[mID].mID=mID;	// make sure we pass the minion ID down
			    minions[mID].devid=LB_devreg->devid;	// use the actual device id (MAC address)
			    minions[mID].seq=0;	// the seq number for this minions transmissions to RCC

			    if (LB_devreg->dev_type==1)
				minions[mID].S.cap|=PD_NES;	// add the NES capability

			    /*   fork a minion */    
			    if ((child = fork()) == -1) perror("fork");

			    if (child) {
				/* This is the parent. */
				DCMSG(RED,"MCP forked minion %d", mID);
				close(minions[mID].mcp_sock);

			    } else {
				/* This is the child. */
				close(minions[mID].minion);
				minion_thread(&minions[mID]);
				DCMSG(RED,"MCP: minion_thread(...) returned. that minion must have died, so do something smart here like remove it");

				break;	// if we come back , bail to another level or something more fatal
			    }

			    // add the minion to the set of file descriptors
			    // that are monitored by epoll
			    ev.events = EPOLLIN;
			    ev.data.fd = minions[mID].minion;
			    ev.data.ptr = (void *) &minions[mID]; 

			    if (epoll_ctl(fds, EPOLL_CTL_ADD, minions[mID].minion, &ev) < 0) {
				perror("MCP: epoll set insertion error: \n");
				return 1;
			    }

			    taddr_cnt=1;	// look for an unused address slot
			    while(taddr_cnt<2046&&(taddr[taddr_cnt].inuse)) taddr_cnt++;			    
//			    taddr[taddr_cnt].addr=LB_devreg->addr;
			    taddr[taddr_cnt].devid=LB_devreg->devid;
			    taddr[taddr_cnt].fd=minions[mID].minion;
			    taddr[taddr_cnt].mID=mID;
			    taddr[taddr_cnt].inuse=1;

			    DDCMSG(D_RF,RED,"MCP: assigning an address slot #%d temp=%d fd=%d mID=%d inuse=%d "
				   ,taddr_cnt,taddr[taddr_cnt].addr,taddr[taddr_cnt].fd,taddr[taddr_cnt].mID,taddr[taddr_cnt].inuse);


			    /***   end of create a new minion 
			     ****************************************************************************/
			}  //  end of check to see if we already had a minion this devID

		    
			// we send this even if we already had this devid - the slave might have rebooted
			// taddr_cnt  should be the RF_addr slot  it already had

			LB_addr =(LB_assign_addr_t *)(&LB_buf);	// map our bitfields in

			LB_addr->cmd=LBC_ASSIGN_ADDR;
			LB_addr->reregister=0;			
			LB_addr->devid=taddr[taddr_cnt].devid;	// get what might have been given out earlier
			LB_addr->new_addr=taddr_cnt;		// the actual slot is the perm address

			DDCMSG(D_RF,RED,"MCP: Build a LB device addr packet to assign the address slot %4d (0x%x)  %4d (0x%x)"
			       ,taddr_cnt,taddr_cnt,LB_addr->new_addr,LB_addr->new_addr);

			// calculates the correct CRC and adds it to the end of the packet payload
			// also fills in the length field
			set_crc8(LB_addr);
			if (verbose&D_RF){	// don't do the sprintf if we don't need to
			    sprintf(hbuf,"MCP: LB packet: RF_addr=%4d new_addr=%d cmd=%2d msglen=%d\n",
				    taddr_cnt,LB_addr->new_addr,LB_addr->cmd,RF_size(LB_addr->cmd));
			    DDCMSG_HEXB(D_RF,BLUE,hbuf,LB_addr,7);
			}
			// this packet must also get sent to the minion
			result=write(taddr[taddr_cnt].fd,LB_addr,RF_size(LB_addr->cmd));
			DDCMSG(D_RF,BLUE,"MCP: 2 Sent %d bytes to minion %d  fd=%d",result,mID,taddr[taddr_cnt].fd);

			// now send it to the RF master
			result=write(RF_sock,LB_addr,RF_size(LB_addr->cmd));
			DDCMSG(D_RF,RED,"MCP: 1 Sent %d bytes to RF fd=%d",result,RF_sock);


		    } else {	// it is any other command than dev regx
			// which means we just copy it on to the minion so it can process it

			// just display the packet for debugging
			LB=(LB_packet_t *)buf;
			if (verbose&D_RF){	// don't do the sprintf if we don't need to
			    sprintf(hbuf,"MCP: passing RF packet from RF_addr %4d on to Minion %d.   cmd=%2d  length=%d msglen=%d \n"
				    ,LB->addr, mID,LB->cmd,RF_size(LB->cmd),msglen);
			    DDCMSG_HEXB(D_RF,BLUE,hbuf,LB,RF_size(LB->cmd));
			}

			// do the copy down here
			result=write(ev.data.fd,LB,RF_size(LB->cmd));
			DDCMSG(D_RF,BLUE,"MCP: 3 Sent %d bytes to minion %d at fd=%d\n",result,mID,ev.data.fd);

		    }

		}
		else { // it is from a minion

		    //   we have to do some processing, mainly just pass on to the RF_sock

		    /***
		     *** we are ready to read from a minion - use the event.data.ptr to know the minion
		     ***   we should just have to copy the message down to the RF
		     ***   and we need to deal with special cases like the minion dieing.
		     ***
		     ***/

		    //		    minion_fd=events[ready_fd].data.fd;	// don't know what this is , but it aint right
		    minion=(thread_data_t *)events[ready_fd].data.ptr;
		    minion_fd=minion->minion;		    
		    mID=minion->mID;
		    DDCMSG(D_POLL,YELLOW,"MCP: fd %d for minion %d ready  [RF_addr=%d]\n",minion_fd,mID,minion->RF_addr);

		    if(minion_fd>2048) exit(-1);

		    msglen=read(minion_fd, buf, 1023);

		    // just display the packet for debugging
		    LB=(LB_packet_t *)buf;

		    // do the copy down here
		    result=write(RF_sock,LB,RF_size(LB->cmd));
		    if (verbose&D_RF){	// don't do the sprintf if we don't need to
			sprintf(hbuf,"MCP: passing Minion %d's LB packet to RFmaster address=%d  cmd=%d  length=%d msglen=%d \n"
				,mID,LB->addr,LB->cmd,RF_size(LB->cmd),msglen);
			DDCMSG_HEXB(D_RF,YELLOW,hbuf,buf,RF_size(LB->cmd));
		    }

		} // it is from a minion
	    } //  for all the ready fd's
	}  // else we did not time out
    } //while forever loop
} // end of main

