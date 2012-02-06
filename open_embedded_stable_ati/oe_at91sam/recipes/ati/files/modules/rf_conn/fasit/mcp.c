#include "mcp.h"

thread_data_t minions[MAX_NUM_Minions];




void print_help(int exval) {
    printf("mcp [-h] [-v num] [-f ip] [-p port] [-r ip] [-m port] [-n minioncount]\n\n");
    printf("  -h            print this help and exit\n");
    printf("  -f 127.0.0.1  set FASIT server IP address\n");
    printf("  -p 4000       set FASIT server port address\n");
    printf("  -r 127.0.0.1  set RFmaster server IP address\n");
    printf("  -m 4004       set RFmaster server port address\n");
    printf("  -v 2          set verbosity bits\n");
    printf("  -n 1          set number of minions to start\n\n");
    exit(exval);
}


#define BufSize 1024

int main(int argc, char **argv) {
    int opt;
    int i, rc, mID,child,result,msglen,highest_minion,minnum,error;
    char buf[BufSize];
    char RFbuf[BufSize];
    char cbuf[BufSize];
    struct sockaddr_in fasit_addr;
    struct sockaddr_in RF_addr;
    int RF_sock;
    fd_set minion_fds;
    int minions_ready,verbose;
    struct timeval timeout;


// process the arguments
//  -f 192.168.10.203   RCC ip address
//  -p 4000		RCC port number
//  -r 127.0.0.1	RFmaster ip address
//  -m 4004		RFmaster port number
//  -n 1		Number of minions to fire up
//  -v 1		Verbosity level


    // MAX_NUM_Minions is defined in mcp.h, and minnum - the number of minions to create must be less.
    minnum = 1;
    fasit_addr.sin_addr.s_addr = inet_addr("192.168.10.203");	// fasit server the minions will connect to
    fasit_addr.sin_port = htons(4000);				// fasit server port number
    RF_addr.sin_addr.s_addr = inet_addr("127.0.0.1");		// RFmaster server the MCP connects to
    RF_addr.sin_port = htons(4004);				// RFmaster server port number
    verbose=0;
    
    while((opt = getopt(argc, argv, "hv:n:m:r:p:f:")) != -1) {
	switch(opt) {
	    case 'h':
		print_help(0);
		break;
		
	    case 'v':
		verbose = atoi(optarg);
		printf("verbosity is set to 0x%x\n", verbose);
		break;
		
	    case 'f':
		fasit_addr.sin_addr.s_addr = inet_addr(optarg);
		printf("FASIT SERVER ip = `%s'\n", optarg);
		break;
		
	    case 'p':
		fasit_addr.sin_port = htons(atoi(optarg));
		printf("FASIT SERVER port = %d\n", atoi(optarg));
		break;
		
	    case 'r':
		RF_addr.sin_addr.s_addr = inet_addr(optarg);
		printf("RFmaster SERVER ip = `%s'\n", optarg);
		break;

	    case 'm':
		RF_addr.sin_port = htons(atoi(optarg));
		printf("RFmaster SERVER port = %d\n", atoi(optarg));
		break;

	    case 'n':
		minnum = atoi(optarg);
		printf("number of minions to start =%d\n", minnum);
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
    printf("verbosity is set to 0x%x\n", verbose);
    printf("FASIT SERVER address = %s<%d>\n", inet_ntoa(fasit_addr.sin_addr),htons(fasit_addr.sin_port));
    printf("RFmaster SERVER address = %s<%d>\n", inet_ntoa(RF_addr.sin_addr),htons(RF_addr.sin_port));



   
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

    /* start with a clean address structure */
    memset(&RF_addr, 0, sizeof(struct sockaddr_in));

    RF_addr.sin_family = AF_INET;
    RF_addr.sin_addr.s_addr = inet_addr("127.0.0.1");	// these need to be arguments, or something other than hard code
    RF_addr.sin_port = htons(4004);				// same here

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
 ******    start the minions
 ******
 ******
 ******
 ******
 ******
 ******
 ****************************************************************/
    
    DCMSG(RED,"MCP will start  %d minions",minnum);

    // loop until somebody wants to exit, or something
    while(1) {

	// determine what slaves are out there some how, and create just
	// the number of 
	
	// lets only make minnum minions, so they don't take over the system like magic brooms are likely to do
	for (mID=0; mID<minnum; mID++) {
	    // if we have a new slave, make a minion for it
	    // with minion ID = mID

	    /* open a bidirectional pipe for communication with the minion  */
	    if (socketpair(AF_UNIX,SOCK_STREAM,0,((int *) &minions[mID].mcp_sock))){
		perror("opening stream socket pair");
		minions[mID].status=S_closed;
	    } else {
		minions[mID].status=S_open;
	    }
	    
	    minions[mID].mID=mID;	// make sure we pass the minion ID down
	    minions[mID].devid=htonll(1+mID);	// make sure we pass some unique number for devid down
	    
	    /*   fork a minion */    
	    if ((child = fork()) == -1) perror("fork");
	    else if (child) {
		/* This is the parent. */
		DCMSG(RED,"MCP forked minion %d", mID);
		close(minions[mID].mcp_sock);
		
		msglen=read(minions[mID].minion, buf, 1023);
		error=errno;
		if (msglen > 0) {
		    buf[msglen]=0;
		    DCMSG(RED,"MCP received %d chars from minion %d -->%s<--",msglen, mID, buf);
		} else if (!msglen) {
		    DCMSG(RED,"MCP minion %d socket closed, minion has been DE-REZZED !  errno=%d", mID,error);
		    close(minions[mID].minion);
		    minions[mID].status=S_closed;
		    break;
		} else {
		    perror("reading stream message");
		}

		DCMSG(RED,"minion %d said -->%s<--", mID,buf);
		sprintf(cbuf,"Bow to the MCP, you are minion %d now", mID);
		result=write(minions[mID].minion, cbuf, strlen(cbuf));
		if (result >= 0){
		    DCMSG(RED,"sent %d chars to minion %d  --%s--",strlen(cbuf), mID,cbuf);
		} else {
		    perror("writing stream message");
		}
		//	close(minions[mID].minion);

	    } else {
		/* This is the child. */
		close(minions[mID].minion);
		minion_thread(&minions[mID],verbose);

		//	close(minions[mID].mcp);
	    }
	}

#if 1
	//  at this point we have a bunch of minions with open connections that
	//  we should be able to exploit.
	// this is just a communication test, really
	for (mID=0; mID<minnum; mID++){
	    if (minions[mID].status!=S_closed){
		sprintf(cbuf,"minion %d  Respond!", mID);
		result=write(minions[mID].minion, cbuf, strlen(cbuf));
		if (result >= 0){
		    DCMSG(RED,"sent %d chars to minion %d  --%s--",strlen(cbuf), mID,cbuf);
		} else {
		    perror("writing stream message");
		}
	    }
	}
#endif

	while(1){

	    /* create a fd_set so we can monitor the minions*/
	    FD_ZERO(&minion_fds);

	    highest_minion=0;
	    for (mID=0; mID<minnum; mID++) {
		if (minions[mID].status!=S_closed){
		    FD_SET(minions[mID].minion,&minion_fds);
		    highest_minion=minions[mID].minion;
		}
	    }
	    if (!highest_minion){
		DCMSG(RED,"MCP all the minions have been De-Rezzed.   ");
		// normally we would wait for them to re-attach or go look for more of them or something
		exit(-2);
	    }
	    
	    timeout.tv_sec=3;
	    timeout.tv_usec=0;

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
}
