#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "mcp.h"

thread_data_t minions[MAX_NUM_Minions];


uint64 htonll( uint64 id){
    unsigned char *bytes,temp;

    bytes=(unsigned char *)&id;

    temp=bytes[0];
    bytes[0]=bytes[7];
    bytes[7]=temp;
    
    temp=bytes[1];
    bytes[1]=bytes[6];
    bytes[6]=temp;
    
    temp=bytes[2];
    bytes[2]=bytes[5];
    bytes[5]=temp;
    
    temp=bytes[3];
    bytes[3]=bytes[4];
    bytes[4]=temp;
    
    return(id);
}

int main(int argc, char **argv) {
    int i, rc, mID,child,result,msglen,highest_minion,minnum,error;
    char buf[1024];
    char cbuf[1024];
    fd_set minion_fds;
    int minions_ready;
    struct timeval timeout;

    // MAX_NUM_Minions must be greater than minnum
    minnum = 2;

    DCMSG(RED,"MCP will start  %d minions",minnum);

    // loop until somebody wants to exit, or something
    while(1) {

	// determine what slaves are out there
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
		minion_thread(&minions[mID]);

		//	close(minions[mID].mcp);
	    }
	}

#if 1
	//  at this point we have a bunch of minions with open connections that
	//  we should be able to exploit.
	for (mID=0; mID<minnum; mID++){
	    if (minions[mID].status!=S_closed){
		sprintf(cbuf,"minion %d  expose!", mID);
		result=write(minions[mID].minion, cbuf, strlen(cbuf));
		if (result >= 0){
		    DCMSG(RED,"sent %d chars to minion %d  --%s--",strlen(cbuf), mID,cbuf);
		} else {
		    perror("writing stream message");
		}
	    }
	}
	for (mID=0; mID<minnum; mID++) {
	    if (minions[mID].status!=S_closed){
		sprintf(cbuf,"minion %d  conceal!", mID);
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
