// Support structures and functions for the smartqueue.
// Developed 12/2012 by Jesse Jensen
// for the atm project.

// Constants which control operation.

// Maximum number of queued queries that will be executed sequentially.
// This prevents queued queries from "hogging" the rf interface.
#define SMART_QUEUE_MAX_SEQUENTIAL	3

// Maximum number of round robin queries that will be executed sequentially.
// This prevents round robin queries from "hogging" the rf interface.
#define ROUND_ROBIN_MAX_SEQUENTIAL	3

// Round-robin check frequency (nominal) (seconds). This value specifies
// the elapsed time before a target is "due" for a check by the round-robin
// function. 
#define ROUND_ROBIN_FREQ 		5

// Socket selection timeout for the RCC-side thread (milliseconds). This
// timeout prevents waiting for socket input from "hogging" the rf
// interface.  
#define RCC_SELECT_TIMEOUT		50

// Command number (unused in rf.h) which currently is used for control
// packets between the smartqueue processes.
#define LBC_TRIGGER_PACKET		11

// Size of character buffers used to read/write sockets.
#define BufSize				1024

// Time between a command to a target and a subsequent query (milliseconds).
#define QueryDelay			2000 

// Delay to re-request a packet if not received (seconds).
#define REREQUEST_DELAY			3 

// Target list structures and functions.

// The RCC side of smartqueue program uses this capability to "learn"
// targets. As packets move in either direction through the RCC-side
// smartqueue thread, they are inspected and used to construct a simple,
// singly-connected linked list used for round-robin polling of the targets.

// linked list element to "remember" a target
struct targetelt {
	uint16 address;		// target address
	struct timeval touched; // last activity time
	struct targetelt *nex;
};

// Touch Target - Update the touched time. If the target is new it is added
// to the list and then touched. Generally this function is invoked every
// time the smartqueue handles a packet for a given target.

// CALLED BY: smartqueue_rcc_thread, processMCPpacket, roundrobin, processqueue
// IN: head: reference to pointer to target list head element.
//     addr: address of target to touch
void touchtarget(struct targetelt **head,uint16 addr) {
	struct targetelt *curr=*head;	//current pointer for list manipulation
	int found=0;			//search status

	if(addr==160 || addr==161) return;		//don't learn 160. It is used for request_new commands.

	//search for address provided	
	while (curr != NULL) {
		if(curr->address == addr) {
			found=1;
			gettimeofday(&(curr->touched),NULL); //update the time
		}
	}

	//add the new target if not found
	if(!found) {
		DCMSG(BLACK,"Smartqueue learning address 0x%x.",addr);
		if(*head == NULL) { //special case, first element
			*head=malloc(sizeof(struct targetelt));
			(*head)->address=addr;
			gettimeofday(&((*head)->touched),NULL); //update the time
			(*head)->nex=NULL;
		} else { //general case
			curr=*head;
			while (curr->nex != NULL) {
				curr=curr->nex;
			}
			curr->nex=malloc(sizeof(struct targetelt));
			curr=curr->nex;
			curr->address=addr;
			gettimeofday(&(curr->touched),NULL); //update the time
			curr->nex=NULL;
		}
	}	
}

// Destructor for target list.

// CALLED BY: smartqueue_rcc_thread
// IN: head: reference to pointer to target list head element.
void targetlistdestruct(struct targetelt **head)	//destructor
{
	struct targetelt *curr;
	curr=*head;

	while(*head != NULL)
	{
		*head=(*head)->nex;
		free(curr);
		curr=*head;	
	}
}


// Command queue structures and functions.

// The RCC side of the smartqueue program uses this capability to queue 
// queries based on anticipated responses from the targets. Outgoing 
// commands known to trigger response packets cause response queries to
// be added to this queue. Queued queries are stored in a
// singly-connected linked list.

// linked list element to queue a command
struct commandqueueelt {
	uint16 address;
	int attempts;
	struct timeval gotime;
	struct commandqueueelt *nex;
};

// Queue a query to happen in the future.

// CALLED BY: processMCPpacket
// IN: head: reference to pointer to command queue head.
//     addr: address of target to query.
//     delaymsec: time from now to query execution.
//     npackets: number of packets expected.
void queuequery(struct commandqueueelt **head,uint16 addr,int delaymsec,int npackets)
{
	struct commandqueueelt *curr=*head; 	
	struct timeval incby;
	timerclear(&incby);
	incby.tv_usec=1000*delaymsec; //delta t
	int i;

	DCMSG(BLACK,"Smartqueue queueing %d queries to addr: 0x%x",npackets,addr);
	for(i=0;i<npackets;i++) //add a query for each packet anticipated
	{
		//add an element to the query list
		if(*head==NULL) { //special case, first element
			*head=malloc(sizeof(struct commandqueueelt));
			(*head)->address=addr;
			(*head)->attempts=0;
			gettimeofday(&((*head)->gotime),NULL);
			timeradd(&incby,&((*head)->gotime),&((*head)->gotime));
			(*head)->nex=NULL;
		} else { //general case
			curr=*head;
			while (curr->nex != NULL) {
				curr=curr->nex;
			}
			curr->nex=malloc(sizeof(struct commandqueueelt));
			curr=curr->nex;
			curr->address=addr;
			curr->attempts=0;
			gettimeofday(&(curr->gotime),NULL);
			timeradd(&incby,&(curr->gotime),&(curr->gotime));
			curr->nex=NULL;
		}
	}
	DCMSG(BLACK,"Smartqueue done queueing for addr: 0x%x",addr);
}

// Process MCP packet - Invoked on packets passing from MCP to RF
// master, which are commands from SmartRange to the targets. If the
// packet is of a type known to evoke a response from the target, this 
// function queues up the query to obtain the response packets in the 
// future. It then passes the packet to RFmaster.

// CALLED BY: smartqueue_rcc_thread
// IN: mcpincbuf: buffer which contains the packet to be sent.
//     RF_sock: connected socket to RFmaster.
//     msglen: message length of packet - output from read function.
int processMCPpacket(char mcpincbuf [BufSize], int RF_sock, int msglen, struct targetelt **targetlisthead, struct commandqueueelt **commandqueuehead) {

	LB_packet_t *LB;
	LB_accessory_t *lba;

	LB=(LB_packet_t *)mcpincbuf; 

	//touch and/or learn the target
	touchtarget(targetlisthead,LB->addr);

	//parse the command
	switch (LB->cmd) {
		//These commands expect up to 3 packets as a response.
		//2100 CID_Expose_Request -> 2101 and 2102
		//2100 CID_Move_Request -> 2101 and 2102
		case LBC_EXPOSE:
		case LBC_MOVE:
			queuequery(commandqueuehead,LB->addr,QueryDelay,3);
		break;

		//These commands expect 1 packet as a response
		//2100 CID_Reset_Device -> 2101
		//2100 CID_Config_Hit_Sensor -> maybe 2102
		case LBC_RESET:
		case LBC_CONFIGURE_HIT:
			queuequery(commandqueuehead,LB->addr,QueryDelay,1);
		break;

		//Accessory commands, one packet as a response
		//2110 Configure Muzzle Flash -> 2101 (F) or 2112
		//2114 MSDH -> 2101 (F) or 2115
		//13110 Moonglow -> 2101 (F) or 13112
		//14110 Configure Muzzle Flash -> 2101 (F) or 14412
		//15110 Configure Thermals -> 2101 (F) or 15112
		case LBC_ACCESSORY:
			queuequery(commandqueuehead,LB->addr,QueryDelay,1);
		break;		
		
		//we only get here if the packet wasn't recognized by the switch statement.
		//simply pass on the packet.
		
		//Includes the following, which do not trigger RF activity from the minion and/or client:
		//100 Device Attributes
		//2100 CID_No_Event   -> 2101 
		//2100 CID_Reserved01 -> 2101
		//2100 CID_Status_Request -> 2102 and/or 2112
		//2100 CID_Stop -> nothing
		//2100 CID_Shutdown -> nothing
		//2100 CID_Sleep -> nothing
		//2100 CID_Wake -> nothing
		//2100 CID_Hit_Count_Reset -> nothing
		//14200 Hit Blanking -> nothing
		//2100 CID_GPS_Location_Request -> 2101
	}

	DCMSG(BLACK,"Smartqueue MCP receive addr: 0x%x, cmd: %d. Forwarding...",LB->addr,LB->cmd);
	write(RF_sock,mcpincbuf,msglen); //from legacy mcp

	return 0;
}

// Check Queue - Invoked on packets passing from RF Master to MCP, which
// are responses from the targets to SmartRange. If the packet was
// expected by the smart queue, this function removes the query for this
// packet from the queue so that it will not be requested again.

// CALLED BY: smartqueue_rcc_thread
// IN: head: reference to pointer to command queue head.
//     LB: reference to the packet under examination.
void checkqueue(struct commandqueueelt **head,LB_packet_t *LB)
{
	struct commandqueueelt *curr, *tmp;

	curr=*head;
	while (curr != NULL) {
		if(LB->addr == curr->address && curr->attempts > 0)
		{
			DCMSG(BLACK,"Smartqueue removing queued query for addr: 0x%x",LB->addr);
			//element deletion sequence
			if (curr == *head) //special case, delete first element
			{
				*head=(*head)->nex;
				free(curr);
			}
			else
			{
				tmp=*head;
				while(tmp->nex != curr && tmp != NULL) //find the previous element
				{
					tmp=tmp->nex;
				}

				//Assert: tmp now points to the element preceding that which is pointed to by curr
				tmp->nex=curr->nex;
				free(curr);
			}

			break; //only delete one element
		}

		curr=curr->nex;
	}
}

// Process Queue - Invoked regularly during the main loop of the RCC side
// thread. This function processes the number of stored queries specified
// in SMART_QUEUE_MAX_SEQUENTIAL.

// CALLED BY: smartqueue_rcc_thread
// IN: head: reference to pointer to command queue head.
//     RF_sock: connected socket to RF master.
//     targetlisthead: pointer to target list head.
void processqueue(struct commandqueueelt **head, int RF_sock, struct targetelt *targetlisthead)
{
	int cyclecount=0;
	struct commandqueueelt *curr,*execnext;
	time_t now,maxdiff;
	char rfcmdbuf[BufSize];
	LB_packet_t *LB;

	struct timeval incby;
	timerclear(&incby);
 
	incby.tv_sec=REREQUEST_DELAY; //stall for query repeat

	while(cyclecount < SMART_QUEUE_MAX_SEQUENTIAL)
	{
		//find the most overdue packet
		curr=*head;
		execnext=NULL;
		now=time(NULL);
		maxdiff=0;
		while (curr != NULL) {
			if(!(now%5)) {
				DCMSG(BLACK,"Queued content a: 0x%x at: %d n: %d d: %d",curr->address,curr->attempts,now,curr->gotime.tv_sec);
			}

			if(curr->gotime.tv_sec < now) { //this packet is due
				//calculate the difference & check 
				if(now - curr->gotime.tv_sec > maxdiff)
				{
					execnext=curr;
					maxdiff=curr->gotime.tv_sec - now;
				}
			}
			curr=curr->nex;
		}
		//execnext now points to the packet to process if any were found

		if(execnext != NULL)
		{
			if(execnext->attempts < 3)
			{
				//we send a special packet to the target requesting queued items
				LB=(LB_packet_t *)rfcmdbuf;
				LB->cmd=LBC_TRIGGER_PACKET;
				LB->addr=execnext->address;
				LB->payload[0]=0;
				touchtarget(&targetlisthead,LB->addr);
				DCMSG(BLACK,"Smartqueue triggering 0x%x",LB->addr);
				write(RF_sock,LB,3); //3 bytes allows null termination if needed
				(execnext->attempts)++;
				timeradd(&incby,&(execnext->gotime),&(execnext->gotime)); //push due time into the future
			} else {
				//element deletion sequence
				if (execnext == *head) //special case, delete first element
				{
					curr=*head;
					*head=(*head)->nex;
					free(curr);
				}
				else
				{
					curr=*head;
					while(curr->nex != execnext && curr != NULL) //find the previous element
					{
						curr=curr->nex;
					}
					//Assert: curr now points to the element preceding that which is pointed to by execnext
					curr->nex=execnext->nex;
					free(execnext);
				}
			}
		}		

		cyclecount++;
	}
}

// Round Robin - Invoked regularly during the main loop of the RCC side
// thread. This function processes the number of queries specified
// in ROUND_ROBIN_MAX_SEQUENTIAL. It moves through each detected target
// on a round-robin basis to assure periodic contact with targets that
// are not actively receiving commands.

// CALLED BY: smartqueue_rcc_thread
// IN: head: pointer to target list head.
//     rrdelay: sets delay (seconds) before a target will be polled
//		again by this function. This prevents the round robin
//		operation from flooding the channel during idle
//		periods.
//     RF_sock: connected socket to RF master.
void roundrobin(struct targetelt *head,int rrdelay,int RF_sock)
{
	//this function polls up to 3 targets in a row
	//on a round-robin basis.
	int cyclecount=0;
	struct targetelt *curr,*execnext;
	time_t now,maxdiff;
	char rfcmdbuf[BufSize];
	LB_packet_t *LB;

	while(cyclecount < ROUND_ROBIN_MAX_SEQUENTIAL)
	{
		//find the most overdue target
		curr=head;
		execnext=NULL;
		now=time(NULL);
		maxdiff=0;
		while (curr != NULL) {
			//DCMSG(BLACK,"Target %d: , n:%d, t:%d, d:%d r:%d",curr->address,now,curr->touched.tv_sec,now-curr->touched.tv_sec,rrdelay);
			if(now - curr->touched.tv_sec > rrdelay) { //this packet is due
				//DCMSG(BLACK,"Smartqueue RR target %d is due.",curr->address);
				//calculate the difference & check 
				if(now - curr->touched.tv_sec > maxdiff)
				{
					execnext=curr;
					maxdiff=now - curr->touched.tv_sec;
				}
			}
			curr=curr->nex;
		}
		//execnext now points to the target to process if any were found

		if(execnext != NULL)
		{
			DCMSG(BLACK,"Smartqueue RR to id 0x%x.",execnext->address);
			LB=(LB_packet_t *)rfcmdbuf;
			LB->cmd=LBC_TRIGGER_PACKET;
			LB->addr=execnext->address;
			LB->payload[0]=0;
			touchtarget(&head,LB->addr);
			write(RF_sock,LB,3); //3 bytes allows null termination if needed
		}

		cyclecount++;
	}

	return;
}

// Destructor for command queue.

// CALLED BY: smartqueue_rcc_thread
// IN: head: reference to pointer to target list head element.
void commandqueuedestruct(struct commandqueueelt **head)	//destructor
{
	struct commandqueueelt *curr;
	curr=*head;

	while(*head != NULL)
	{
		*head=(*head)->nex;
		free(curr);
		curr=*head;	
	}
}


// Response queue structures and functions.

// The target side of the smartqueue program uses this capability to queue 
// responses to be relayed when commanded by the RCC side. Response
// packets from the target's internal control system are captured and held
// in this queue. Queued responses are stored in a singly-connected linked
// list.

//list element to queue a response
struct responsequeueelt {
	char message[BufSize];
	int msglen;
	struct responsequeueelt *nex;
};

// targetPushQueueElt - Push a response packet into the queue. This 
// function is called when a packet is received from the slaveboss
// process, which originated from the target's internal control system.
// New response packets are added to the tail end of the linked list.

// CALLED BY: smartqueue_trg_thread
// IN: head: reference to pointer to response queue head.
//     sbsincbuf: reference to buffer containing the packet.
//     msglen: length of the packet, determined from socket read.
void targetPushQueueElt(struct responsequeueelt **head, char sbsincbuf[BufSize], int msglen)
{
	//This function adds a packet to the outbound queue on the target
	struct responsequeueelt *curr;

	if(*head==NULL) { //special case, nothing in queue
		*head=malloc(sizeof(struct responsequeueelt));
		strcpy((*head)->message,sbsincbuf);
		(*head)->msglen=msglen;
		(*head)->nex=NULL;
	} else {
		curr=*head;
		while(curr->nex != NULL) {
			curr=curr->nex;
		}
		//curr now points to last element
		curr->nex=malloc(sizeof(struct responsequeueelt));
		curr=curr->nex;
		strcpy(curr->message,sbsincbuf);
		curr->msglen=msglen;
		curr->nex=NULL;
	}
}

// targetPopQueueElt - Pop a response packet from the queue and write
// it to RFslave. This function is called when the target receives the
// special packet requesting it to transmit queued items. The "next"
// response packet is taken from the head end of the linked list.

// CALLED BY: smartqueue_trg_thread
// IN: head: reference to pointer to response queue head.
//     RFslave_sock: connected socket to RF slave.
void targetPopQueueElt(struct responsequeueelt **head,int RFslave_sock)
{
	//This function pops a packet from the target queue and writes it to RFslave
	struct responsequeueelt *curr;

	if(*head == NULL) //nothing to pop
		return;

	curr=*head;
	*head=(*head)->nex; //in single element case, this will nullify the head pointer

	write(RFslave_sock,curr->message,curr->msglen);
	free(curr);
}

// Destructor for response queue.

// CALLED BY: smartqueue_trg_thread
// IN: head: reference to pointer to response queue head element.
void responsequeuedestruct(struct responsequeueelt **head) //destructor
{
	struct responsequeueelt *curr;
	curr=*head;

	while(*head != NULL)
	{
		*head=(*head)->nex;
		free(curr);
		curr=*head;	
	}
}

