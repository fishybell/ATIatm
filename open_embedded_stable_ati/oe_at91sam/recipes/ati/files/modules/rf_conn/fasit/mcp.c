#include "mcp.h"
#include "fasit_c.h"
#include "rf.h"

thread_data_t minions[MAX_NUM_Minions];
struct sockaddr_in fasit_addr;
int verbose;

void print_help(int exval) {
    printf("mcp [-h] [-v num] [-f ip] [-p port] [-r ip] [-m port] [-n minioncount]\n\n");
    printf("  -h            print this help and exit\n");
    printf("  -f 127.0.0.1  set FASIT server IP address\n");
    printf("  -p 4000       set FASIT server port address\n");
    printf("  -r 127.0.0.1  set RFmaster server IP address\n");
    printf("  -m 4004       set RFmaster server port address\n");
    printf("  -v 2          set verbosity bits\n");
    exit(exval);
}

#define BufSize 1024

int main(int argc, char **argv) {
    struct epoll_event ev, events[MAX_NUM_Minions];    
    int opt,fds,nfds;
    int i, rc, mID,child,result,msglen,maxminion,minnum,error;
    char buf[BufSize];
    char RFbuf[BufSize];
    char cbuf[BufSize];
    struct sockaddr_in RF_addr;
    int RF_sock;
    int ready_fd_count;
    int timeout;
    int cmd,crcFlag,plength,dev_addr;
    LB_packet_t *LB,LB_packet;

    LBC_device_reg *LB_devreg;
    


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

    while((opt = getopt(argc, argv, "hv:m:r:p:f:")) != -1) {
	switch(opt) {
	    case 'h':
		print_help(0);
		break;

	    case 'v':
		verbose = atoi(optarg);
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
    DCMSG(YELLOW,"MCP: verbosity is set to 0x%x", verbose);
    DCMSG(YELLOW,"MCP: FASIT SERVER address = %s:%d", inet_ntoa(fasit_addr.sin_addr),htons(fasit_addr.sin_port));
    DCMSG(YELLOW,"MCP: RFmaster SERVER address = %s:%d", inet_ntoa(RF_addr.sin_addr),htons(RF_addr.sin_port));

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

    //    DCMSG(RED,"MCP all the minions have been De-Rezzed.   ");
    // loop until we lose connection to the rfmaster.
    while(1) {

	// wait for data from either the RFmaster or from a minion
	timeout = 3000;
	DDCMSG(D_POLL,RED,"epoll_wait with %i timeout", timeout);
	ready_fd_count = epoll_wait(fds, events, MAX_NUM_Minions, timeout);
	DDCMSG(D_POLL,RED,"epoll_wait over   ready_fd_count = %d",ready_fd_count);

	//  if we have no minions, or we have been idle long enough - so we
	//      build a LB packet "request new devices"
	//  send_LB();   // send it
	//
	if (!ready_fd_count /* || idle long enough */  ){
	    DDCMSG(D_POLL,RED,"  Build a LB request new devices messages");

	    //   for now the request new devices will just use payload len=0	    
	    LB_packet.header=LB_HEADER(0,LBC_REQUEST_NEW);
	    // calculates the correct CRC and adds it to the end of the packet payload
	    // also fills in the length field
	    LB_CRC_add(&LB_packet,3);
	    // now send it to the RF master
	    result=write(RF_sock,&LB_packet,LB_packet.length);
	    
	    DDCMSG(D_POLL,RED,"  Sent %d bytes to RF\n",LB_packet.length);
	    
	} else {

	// we have some fd's ready
	if (ready_fd_count){

	    // check for ready minions or RF
	    for (i=0; i<ready_fd_count; i++) {
		// if we have data from RF, it is a LB packet we need to decode and pass on,
		// or one of our 'special packets' that means something else - regardless we have to
		// have a case statement to process it properly
		if (RF_sock==events[i].data.fd){

		    msglen=read(RF_sock, buf, 1023);

		    LB=(LB_packet_t *)buf;
		    // do some checking - like to see if the CRC is OKAY
		    // although the CRC could be done in the RFmaster process

		    // Iff the CRC is okay we can process it, otherwise throw it away
		    dev_addr=(LB->header&0xffe0)>>5;
		    cmd=(LB->header&0x1F);
		    DDCMSG(D_POLL,BLUE,"MCP: read from RFmaster... address=%d   cmd=%d  msglen=%d",dev_addr,cmd,msglen);

		    //   process the different LB packets we might see
		    switch (cmd) {
			
			case LBC_DEVICE_REG:

			    LB_devreg =(LBC_device_reg *)(LB->payload);	// map our bitfields in
			    
			    DDCMSG(D_POLL,BLUE,"MCP: RFslave sent LB DEVICE_REG packet.   devtype=%d devid=%06x tempaddr=%d"
				   ,LB_devreg->dev_type,LB_devreg->devid,LB_devreg->temp_addr);
			    
			    /***  if we have a newly registered slave,
			     ***  create a minion for it and make sure the minion ID  matches the new device address (1-1700)
			     ***  also the devid should reflect the actual slave MAC address
			     ***
			     ***   create a new minion - mID = slave registration address
			     ***                       devid = MAC address
			     ****************************************************************************/

			    /* open a bidirectional pipe for communication with the minion  */
			    if (socketpair(AF_UNIX,SOCK_STREAM,0,((int *) &minions[mID].mcp_sock))){
				perror("opening stream socket pair");
				minions[mID].status=S_closed;
			    } else {
				minions[mID].status=S_open;
			    }

			    minions[mID].mID=mID;	// make sure we pass the minion ID down
			    minions[mID].devid=LB_devreg->devid;	// use the actual device id (MAC address)

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

			    }

			    minnum++;	// increment our minion count
			    // add the minion to the set of file descriptors
			    // that are monitored by epoll
			    ev.events = EPOLLIN;
			    ev.data.fd = minions[mID].minion; 
			    //		    ev.data.ptr = mID; //ptr is unused
			    if (epoll_ctl(fds, EPOLL_CTL_ADD, minions[mID].minion, &ev) < 0) {
				perror("epoll set insertion error: \n");
				return 1;
			    }

			    /***   end of create a new minion 
			     ****************************************************************************/
			    break;
		    }
		}   else {
		    // we are ready on a connection to a minion
		    mID=events[i].data.fd;
		    DDCMSG(D_POLL,RED,"fd %d for minion %d ready\n",mID,mID);

		}
	    }
	}
	}

	sleep(1);	// put some dead time in just for debugging
    }
}
#if 0    
	    minions_ready=select(FD_SETSIZE,&minion_fds,(fd_set *) 0,(fd_set *) 0, &timeout);	
	    /* block until a minion wants something */

	    if (minions_ready<0){
		perror("select");
		return EXIT_FAILURE;
	    }

	    for (mID=0; mID<minnum; mID++) {
		if (FD_ISSET(minions[mID].minion,&minion_fds)){
		    msglen=read(minions[mID].minion, buf, 1023);
		    if (msglen > 0) {
			buf[msglen]=0;
			DCMSG(RED,"MCP received %d chars from minion %d (%d) -->%s<--",msglen,mID, minions[mID].minion, buf);
		    } else if (!msglen) {
			DCMSG(RED,"MCP: minion %d socket closed, minion has been DE-REZZED !!!", mID);
			close(minions[mID].minion);
			minions[mID].status=S_closed;
		    } else {
			perror("reading stream message");
		    }
		}
	    }
    }
    return EXIT_SUCCESS;	    
}
#endif



