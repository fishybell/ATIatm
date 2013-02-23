// smartqueue.c

// Action Target Smart Queue Plugin
// Developed 12/2012 by Jesse Jensen
// for the ATM project.

// FILES: smartqueue.c, smartqueue_structures.c

// The smart queue plugin is designed to increase control over 
// RF communications allowing reduction of susceptibility to 
// packet collisions and RF noise, enhancing system
// performance and scalability while minimizing impact of the
// change to existing, validated features.

// The plugin operates by inserting control process modules at
// two points in the RF communication chain. The RCC-side
// process runs in between the MCP and RFMaster modules. The 
// target-side process runs in between the RFSlave and
// Slaveboss modules. The processes work together to control
// the flow of data over the RF link. Most notably, the system
// enforces that targets only transmit when requested by the
// RCC-side process. 

// This file contains two main thread functions,
// smartqueue_rcc_thread and smartqueue_trg_thread, which run
// on the RCC and target respectively. Each is invoked as a
// child process of one of the existing process modules. The 
// support functions and structure definitions are in 
// smartqueue_structures.c.

// System Include Directives
#include <sys/socket.h> 		// sockets
#include <sys/select.h> 		// socket "select"
#include <sys/time.h>			// time functions

// Custom Include Directives
#include "rf.h"				// legacy RF include
#include "smartqueue_structures.c"	// smartqueue support

// Main RCC smartqueue thread - runs on the RCC
// bridging between MCP and RFmaster.

// CALLED BY: mcp
// IN: MCP_sock: connected socket to mcp
//     RF_sock: connected socket to RFmaster
void smartqueue_rcc_thread(int MCP_sock, int RF_sock) {
	DCMSG(BLACK,"Smartqueue RCC process active.");

	// head pointers for RCC side target and command lists
	struct targetelt *targetlisthead=NULL;
	struct commandqueueelt *commandqueuehead=NULL;

	fd_set rf_or_mcp;				// socket selection set
	int nfds;					// socket selection: number of file descriptors
	int msglen;					// socket message length
	struct timeval selecttimeout;			// socket selection timeout	
	timerclear(&selecttimeout);			// init
	selecttimeout.tv_usec=1000*RCC_SELECT_TIMEOUT;	// set

	char mcpincbuf[BufSize];			// incoming buffer from MCP
	char rfmincbuf[BufSize];			// incoming buffer from RFmaster
	LB_packet_t *LB;				// low bandwidth packet pointer (to use existing RF structures)

	// set nfds for select
	if(RF_sock >= MCP_sock) {
		nfds=RF_sock+1;
	} else {
		nfds=MCP_sock+1;
	}

	while(1) { // never stop

		// setup and select sockets with RCC_SELECT_TIMEOUT
		FD_ZERO(&rf_or_mcp);
		FD_SET(RF_sock,&rf_or_mcp);
		FD_SET(MCP_sock,&rf_or_mcp);
		select(nfds, &rf_or_mcp, (fd_set *) 0, (fd_set *) 0, &selecttimeout);

		// check socket to/from mcp first
		if(FD_ISSET(MCP_sock, &rf_or_mcp)) {
			// read data from mcp socket
			msglen = read(MCP_sock,mcpincbuf,1); // first byte indicates message size
			if(msglen > 0) {
				LB=(LB_packet_t *)mcpincbuf; 
				msglen+=read(MCP_sock,(mcpincbuf+1),RF_size(LB->cmd)-1); 	// read the message
				processMCPpacket(mcpincbuf, RF_sock, msglen, &targetlisthead, &commandqueuehead);// analyze & forward packet			
			}
		}

		// check socket to/from rfmaster second
		if(FD_ISSET(RF_sock, &rf_or_mcp)) {
			// read data from RFmaster socket
			msglen = read(RF_sock,rfmincbuf,1); // first byte indicates message size
			if(msglen > 0) {
				LB=(LB_packet_t *)rfmincbuf; 
				msglen+=read(RF_sock,(rfmincbuf+1),RF_size(LB->cmd)-1); 	// read the message
				checkqueue(&commandqueuehead,LB);		// check if this packet was requested & remove request if so
				touchtarget(&targetlisthead,LB->addr);		// update the "last touched" for this target
				DCMSG(BLACK,"Smartqueue RF receive addr: 0x%x cmd: %d.",LB->addr,LB->cmd);
				write(MCP_sock,rfmincbuf,msglen);		// pass the packet to MCP
			}
		}

		processqueue(&commandqueuehead,RF_sock,targetlisthead);		// process queued commands

		roundrobin(targetlisthead,ROUND_ROBIN_FREQ,RF_sock);		// round robin commands
	}	

	// should not get here
	targetlistdestruct(&targetlisthead);
	commandqueuedestruct(&commandqueuehead);
	return;
}

// Main target smartqueue thread - runs on the target
// bridging between RFslave and slaveboss.

// CALLED BY: RFslave
// IN: RFslave_sock: connected socket to RFslave
//     SlaveBoss_sock: connected socket to Slaveboss
void smartqueue_trg_thread(int RFslave_sock, int SlaveBoss_sock) {

	// head pointer for target side response queue (FIFO)
	struct responsequeueelt *targetqueuehead=NULL;

	int bypass=1; //prevents queue from blocking packets before discovery

	fd_set slave_or_boss;				// socket selection set
	int nfds;					// socket selection: number of file descriptors
	int msglen;					// socket message length

	char rfsincbuf[BufSize];			// incoming buffer from RFSlave
	char sbsincbuf[BufSize];			// incoming buffer from Slaveboss
	LB_packet_t *LB;				// low bandwidth packet pointer (to use existing RF structures)
	
	// set nfds for select
	if(SlaveBoss_sock >= RFslave_sock) {
		nfds=SlaveBoss_sock+1;
	} else {
		nfds=RFslave_sock+1;
	}

	while(1) { // never stop

		// setup and select sockets
		FD_ZERO(&slave_or_boss);
		FD_SET(SlaveBoss_sock,&slave_or_boss);
		FD_SET(RFslave_sock,&slave_or_boss);
		select(nfds, &slave_or_boss, (fd_set *) 0, (fd_set *) 0, NULL);

		// check socket to/from RFslave first
		if(FD_ISSET(RFslave_sock, &slave_or_boss)) {
			// read data from RFslave socket
			msglen = read(RFslave_sock,rfsincbuf,BufSize); // from legacy RFslave
			if(msglen > 0) {
				// check for special packet
				LB=(LB_packet_t *)rfsincbuf;
				if(LB->cmd == LBC_TRIGGER_PACKET) {
					bypass=0; //receiving this packet means we have been memorized by the RCC smartqueue thread.
					targetPopQueueElt(&targetqueuehead,RFslave_sock); // respond to trigger packet by sending the next element
				} else {
					write(SlaveBoss_sock,rfsincbuf,msglen);	// pass to slaveboss
				}
			}
		}

		// check socket to/from Slaveboss second
		if(FD_ISSET(SlaveBoss_sock, &slave_or_boss)) {
			// read data from slaveboss socket
			msglen = read(SlaveBoss_sock,sbsincbuf,BufSize);
			if(msglen > 0) {
				LB=(LB_packet_t *)sbsincbuf;
				if(bypass || LB->cmd == LBC_DEVICE_REG) {
					write(RFslave_sock,sbsincbuf,msglen); //just pass it on
				} else {
					targetPushQueueElt(&targetqueuehead,sbsincbuf,msglen); // queue the packet 
				}
			}
		}
	}	

	// should not get here
	responsequeuedestruct(&targetqueuehead);
	return;
}

