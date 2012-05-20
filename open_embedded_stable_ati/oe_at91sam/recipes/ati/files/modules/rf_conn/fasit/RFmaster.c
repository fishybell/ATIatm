#include <signal.h>
#include "mcp.h"
#include "rf.h"

#define MAX_EVENTS 16
#define MAX_CONNECTIONS 16

const char *__PROGRAM__ = "RFmaster ";

int verbose,rtime,collecting_time;    // globals

// this makes the warning go away
extern size_t strnlen (__const char *__string, size_t __maxlen)
__THROW __attribute_pure__ __nonnull ((1));

// tcp port we'll listen to for new connections
#define defaultPORT 4004

// tcp port we'll listen to for SmartRange radio interface
#define smartrangePORT 4008

// size of client buffer
#define CLIENT_BUFFER 1024

// size of mcp buffer
#define MCP_BUF_SIZE 16384

// kill switch to program
static int close_nicely = 0;
static void quitproc(int sig) {
   switch (sig) {
      case SIGINT:
         DCMSG(red,"Caught signal: SIGINT\n");
         break;
      case SIGQUIT:
         DCMSG(red,"Caught signal: SIGQUIT\n");
         break;
      default:
         DCMSG(red,"Caught signal: %i\n", sig);
         break;
   }
   close_nicely = 1;
}

void DieWithError(char *errorMessage){
   char buf[200];
   strerror_r(errno,buf,200);
   DCMSG(RED,"RFmaster %s %s \n", errorMessage,buf);
   exit(1);
}

void setblocking(int fd) {
   int opts, yes=1;

   // generic file descriptor setup
   opts = fcntl(fd, F_GETFL); // grab existing flags
   if (opts < 0) {
      DieWithError("fcntl(F_GETFL)");
   }
   opts = (opts ^ O_NONBLOCK); // remove nonblock from existing flags
   if (fcntl(fd, F_SETFL, opts) < 0) {
      DieWithError("fcntl(F_SETFL)");
   }
}

void setnonblocking(int fd) {
   int opts, yes=1;

   // generic file descriptor setup
   opts = fcntl(fd, F_GETFL); // grab existing flags
   if (opts < 0) {
      DieWithError("fcntl(F_GETFL)");
   }
   opts = (opts | O_NONBLOCK); // add nonblock to existing flags
   if (fcntl(fd, F_SETFL, opts) < 0) {
      DieWithError("fcntl(F_SETFL)");
   }
}

// add message to the write buffer
static void queueMsg(char *msgbuf, int *buflen, void *msg, int size) {

   // check buffer remaining...
   if ((size + *buflen) > MCP_BUF_SIZE) {
      // buffer not big enough
      return;
   }

   // append to existing message
   memcpy(msgbuf + (sizeof(char) * *buflen), msg, size);
   *buflen += size;
   DDCMSG(D_NEW, BLACK, "QUEUED: Msg size %i, bufsize: %i", size, *buflen);
}

/*************
 *************  HandleRF
 *************
 *************  when we first start, or if the we are idle for some amount of time,
 *************  we need to send 'request new devices' on the radio,
 *************  and register those devices.
 *************
 *************  At the MCP level, it really shouldn't start any minions until and unless
 *************  we have a registered slave device, and if that registered device dissappears
 *************  then after some suitable time the minion for it should die or hibernate or something
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

#define Rxsize 16384
#define Txsize 256
#define RF_BUF_SIZE 512

void HandleRF(int MCPsock,int risock, int *riclient,int RFfd){
   struct timespec elapsed_time, start_time, istart_time,delta_time, dwait_time, collect_time;
   queue_t *Rx,*Tx;
   int Queue_Contains[2048]; // used to see what commands the outbound queue contains. max of rf addr 2047, 0 is not used
   int seq=1;           // the packet sequence numbers

   char Rbuf[RF_BUF_SIZE];
   char buf[200];        /* text Buffer  */
   char  *Rptr,*Rstart;
   int end, Rsize=0, size,gathered,gotrf,delta,cdelta,remaining_time,bytecount,bytecount2;
   int MsgSize,result,sock_ready,pcount=0;                    /* Size of received message */
   fd_set rf_or_mcp;
   struct timeval timeout;
   int maxcps=500,rfcount=0;            /*  characters per second that we can transmit without melting - 1000 is about 100% */
   double cps;
   uint8 crc;
   int inittime=0,slottime=0,total_slots;
   int low_dev,high_dev;
   int burst,ptype,packets;
   LB_burst_t LBb;
   char mcpbuf[MCP_BUF_SIZE];
   int mcpbuf_len=0;
   // epoll stuff
   struct epoll_event ev, events[MAX_EVENTS]; // temporary event, main event list
   int efd, nfds, n; // file descriptors for sockets
   int ep_rf, ep_mcp, ep_ris, ep_ric; // fds marked as ready in epoll

   // setup epoll
   efd = epoll_create(MAX_CONNECTIONS);

   // add MCP socket to epoll
   memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN; // only for reading to start
   ev.data.fd = MCPsock; // remember for later
   if (epoll_ctl(efd, EPOLL_CTL_ADD, MCPsock, &ev) < 0) {
      EMSG("epoll MCPsock insertion error\n");
      close_nicely=1;
   }

   // add radio interface listener socket to epoll
   memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN; // only for reading to start
   ev.data.fd = risock; // remember for later
   if (epoll_ctl(efd, EPOLL_CTL_ADD, risock, &ev) < 0) {
      EMSG("epoll risock insertion error\n");
      close_nicely=1;
   }

   // add RF fd to epoll
   memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN; // only for reading to start
   ev.data.fd = RFfd; // remember for later
   if (epoll_ctl(efd, EPOLL_CTL_ADD, RFfd, &ev) < 0) {
      EMSG("epoll RFfd insertion error\n");
      close_nicely=1;
   }

   // packet header so we can determine the length from the command in the header
   LB_packet_t *LB;
   LB_request_new_t *LB_new;

   // initialize our gathering buffer
   Rptr=Rbuf;
   Rstart=Rptr;
   gathered=0;
   
   Rx=queue_init(Rxsize);       // incoming packet buffer
   Tx=queue_init(Txsize);       // outgoing Tx buffer

   memset(Rstart,0,100);
   memset(Queue_Contains, 0, 2048);

   remaining_time=100;          //  remaining time before we can transmit (in ms)

   /**   loop until we lose connection  **/
   clock_gettime(CLOCK_MONOTONIC,&istart_time); // get the intial current time
   timestamp(&elapsed_time,&istart_time,&delta_time);   // make sure the delta_time gets set    

#define TIMESTAMP_NOW {\
   timestamp(&elapsed_time,&istart_time,&delta_time); \
   /* now add my delta to my dwait */ \
   dwait_time.tv_sec+=delta_time.tv_sec;	 /* add seconds */ \
   dwait_time.tv_nsec+=delta_time.tv_nsec; /* add nanoseconds */ \
   if (dwait_time.tv_nsec>=1000000000L) {  /* fix nanoseconds */ \
      dwait_time.tv_sec++; \
      dwait_time.tv_nsec-=1000000000L; \
   } \
   /* now add my delta to my collect time */ \
   collect_time.tv_sec+=delta_time.tv_sec;	 /* add seconds */ \
   collect_time.tv_nsec+=delta_time.tv_nsec; /* add nanoseconds */ \
   if (collect_time.tv_nsec>=1000000000L) {  /* fix nanoseconds */ \
      collect_time.tv_sec++; \
      collect_time.tv_nsec-=1000000000L; \
   } \
}

   dwait_time.tv_sec=0; dwait_time.tv_nsec=0;   // make sure the dwait_time gets set
   while(!close_nicely) {

      TIMESTAMP_NOW;
      DDCMSG(D_TIME,CYAN,"Top of loop    %5ld.%03ld timestamp, delta=%5ld.%03ld, dwait=%5ld.%03ld, collect=%5ld.%03ld"
             ,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l, delta_time.tv_sec, delta_time.tv_nsec/1000000l, dwait_time.tv_sec, dwait_time.tv_nsec/1000000l, collect_time.tv_sec, collect_time.tv_nsec/1000000l);
      delta = (dwait_time.tv_sec*1000)+(dwait_time.tv_nsec/1000000l);
      cdelta = (collect_time.tv_sec*1000)+(collect_time.tv_nsec/1000000l);
      DDCMSG(D_TIME,CYAN,"delta=%2ims\tcdelta=%2ims", delta, cdelta);

#if 0 /* old select method */      
      /*   do a select to see if we have a message from either the RF or the MCP  */
      /* then based on the source, send the message to the destination  */
      /* create a fd_set so we can monitor both the mcp and the connection to the RCC*/
      FD_ZERO(&rf_or_mcp);
      FD_SET(MCPsock,&rf_or_mcp);               // we are interested hearing the mcp
      FD_SET(risock,&rf_or_mcp);                // we are interested hearing the Radio Interface listen socket
      if (*riclient > 0) {
         FD_SET(*riclient,&rf_or_mcp); // only add riclient to select when it exists
      }
      FD_SET(RFfd,&rf_or_mcp);          // we also want to hear from the RF world
#endif /* end of old select method */

      /*   actually for now we will block until we have data - no need to timeout [yet?]
       *     I guess a timeout that will make sense later is if the radio is ready for data
       *   and we have no MCP data to send, then we should make the rounds polling
       *   for status.  At the moment I don't want to impliment this at the RFmaster level,
       *   for my initial working code I am shooting for the MCP to do all the work and
       *   the RFmaster to just pass data back and forth.
       */


      TIMESTAMP_NOW;
      DDCMSG(D_TIME,YELLOW,"before select remaining_time=%d  Rx[%d] Tx[%d] @ time %5ld.%03ld"
             ,remaining_time,Queue_Depth(Rx),Queue_Depth(Tx), elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l);

      // check that we're waiting at least rtime   configurable via command line with the option -r
      if (remaining_time < rtime) { // look at rtime, maximum of collecting_time-cdelta
         DDCMSG(D_TIME|D_NEW, MAGENTA, "TIMEOUT: Looking at %i vs rtime of %i", collecting_time-cdelta, rtime);
         timeout.tv_sec=max(collecting_time-cdelta, rtime)/1000;
         timeout.tv_usec=(max(collecting_time-cdelta, rtime)%1000)*1000;
      } else if (remaining_time > delta) { // look at remaining_time-delta, maximum of collecting_time-cdelta
         DDCMSG(D_TIME|D_NEW, MAGENTA, "TIMEOUT: Looking at %i vs remaining_time-delta of %i", collecting_time-cdelta, remaining_time-delta);
         timeout.tv_sec=max(collecting_time-cdelta, (remaining_time-delta))/1000;
         timeout.tv_usec=(max(collecting_time-cdelta, (remaining_time-delta))%1000)*1000;
      } else { // look at remaining_time, maximum of collecting_time-cdelta
         DDCMSG(D_TIME|D_NEW, MAGENTA, "TIMEOUT: Looking at %i vs remaining_time of %i", collecting_time-cdelta, remaining_time);
         timeout.tv_sec=max(collecting_time-cdelta, remaining_time)/1000;
         timeout.tv_usec=(max(collecting_time-cdelta, remaining_time)%1000)*1000;
      }
      // if we don't have anything in the queue, don't timeout
      bytecount=Queue_Depth(Rx);
      bytecount2=Queue_Depth(Tx);
      TIMESTAMP_NOW;
      if (bytecount <= 0 && mcpbuf_len <= 0) {
         DDCMSG(D_NEW,CYAN,"BEFORE SELECT: in: %i, out: %i, mcp: %i, remaining_t=%d, elapsed_t=%3ld.%03ld, delta_t=%3ld.%03ld, dwait_t=%3ld.%03ld, timeout=INFINITE, ", bytecount, bytecount2, mcpbuf_len, remaining_time, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l, delta_time.tv_sec, delta_time.tv_nsec/1000000l, dwait_time.tv_sec, dwait_time.tv_nsec/1000000l);
         // new epoll method
         nfds = epoll_wait(efd, events, MAX_EVENTS, -1); // infinite timeout
#if 0 /* old select method */      
         sock_ready=select(FD_SETSIZE,&rf_or_mcp,(fd_set *) 0,(fd_set *) 0, NULL);
#endif /* end of old select method */
      } else {
         DDCMSG(D_NEW,CYAN,"BEFORE SELECT: in: %i, out: %i, mcp: %i, remaining_t=%d, elapsed_t=%3ld.%03ld, delta_t=%3ld.%03ld, dwait_t=%3ld.%03ld, timeout=%d.%03d, ", bytecount, bytecount2, mcpbuf_len, remaining_time, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l, delta_time.tv_sec, delta_time.tv_nsec/1000000l, dwait_time.tv_sec, dwait_time.tv_nsec/1000000l, timeout.tv_sec, timeout.tv_usec);
         if ((remaining_time-delta) <= 0) {
            // will cause an immediate return if we're ready to send another burst
            DDCMSG(D_NEW,MAGENTA,"GOING TO RETURN IMMEDIATELY FROM SELECT (%i-%i <= 0)", remaining_time, delta);
            timeout.tv_sec=0;
            timeout.tv_usec=0;
         }
         // new epoll method
         nfds = epoll_wait(efd, events, MAX_EVENTS, (timeout.tv_sec*1000) + (timeout.tv_usec/1000)); // calculate millisecond timeout
#if 0 /* old select method */      
         sock_ready=select(FD_SETSIZE,&rf_or_mcp,(fd_set *) 0,(fd_set *) 0, &timeout);
#endif /* end of old select method */
      }

      // new epoll method
      // find all waiting connections
      ep_rf=-1; ep_mcp=-1; ep_ris=-1; ep_ric=-1; // none marked yet
      for (n = 0; !close_nicely && n < nfds; n++) {
         DDCMSG(D_NEW, YELLOW, "Looking at %i in events...%i", n, events[n].data.fd);
         if (events[n].data.fd == RFfd) { // RFfd is ready...
            ep_rf = n; // ...at index n
         } else if (events[n].data.fd == MCPsock) { // MCPsock is ready...
            ep_mcp = n; // ...at index n
         } else if (events[n].data.fd == risock) { // risock is ready...
            ep_ris = n; // ...at index n
         } else if (*riclient > 0 && events[n].data.fd == *riclient) { // *riclient is ready...
            ep_ric = n; // ...at index n
         } else {
            DDCMSG(D_NEW, YELLOW, "events[%i].events: %i unknown: %i", n, events[n].events, events[n].data.fd);
            close_nicely = 1; // exit
         }
      }
      sock_ready = nfds; // new epoll method

      if (close_nicely) {break;}
      TIMESTAMP_NOW;
      DDCMSG(D_NEW,YELLOW,"select returned, sock_ready=%d @ time %5ld.%03ld",sock_ready, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l);
      if (sock_ready<0){
         strerror_r(errno,buf,200);
         DCMSG(RED,"RFmaster select error: %s", buf);
         exit(-1);
      }

      /*******      we timed out
       *******
       *******      check if the listening timeslots have expired
       *******      check if the radio is cold and has room for the next send
       *******/      
      //    we timed out.   
      if (!sock_ready){

         // get the actual current time.
         TIMESTAMP_NOW;
         DDCMSG(D_TIME,CYAN,"select timed out at %5ld.%03ld timestamp, delta=%5ld.%03ld"
                ,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l,delta_time.tv_sec, delta_time.tv_nsec/1000000l);
      delta = (dwait_time.tv_sec*1000)+(dwait_time.tv_nsec/1000000l);
      //   delta = (delta_time.tv_sec*1000)+(delta_time.tv_nsec/1000000l);
         DDCMSG(D_TIME,CYAN,"delta=%2ims",delta);

         DDCMSG(D_TIME,YELLOW,"select timed out  delta=%d remaining_time=%d  Rx[%d] Tx[%d]"
                ,delta,remaining_time,Queue_Depth(Rx),Queue_Depth(Tx));

         // if we timed out to process an RF transmission
         bytecount=Queue_Depth(Rx);
         if (bytecount <= 0 && mcpbuf_len <= 0) {
            TIMESTAMP_NOW;
            DDCMSG(D_NEW,GRAY,"Timed out, but didn't do anything: elapsed_t=%5ld.%03ld delta_t=%5ld.%03ld, dwait_t=%5ld.%03ld, bytecount: %i, mcpbuf_len: %i"
                   ,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l, delta_time.tv_sec, delta_time.tv_nsec/1000000l, dwait_time.tv_sec, dwait_time.tv_nsec/1000000l, bytecount, mcpbuf_len);
         }

         if (bytecount > 0) {


            /***************     TIME to send on the radio
             ***************     
             ***************    more simple more better algorithm
             ***************    
             ***************    
             ***************      everything else in chonological order
             ***************    type #1 should be relativly seldom and can be sent alone or tacked to the end of a string of commands
             ***************            it expects up to  ((slottime+1)*(high_dev-low_dev)) responses time to wait
             ***************            
             ***************    type #2 and 3 can all be strung together now up to less than about 250 bytes
             ***************            to be sent as a 'burst'
             ***************
             ***************    when they are read from the MCP they should be  2 seperate queues
             ***************     or, we could even do it with one queue we are able to send-flush anything previous expecting a response
             ***************    then we follow it up with just the request new devices packet which expects a big string of responses
             ***************
             ***************
             ***************
             ******
             ******    we used to have multiple queues and sorting and sequence numbers, but that was a bad idea.
             ******
             ******    all we have to do is have our recieve queue, and put just what we want in the transmit queue.
             ******
             ******    Algorithm:  for up to 239 bytes, as long as the packets are not type #1 (request new devices) we
             ******                put them in the Tx queue.
             ******                If we do have a #1 then we:
             ******                a) if there are any #2's in the current Tx queue, then we must send the current queue.
             ******                b) if there are not any #2's, we tack the #1 on the end and send it.
             ******
             ******                much simpler, much better, more goodness
             ******                we only have to keep track if there are any type #2's in the queue or not.
             ******  
             ******               
             ***************/


            /******              do enough parsing at this point to decide if the packet is type 1,2, or 3.
             ******              we also have fully parse and record the settings in LBC_REQUEST_NEW that are
             ******               meant for us
             ******/

            DDCMSG(D_MEGA,YELLOW,"ptype rx=%d before Rx to Tx.  Rx[%d] Tx[%d]",QueuePtype(Rx),Queue_Depth(Rx),Queue_Depth(Tx));

            // loop until we are out of complete packets, and place them into the Tx queue
            burst=3;            // remembers if we upt a type2 into the burst
            packets=0;          // remembers the number of packets in a burst
            remaining_time = 50;       // reset our remaining_time before we are allowed to Tx again   time needs to be smarter

            while((ptype=QueuePtype(Rx))&&              /* we have a complete packet */
                  burst &&                              /* and burst is not 0 (0 forces us to send) */
                  (Queue_Depth(Tx)<239)&&               /*  room for more */
                  !((ptype==1)&&(burst==2))                           /* and if we are NOT a REQUEST_NEW.   requests get bursted like everything else */
                 ){     


               DDCMSG(D_MEGA,CYAN,"in loop.  Rx[%d] Tx[%d]",Queue_Depth(Rx),Queue_Depth(Tx));
               if (ptype==1){   /*  parse it to get the slottime and devid range, and set burst to 0 */
                  LB_new =(LB_request_new_t *) Rx->head;        // we need to parse out the slottime, high_dev and low_dev from this packet.
                  inittime=LB_new->inittime*5;          // convert passed initial time back to milliseconds
                  slottime=LB_new->slottime*5;          // convert passed slottime back to milliseconds
                  low_dev=LB_new->low_dev;
                  total_slots=10;               // total number of slots with end padding
                  burst=0;
                  if (burst == 3) {
                     // no existing stuff in out-queue this burst
                     remaining_time =(total_slots)*slottime;       // set up the timer
                     DDCMSG(D_TIME,YELLOW,"setting remaining_time to %d  total_slots=%d slottime=%d",
                         remaining_time,total_slots,slottime);
                  } else {
                     // existing stuff in out-queue this burst
                     remaining_time +=(total_slots)*slottime;       // set up the timer
                     DDCMSG(D_TIME,YELLOW,"adding remaining_time to %d  total_slots=%d slottime=%d",
                         remaining_time,total_slots,slottime);
                  }
                  ReQueue(Tx,Rx,RF_size(LB_new->cmd));  // move it to the Tx queue
                  packets++;


               } else {
                  LB=(LB_packet_t *)Rx->head;                   
                  if (ptype==2) {
                     burst=2;
                     remaining_time +=slottime; // add time for a response to this one
                     DDCMSG(D_TIME,YELLOW,"incrementing remaining_time by slottime to %d  slottime=%d",
                            remaining_time,slottime);

                     // no longer in Rx queue; un-mark
                     DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: %i currently in queue for addr %i (%p)", LB->cmd, LB->addr, Queue_Contains[LB->addr]);
                     Queue_Contains[LB->addr] ^= (1<<LB->cmd);
                     DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: %i no longer in queue for addr %i (%p)", LB->cmd, LB->addr, Queue_Contains[LB->addr]);
                  }                         
                  ReQueue(Tx,Rx,RF_size(LB->cmd));      // move it to the Tx queue
                  packets++;
               }
            }  // end of while loop to build the Tx packet

            if (packets > 0) {
               // build burst header and put in front of transmission (always, even if only one message is being sent)
               LBb.cmd = LBC_BURST;
               LBb.number=packets&0x7f; // max of 7 bits for number of packets
               set_crc8(&LBb);
               QueuePush(Tx, &LBb, RF_size(LBb.cmd));

               /***********    Send the RF burst
                ***********
                ***********/

               DDCMSG(D_MEGA,CYAN,"before Tx to RF.  Tx[%d]",Queue_Depth(Tx));
               DDCMSG(D_TIME, BLACK, "Adding initial time to remaining time: %d %d", remaining_time, inittime);
               remaining_time += (inittime*2) + (364*2); // add initial delay time (for beginning and end) and DTXM measured delaya (for beggining and end)

               if (Queue_Depth(Tx)){  // if we have something to Tx, Tx it.
                  TIMESTAMP_NOW;
                  DDCMSG(D_TIME, YELLOW, "TX @ Remaining time: %d %d, Elapsed time: %5ld.%03ld ", remaining_time, slottime, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l);

                  // when we're sending, we're busy, tell the Radio Interface client
                  if (*riclient > 0) {
                     char ri_buf[128];
                     snprintf(ri_buf, 128, "B %i\n", (5 * (Queue_Depth(Tx)) / 3) + 37 + remaining_time); // number of bytes * baud rate = milliseconds (9600 baud / 2 for overhead => 600 bytes a second => 5/3 second for 1000 bytes) + transmit delays
                     DDCMSG(D_RF|D_VERY, BLACK, "writing riclient %s", ri_buf);
                     result=write(*riclient, ri_buf, strnlen(ri_buf, 128));
                     // add radio interface client socket to epoll
                     memset(&ev, 0, sizeof(ev));
                     ev.events = EPOLLIN; // only for reading to start
                     ev.data.fd = *riclient; // remember for later
                  }

                  setblocking(RFfd);
                  result=write(RFfd,Tx->head,Queue_Depth(Tx));
                  setnonblocking(RFfd);
                  if (result<0){
                     strerror_r(errno,buf,200);                
                     DCMSG(RED,"write Tx queue to RF error %s",buf);
                  } else {
                     if (!result){
                        DCMSG(RED,"write Tx queue to RF returned 0");
                     }
                     if (verbose&D_RF){
                           sprintf(buf,"[%03d] %4ld.%03ld  ->RF [%2i] ",D_PACKET,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l,result);
                           printf("%s",buf);
                           printf("\x1B[3%d;%dm",(BLUE)&7,((BLUE)>>3)&1);
                           if(result>1){
                              for (int i=0; i<result-1; i++) printf("%02x.", (uint8) Tx->head[i]);
                           }
                           printf("%02x\n", (uint8) Tx->head[result-1]);
                     }  
                     if (verbose&D_PARSE){
                        DDpacket(Tx->head,result);
                     } else {
                        DDCMSG(D_NEW, GREEN, "\t->\tTransmitted %i bytes over RF", result);
                     }
                  }
                  //DeQueue(Tx,Queue_Depth(Tx));
                  ClearQueue(Tx);

               }
               TIMESTAMP_NOW;
               // adjust dwait_time to the time to nothing, as we haven't waited anything yet
               dwait_time.tv_sec=0; dwait_time.tv_nsec=0;
               DDCMSG(D_NEW,CYAN,"Just might have Tx'ed to RF   at %5ld.%03ld timestamp, delta=%5ld.%03ld, dwait=%5ld.%03ld"
                      ,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l, delta_time.tv_sec, delta_time.tv_nsec/1000000l, dwait_time.tv_sec, dwait_time.tv_nsec/1000000l);
               /*****    this stuff below isn't really tested  *****/              
               rfcount+=MsgSize;
               cps=(double) rfcount/(double)elapsed_time.tv_sec;
               DDCMSG(D_TIME,CYAN,"RFmaster average cps = %f.  max at current duty is %d",cps,maxcps);
            } else {
               DDCMSG(D_NEW,GREEN,"Failed to send any messages from Rx buffer of size %i", Queue_Depth(Rx));
               DDCMSG_HEXB(D_NEW,GREEN,"Rx buffer: ",Rx->head,Queue_Depth(Rx));
            }
         }
         // if we timed out to process MCP messages
         if (mcpbuf_len > 0) {
            result=write(MCPsock,mcpbuf,mcpbuf_len);
            if (result==mcpbuf_len) {
               // debug stuff
               if (verbose&D_RF){
                  sprintf(buf,"[%03d] %4ld.%03ld ->MCP [%2i] ",D_PACKET,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l,result);
                  printf("%s",buf);
                  printf("\x1B[3%d;%dm",(BLUE)&7,((BLUE)>>3)&1);
                  if(result>1){
                     for (int i=0; i<result-1; i++) printf("%02x.", (uint8) mcpbuf[i]);
                  }
                  printf("%02x\n", (uint8) mcpbuf[result-1]);
               }  
               if (verbose&D_PARSE){
                  DDpacket(mcpbuf,result);
               } else {
                  DDCMSG(D_NEW, GREEN, "\t<>\tSent %i bytes to MCP", result);
               }
               // clear out mcp buffer
               mcpbuf_len = 0;
            } else if (result > 0) {
               // clear out what was sent
               char tbuf[MCP_BUF_SIZE];
               memcpy(tbuf, mcpbuf + (sizeof(char) * result), mcpbuf_len - result);
               memcpy(mcpbuf, tbuf, mcpbuf_len - result);
               mcpbuf_len -= result;
            } else {
               sprintf(buf,"RF ->MCP  [%d!=%d]  ",mcpbuf_len,result);
               DDCMSG_HEXB(D_RF,RED,buf,mcpbuf,mcpbuf_len);
               close(MCPsock);    /* Close socket */    
               return;            /* go back to the main loop waiting for MCP connection */
            }
         }
      }

      // read range interface client
      TIMESTAMP_NOW;
      DDCMSG(D_TIME,BLACK,"RFmaster checking FD_ISSET(riclient:%i)=%i @ time %5ld.%03ld", *riclient, ep_ric, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l);
#if 0 /* old select method */
      if (*riclient > 0 && FD_ISSET(*riclient,&rf_or_mcp))
#endif /* end of old select method */
      if (*riclient > 0 && ep_ric != -1) {
         DDCMSG(D_NEW,BLACK,"RFmaster FD_ISSET(riclient)=%i @ time %5ld.%03ld", ep_ric, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l);
         int size;
         char buf[128];
         int err;
         size=read(*riclient,buf,128);
         err = errno;
         if (size <= 0 && err != EAGAIN) {
            DDCMSG(D_NEW, YELLOW, "Range Interface %i dead @ line %i", *riclient, __LINE__);
            close(*riclient);
            *riclient = -1;
            // new epoll method
            epoll_ctl(efd, EPOLL_CTL_DEL, *riclient, NULL);
         } else {
            DDCMSG(D_RF|D_VERY, YELLOW, "Read from Range Interface %i bytes: %s", size,buf);
         }
      }

      // accept range interface client
      TIMESTAMP_NOW;
      DDCMSG(D_TIME,BLACK,"RFmaster checking FD_ISSET(risock:%i)=%i @ time %5ld.%03ld", risock, ep_ris, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l);
#if 0 /* old select method */
      if (FD_ISSET(risock,&rf_or_mcp))
#endif /* end of old select method */
      if (ep_ris != -1) {
         DDCMSG(D_NEW,BLACK,"RFmaster FD_ISSET(risock)=%i @ time %5ld.%03ld", ep_ris, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l);
         int newclient = -1;
         struct sockaddr_in ClntAddr;   /* Client address */
         unsigned int clntLen;               /* Length of client address data structure */
         // close existing one
         if (*riclient > 0) {
            DDCMSG(D_NEW, YELLOW, "Range Interface %i dead @ line %i", *riclient, __LINE__);
            close(*riclient);
            *riclient = -1;
            // new epoll method
            epoll_ctl(efd, EPOLL_CTL_DEL, *riclient, NULL);
         }
         if ((newclient = accept(risock, (struct sockaddr *) &ClntAddr,  &clntLen)) > 0) {
            int yes=1;
            // replace existing riclient with new one
            *riclient = newclient;
            DDCMSG(D_RF|D_VERY, BLACK, "new working riclient %i from %s", *riclient, inet_ntoa(ClntAddr.sin_addr));
            setsockopt(*riclient, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int)); // set keepalive so we disconnect on link failure or timeout
            setnonblocking(*riclient);
            write(*riclient, "V\n", 2); // we're valid if we connect here
            // new epoll method
            if (epoll_ctl(efd, EPOLL_CTL_ADD, *riclient, &ev) < 0) {
               EMSG("epoll *riclient insertion error\n");
               //close_nicely=1; -- not on riclient, it's just not worth it
               close(*riclient);
               *riclient = 1;
               epoll_ctl(efd, EPOLL_CTL_DEL, *riclient, NULL);
            }
         }// if error, ignore
      }

      //  Testing has shown that we generally get 8 character chunks from the serial port
      //  when set up the way it is right now.
      // we must splice them back together
      //  which means we have to be aware of the Low Bandwidth protocol
      //  if we have data to read from the RF, read it then blast it back upstream to the MCP
      TIMESTAMP_NOW;
      DDCMSG(D_TIME,BLACK,"RFmaster checking FD_ISSET(RFfd:%i)=%i @ time %5ld.%03ld", RFfd, ep_rf, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l);
#if 0 /* old select method */
      if (FD_ISSET(RFfd,&rf_or_mcp))
#endif /* end of old select method */
      if (ep_rf != -1) {
         DDCMSG(D_NEW,BLACK,"RFmaster FD_ISSET(RFfd)=%i @ time %5ld.%03ld", ep_rf, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l);

         // while gathering RF data, we're busy, tell the Radio Interface client
         if (*riclient > 0) {
            DDCMSG(D_RF|D_VERY, BLACK, "writing riclient B\\n");
            write(*riclient, "B\n", 2);
         }

         DDCMSG(D_MEGA,BLACK,"RFmaster FD_ISSET(RFfd)");
         /* Receive message, or continue to recieve message from RF */
         Rptr = Rbuf + Rsize;
         if ((gotrf = gather_rf(RFfd,Rptr,RF_BUF_SIZE-Rsize)) > 0) {
            Rsize += gotrf;
            Rptr = Rbuf + Rsize;
         } else {
            Rsize = Rptr - Rbuf;
         }

         if (gotrf <= 0) {
            DDCMSG(D_NEW,BLACK,"RFmaster RF Received no data: %p %i", riclient, gotrf);
         } else {
            DDCMSG(D_NEW,BLACK,"RFmaster RF Received %i bytes", gotrf);
         }

         // after gathering RF data, we're free, tell the Radio Interface client
         DDCMSG(D_NEW,BLACK,"RFmaster got to here 1...");
         if (gotrf <= 0) {
            DDCMSG(D_NEW,BLACK,"RFmaster got to here 3...");
            DDCMSG(D_NEW,BLACK,"clowing riclient %i", *riclient);
            close(*riclient);
            *riclient = -1;
            // new epoll method
            epoll_ctl(efd, EPOLL_CTL_DEL, *riclient, NULL);
         } else if (*riclient > 0) {
            DDCMSG(D_RF|D_VERY, BLACK, "writing riclient %i F\\n", *riclient);
            write(*riclient, "F\n", 2);
         }
         DDCMSG(D_NEW,BLACK,"RFmaster got to here 2...");

         /* send valid message(s) to mcp */
         Rstart = Rbuf;
         while (Rstart < (Rptr - 2)) { /* look up until we don't have enough characters for a full message */
            DDCMSG(D_NEW,BLACK,"RFmaster got to here as well...%p %p", Rstart, Rptr);
            LB=(LB_packet_t *)Rstart;   // map the header in
            size=RF_size(LB->cmd);
            crc=(size > 2) ? crc8(LB) : 1; // only calculate crc if we have a valid size
            if (!crc) {
               if (LB->cmd == LBC_DEVICE_REG) {
                  DDCMSG(D_NEW,GRAY,"clearing what we think the queue contains for %i %p", LB->addr, Queue_Contains[LB->addr]);
                  // clear out what we think the queue contains when we have a device register itself
                  LB_device_reg_t *reg = (LB_device_reg_t*)Rstart;
                  Queue_Contains[LB->addr] = 0;
               }
               queueMsg(mcpbuf, &mcpbuf_len, Rstart, size); // queue for transmission later
               #if 0 /* start of old, pre-queue, method */
               result=write(MCPsock,Rstart,size);
               if (result==size) {
                  if (verbose&D_RF){
                     sprintf(buf,"[%03d] %4ld.%03ld ->MCP [%2i] ",D_PACKET,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l,result);
                     printf("%s",buf);
                     printf("\x1B[3%d;%dm",(BLUE)&7,((BLUE)>>3)&1);
                     if(result>1){
                        for (int i=0; i<result-1; i++) printf("%02x.", (uint8) Rstart[i]);
                     }
                     printf("%02x\n", (uint8) Rstart[result-1]);
                  }  
                  if (verbose&D_PARSE){
                     DDpacket(Rstart,result);
                  }
               } else {
                  sprintf(buf,"RF ->MCP  [%d!=%d]  ",size,result);
                  DDCMSG_HEXB(D_RF,RED,buf,Rstart,size);
               }
               #endif /* end of old, pre-queue, method */
               /* remove message, and everything before it, from buffer */
               end = (Rstart - Rbuf) + size;
               DDCMSG(D_NEW, BLACK, "About to clear msg from buffer %i, %i, %i, %08x, %08x, %08x", end, size, Rsize, Rbuf, Rstart, Rptr);
               if (end >= (Rptr - Rbuf)) {
                  DDCMSG(D_MEGA, RED, "clearing entire buffer");
                  // clear the entire buffer
                  Rptr = Rbuf;
               } else {
                  // clear out everything up to and including end
                  char tbuf[RF_BUF_SIZE];
                  DDCMSG(D_MEGA, RED, "clearing buffer partially: %i-%i", (Rptr - Rbuf), end);
                  memcpy(tbuf, Rbuf + (sizeof(char) * end), (Rptr - Rbuf) - end);
                  memcpy(Rbuf, tbuf, (Rptr - Rbuf) - end);
                  Rstart = Rbuf;
                  Rptr -= end;
               }
               Rsize = Rptr - Rbuf;
               DDCMSG(D_NEW, BLACK, "After msg cleared from buffer %i, %i, %i, %08x, %08x, %08x", end, size, Rsize, Rbuf, Rstart, Rptr);
            } else {
               Rstart++;
            }
         }

#if 0
         // while gathering RF data, we're busy, tell the Radio Interface client
         if (*riclient > 0) {
            DDCMSG(D_RF|D_VERY, BLACK, "writing riclient B\\n");
            write(*riclient, "B\n", 2);
         }

         DDCMSG(D_MEGA,BLACK,"RFmaster FD_ISSET(RFfd)");
         /* Receive message, or continue to recieve message from RF */

         gotrf = gather_rf(RFfd,Rptr,300);
         if (gotrf>0){  // increment our current pointer
            DDCMSG(D_VERY,GREEN,"gotrf=%d gathered =%2i, incrementing  Rptr=%2i (%p)  Rbuf=%p",
                   gotrf,gathered,(int)(Rptr-Rbuf), Rptr, Rbuf);
            Rptr+=gotrf;
            gathered+=gotrf;
            DDCMSG(D_VERY,GREEN,"gotrf=%d gathered =%2i  Rptr=%2i (%p) Rbuf=%p ",
                   gotrf,gathered,(int)(Rptr-Rbuf), Rptr, Rbuf);
         } else {
            DDCMSG(D_VERY,RED,"gotrf=%d gathered =%2i  Rptr=%2i (%p) ",
                   gotrf,gathered,(int)(Rptr-Rbuf), Rptr);
         }
         /* Receive message, or continue to recieve message from RF */
         DDCMSG(D_VERY,YELLOW,"gotrf=%d  gathered=%2i  Rptr=%2i Rstart=%2i Rptr-Rstart=%2i  ",
                gotrf,gathered,(int)(Rptr-Rbuf),(int)(Rstart-Rbuf),(int)(Rptr-Rstart));

         // after gathering RF data, we're free, tell the Radio Interface client
         if (*riclient > 0) {
            DDCMSG(D_RF|D_VERY, BLACK, "writing riclient F\\n");
            write(*riclient, "F\n", 2);
         }

         if (gathered>=3){
            // we have a chance of a compelete packet
            LB=(LB_packet_t *)Rstart;   // map the header in
            size=RF_size(LB->cmd);

            /* Receive message, or continue to recieve message from RF */
            DDCMSG(D_VERY,GREEN,"cmd=%d addr=%d RF_size =%2i  Rptr-Rstart=%2i  ",
                   LB->cmd,LB->addr,size,(int)(Rptr-Rstart));

            if ((Rptr-Rstart) >= size){
               //  we do have a complete packet
               // we could check the CRC and dump it here

               crc=crc8(LB);
               if (!crc) {
                  result=write(MCPsock,Rstart,size);
                  if (result==size) {
                     if (verbose&D_RF){
                        sprintf(buf,"[%03d] %4ld.%03ld ->MCP [%2i] ",D_PACKET,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l,result);
                        printf("%s",buf);
                        printf("\x1B[3%d;%dm",(BLUE)&7,((BLUE)>>3)&1);
                        if(result>1){
                           for (int i=0; i<result-1; i++) printf("%02x.", (uint8) Rstart[i]);
                        }
                        printf("%02x\n", (uint8) Rstart[result-1]);
                     }  
                     if (verbose&D_PARSE){
                        DDpacket(Rstart,result);
                     }
                     
                  } else {
                     sprintf(buf,"RF ->MCP  [%d!=%d]  ",size,result);
                     DDCMSG_HEXB(D_RF,RED,buf,Rstart,size);
                  }

                  if ((Rptr-Rstart) > size){
                     Rstart+=size; // step ahead to the next packet
                     gathered-=size;
                     DDCMSG(D_VERY,RED,"Stepping to next packet, Rstart=%d Rptr=%d size=%d ",(int)(Rstart-Rbuf),(int)(Rptr-Rbuf),size);
                     sprintf(buf,"  Next 8 chars in Rbuf at Rstart  ");
                     DDCMSG_HEXB(D_VERY,RED,buf,Rstart,8);

                  } else {
                     gathered=0;
                     Rptr=Rstart=Rbuf;     // reset to the beginning of the buffer
                     DDCMSG(D_VERY,RED,"Resetting to beginning of Rbuf, Rstart=%d Rptr=%d size=%d ",(int)(Rstart-Rbuf),(int)(Rptr-Rbuf),size);
                  }
                  
               } else {
                  sprintf(buf,"  BAD CRC packet ignored, step ahead by 1 ");
                  DDCMSG_HEXB(D_RF,RED,buf,Rstart,size);
                  Rstart++; // step ahead to the next packet
                  gathered--;

               }

            } else { // if (Rptr-Rstart) > size)
               DDCMSG(D_VERY,RED,"we do not have a complete RF packet, keep gathering ");
            }
         }  // if gathered >=3
#endif
      } // if this fd is ready

      /***************     reads the message from MCP into the Rx Queue
       ***************/
      TIMESTAMP_NOW;
      DDCMSG(D_TIME,BLACK,"RFmaster checking FD_ISSET(MCPsock:%i)=%i @ time %5ld.%03ld", MCPsock, ep_mcp, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l);
#if 0 /* old select method */
      if (FD_ISSET(MCPsock,&rf_or_mcp))
#endif /* end of old select method */
      if (ep_mcp != -1) {
         int err;
         DDCMSG(D_NEW,BLACK,"RFmaster FD_ISSET(MCPsock)=%i @ time %5ld.%03ld", ep_mcp, elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l);
         /* Receive message from MCP and read it directly into the Rx buffer */
         MsgSize = recv(MCPsock, Rx->tail, Rxsize-(Rx->tail-Rx->buf),0);
         err=errno;

         if (verbose&D_PACKET){
            sprintf(buf,"[%03d] %4ld.%03ld MCP-> [%2i] ",D_PACKET,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l,MsgSize);
            printf("%s",buf);
            printf("\x1B[3%d;%dm",(GREEN)&7,((GREEN)>>3)&1);
            if(MsgSize>1){
               for (int i=0; i<MsgSize-1; i++) printf("%02x.", (uint8) Rx->tail[i]);
            }
            printf("%02x\n", (uint8) Rx->tail[MsgSize-1]);
         }

         if (MsgSize<0){
            strerror_r(err,buf,200);
            DCMSG(RED,"read from MCP fd=%d failed  %s ",MCPsock,buf);
            sleep(1);
         }
         if (!MsgSize){
            DCMSG(RED,"read from MCP returned 0 - MCP closed");
            free(Rx);
            free(Tx);
            close(MCPsock);    /* Close socket */    
            return;                     /* go back to the main loop waiting for MCP connection */
         }

         if (MsgSize){

            /***************    Sorting is needlessly complicated.
             ***************       Just queue it up in our Rx queue as long as it doesn't
             ***************       exist already in the queue.
             ***************
             ***************       I suppose it should check for fullness
             ***************
             ***************    there might also need to be some kind of throttle to slow the MCP if we are full.
             ***************    
             ***************    */

            // check against our existing messages in the queue
            while (MsgSize > 0) {
               LB=(LB_packet_t *)Rx->tail;   // map the header in
               ptype = Ptype(Rx->tail);
               if (ptype==2) {
                  // only type 2 messages can be rejected from queue: they are the only 
                  //   messages that cause wait time and have only one destination.
                  //   they are also 100% redundant to have in multiple times.
                  if (Queue_Contains[LB->addr] & (1<<LB->cmd)) {
                     // already in queue, remove from Rx buffer
                     if (RF_size(LB->cmd) < MsgSize) { /* only remove if we're not the end */
                        char tbuf[RF_BUF_SIZE];
                        int rs = MsgSize - RF_size(LB->cmd); // move the buffer after this message to the tail
                        memcpy(tbuf, Rx->tail + RF_size(LB->cmd), rs);
                        memcpy(Rx->tail, tbuf, rs);
                     }
                     DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: Rejecting %i from queue for addr %i (%p)", LB->cmd, LB->addr, Queue_Contains[LB->addr]);
                     // don't move the tail, as we'll need to check the next message which is now at tail
                  } else {
                     // not in queue already, add and mark
                     Rx->tail+=RF_size(LB->cmd);
                     DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: Going to allow %i in queue for addr %i (%p)", LB->cmd, LB->addr, Queue_Contains[LB->addr]);
                     Queue_Contains[LB->addr] |= (1<<LB->cmd);
                     DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: Allowing %i in queue for addr %i (%p)", LB->cmd, LB->addr, Queue_Contains[LB->addr]);
                  }
               } else if (ptype==0) {
                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i with %i %i (%p)", __FILE__, __LINE__, LB->cmd, LB->addr, Queue_Contains[LB->addr]);
                  // type 0 messages are Always rejected from the queue, and they may remove others as well
                  if (LB->cmd == LBC_ILLEGAL_CANCEL && (Queue_Contains[LB->addr] & (1<<LBC_STATUS_REQ))) {
                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i", __FILE__, __LINE__);
                     // illegal-cancel causes all status requests for a target to be cancelled
                     // find the offending items in the queue, and clear it out
                     char *ts = Rx->head;
                     DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: Rejecting %i from queue for addr %i, and removing STAT_REQs", LB->cmd, LB->addr);
                     while (ts < Rx->tail) {
                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i with %p", __FILE__, __LINE__, ts);
                        LB_packet_t *lb=(LB_packet_t *)ts;   // map the header in
                        if (lb->cmd == LBC_STATUS_REQ && lb->addr == LB->addr) {
                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i with %p", __FILE__, __LINE__, ts);
                           // found our culprit, move the queue to engulf it
                           char tbuf[RF_BUF_SIZE];
                           int rs = (Rx->tail + MsgSize) - (ts + RF_size(lb->cmd)); // move entire buffer after the message, not just what's in the queue
                           DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: Removing %i from queue for addr %i (%i-%i...)", lb->cmd, lb->addr, Queue_Depth(Rx), RF_size(lb->cmd)); memcpy(tbuf, ts + RF_size(lb->cmd), rs);
                           memcpy(ts, tbuf, rs);
                           Rx->tail -= RF_size(lb->cmd); // move the tail down
                           LB=(LB_packet_t *)Rx->tail;   // re-map the header in to the new tail position
                           DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: Removed from queue (...=%i)", Queue_Depth(Rx));
                           Queue_Contains[LB->addr] ^= (1<<LBC_STATUS_REQ);
                        } else {
                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i with %p", __FILE__, __LINE__, ts);
                           ts += RF_size(lb->cmd); // not this message, maybe the next?
                        }
                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i with %p", __FILE__, __LINE__, ts);
                     }
                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i", __FILE__, __LINE__);
                  }
                  DDCMSG(D_POINTER, GREEN, "Here...%s:%i", __FILE__, __LINE__);
               } else {
                  Rx->tail+=RF_size(LB->cmd);  // add on the new length
               }
               MsgSize-=RF_size(LB->cmd); // we've looked at this message, now look for more
            }
            // check to see if we're the first of many messages from the mcp
            if (bytecount == 0 && Queue_Depth(Rx) > 0) {
               // adjust collect_time to the time to nothing, as we haven't waited anything yet
               collect_time.tv_sec=0; collect_time.tv_nsec=0;
            }
            bytecount=Queue_Depth(Rx);
            DDCMSG(D_QUEUE,YELLOW,"Rx[%d]",bytecount);
         }  // if msgsize was positive

      }  // end of MCP_sock



      
   } // end while !close_nicely
   
   close(MCPsock);
   if (*riclient > 0) {
      DDCMSG(D_NEW, YELLOW, "Range Interface %i dead @ line %i", *riclient, __LINE__);
      close(*riclient);
      *riclient = -1;
      // new epoll method
      epoll_ctl(efd, EPOLL_CTL_DEL, *riclient, NULL);
   }
   close(risock);
   close(RFfd);
   close(efd);
}  // end of handle_RF


void print_help(int exval) {
   printf("RFmaster [-h] [-v num] [-t comm] [-p port] [-x delay] \n\n");
   printf("  -h            print this help and exit\n");
   printf("  -p 4004       set mcp listen port\n");
   printf("  -i 4008       set radio interface listen port\n");
   printf("  -t /dev/ttyS1 set serial port device\n");
   printf("  -x 800        xmit test, xmit a request devices every arg milliseconds (default=disabled=0)\n");
   printf("  -s 150        slottime in ms (5ms granules, 1275 ms max  ONLY WITH -x\n");
   printf("  -c 300        collection time for burst messages in ms\n");
   printf("  -d 0x20       Lowest devID to find  ONLY WITH -x\n");
   printf("  -D 0x30       Highest devID to find  ONLY WITH -x\n");
   print_verbosity();
   exit(exval);
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
   int serversock;                      /* Socket descriptor for server connection */
   int risock = -1;                  /* Socket descriptor for radio interface connection listener*/
   int riclient = -1;                  /* Socket descriptor for radio interface connection */
   int MCPsock;                 /* Socket descriptor to use */
   int RFfd;                            /* File descriptor for RFmodem serial port */
   struct sockaddr_in ServAddr; /* Local address */
   struct sockaddr_in RiAddr;   /* Radio Interface address */
   struct sockaddr_in ClntAddr; /* Client address */
   unsigned short RFmasterport; /* Server port */
   unsigned short SRport;       /* SmartRange port */
   unsigned int clntLen;               /* Length of client address data structure */
   char ttyport[32];    /* default to ttyS0  */
   int opt,xmit,hardflow;
   int slottime,total_slots;
   int low_dev,high_dev;

   rtime=0;
   collecting_time=300; //  minimum time to wait for collecting messages for a burst (in ms)
   slottime=0;
   verbose=0;
   xmit=0;              // used for testing
   RFmasterport = defaultPORT;
   SRport = smartrangePORT;
   strcpy(ttyport,"/dev/ttyS0");
   hardflow=0;
   
   // install signal handlers
   signal(SIGINT, quitproc);
   signal(SIGQUIT, quitproc);

   while((opt = getopt(argc, argv, "hv:r:i:f:t:p:s:x:l:d:D:c:")) != -1) {
      switch(opt) {
         case 'h':
            print_help(0);
            break;

         case 'f':
            hardflow=7 & atoi(optarg);            
            break;

         case 'r':
            rtime = atoi(optarg);            
            break;

         case 'c':
            collecting_time = atoi(optarg);            
            break;

         case 'v':
            verbose = strtoul(optarg,NULL,16);
            break;

         case 't':
            strcpy(ttyport,optarg);
            break;

         case 'p':
            RFmasterport = atoi(optarg);
            break;

         case 'i':
            SRport = atoi(optarg);
            break;

         case 's':
            slottime = atoi(optarg);
            break;

         case 'd':
            low_dev = strtoul(optarg,NULL,16);
            break;

         case 'D':
            high_dev = strtoul(optarg,NULL,16);
            break;

         case 'l':
            slottime = atoi(optarg);
            break;

         case 'x':
            xmit = atoi(optarg);
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

   if (rtime<350) rtime = 350;  // enforce a minimum time to wait  - also the default if no -r option
   if (rtime>35000) rtime = 35000;  // enforce a maximum rtime
   //hardflow|=2; // force BLOCKING for the time being
   
   DCMSG(YELLOW,"RFmaster: verbosity is set to 0x%x", verbose);
   DCMSG(YELLOW,"RFmaster: hardflow is set to 0x%x", hardflow);
   DCMSG(YELLOW,"RFmaster: listen for MCP on TCP/IP port %d",RFmasterport);
   DCMSG(YELLOW,"RFmaster: listen for SmartRange on TCP/IP port %d",SRport);
   DCMSG(YELLOW,"RFmaster: comm port for Radio transciever = %s",ttyport);
   print_verbosity_bits();

   //   Okay,   set up the RF modem link here

   RFfd=open_port(ttyport,hardflow);   // with hardware flow

   if (RFfd<0) {
      DCMSG(RED,"RFmaster: comm port could not be opened. Shutting down");
      exit(-1);
   }

   //  this section is just used for testing   
   if (xmit){   
      LB_packet_t LB;
      LB_request_new_t *RQ;
      int result,size,more;
      char buf[200];
      char rbuf[300];

      total_slots=high_dev-low_dev+2;
      if (!slottime || (high_dev<low_dev)){
         DCMSG(RED,"\nRFmaster: xmit test: slottime=%d must be set and high_dev=%d cannot be less than low_dev=%d",slottime,high_dev,low_dev);
         exit(-1);
      } else {

         DCMSG(GREEN,"RFmaster: xmit test: slottime=%d  high_dev=%d low_dev=%d  total_slots=%d",slottime,high_dev,low_dev,total_slots);
      }

      if (slottime>1275){
         DCMSG(RED,"\nRFmaster: xmit test: slottime=%d must be set between 50? and 1275 milliseconds",slottime);
         exit(-1);
      }


      RQ=(LB_request_new_t *)&LB;

      RQ->cmd=LBC_REQUEST_NEW;
      RQ->low_dev=low_dev;
      RQ->inittime=slottime/5; // use slottime as initial time in xmit debug state
      RQ->slottime=slottime/5;
      // calculates the correct CRC and adds it to the end of the packet payload
      // also fills in the length field
      size = RF_size(RQ->cmd);
      set_crc8(RQ);


      while(1){

         usleep(xmit*1000);
         DCMSG(YELLOW,"top usleep for %d ms",xmit);
         
         // now send it to the RF master
         setblocking(RFfd);
         result=write(RFfd,RQ,size);
         if (result==size) {
            sprintf(buf,"Xmit test  ->RF  [%2i]  ",size);
            DCMSG_HEXB(BLUE,buf,RQ,size);
         } else {
            sprintf(buf,"Xmit test  ->RF  [%d!=%d]  ",size,result);
            DCMSG_HEXB(RED,buf,RQ,size);
         }
         setnonblocking(RFfd);
      
         
         usleep(slottime + ((1+total_slots)*slottime*1000)); // use slottime as initial time
         DCMSG(YELLOW,"usleep for %d ms",total_slots*slottime);
         
         result = gather_rf(RFfd,rbuf,275);

         if (result>0) {
            sprintf(buf,"Rcved [%2i]  ",result);
//            sprintf(buf,"%4ld.%03ld  %2i    ",elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l,gathered);
//	   printf("\x1B[3%d;%dm%s",(GREEN)&7,((GREEN)>>3)&1,buf);
            printf("%s",buf);
            printf("\x1B[3%d;%dm",(GREEN)&7,((GREEN)>>3)&1);
            if(result>1){
               for (int i=0; i<result-1; i++) printf("%02x.", rbuf[i]);
            }
            printf("%02x\n", rbuf[result-1]);
            if (verbose&D_PARSE){
               DDpacket(rbuf,result);
            } else {
               DDCMSG(D_NEW, GREEN, "\t<-\tReceived %i bytes over RF", result);
            }
         } else {
            printf("Rcved [%2i] \n",result);
         }

//         usleep(total_slots*slottime*1000);
         DCMSG(YELLOW,"x2 usleep for 0 ms"/*,total_slots*slottime*/);

         result = gather_rf(RFfd,rbuf,275);

         if (result>0) {
            sprintf(buf,"x2 Rcved [%2i]  ",result);
//            sprintf(buf,"%4ld.%03ld  %2i    ",elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000l,gathered);
//	   printf("\x1B[3%d;%dm%s",(GREEN)&7,((GREEN)>>3)&1,buf);
            printf("%s",buf);
            printf("\x1B[3%d;%dm",(GREEN)&7,((GREEN)>>3)&1);
            if(result>1){
               for (int i=0; i<result-1; i++) printf("%02x.", rbuf[i]);
            }
            printf("%02x\n", rbuf[result-1]);
            if (verbose&D_PARSE){
               DDpacket(rbuf,result);
            } else {
               DDCMSG(D_NEW, GREEN, "\t<-\tReceived %i more bytes over RF", result);
            }
         } else {
            printf("x2 Rcved [%2i] \n",result);
         }

      }
   }  // if xmit testing section over.

   //  now Create socket for the incoming connection */

   if ((serversock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
      DieWithError("socket(serversock) failed");

   if ((risock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
      DieWithError("socket(risock) failed");

   /* Construct local address structure */
   memset(&ServAddr, 0, sizeof(ServAddr));   /* Zero out structure */
   ServAddr.sin_family = AF_INET;                /* Internet address family */
   ServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
   ServAddr.sin_port = htons(RFmasterport);      /* Local port */

   /* Bind to the local address */
   if (bind(serversock, (struct sockaddr *) &ServAddr, sizeof(ServAddr)) < 0)
      DieWithError("bind(serversock) failed");

   /* Construct local address structure */
   memset(&RiAddr, 0, sizeof(RiAddr));   /* Zero out structure */
   RiAddr.sin_family = AF_INET;                /* Internet address family */
   RiAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
   RiAddr.sin_port = htons(SRport);      /* Local port */

   /* Mark the socket so it will listen for incoming connections */
   /* the '1' for MAXPENDINF might need to be a '0')  */
   if (listen(serversock, 2) < 0) {
      DieWithError("listen(serversock) failed");
   }

   /* Set the size of the in-out parameter */
   clntLen = sizeof(ClntAddr);

   /* Bind to the local address */
   if (bind(risock, (struct sockaddr *) &RiAddr, sizeof(RiAddr)) < 0) {
      DCMSG(RED, "bind(risock) failed");
   }

   /* Mark the socket so it will listen for incoming connections */
   /* the '1' for MAXPENDINF might need to be a '0')  */
   if (listen(risock, 2) < 0) {
      DCMSG(RED, "listen(risock) failed");
   }

   while (!close_nicely) {

      /* error existing riclient */
      if (riclient > 0) {
         write(riclient, "E\n", 2);
      }

      /* Wait for a client to connect */
      if ((MCPsock = accept(serversock, (struct sockaddr *) &ClntAddr,  &clntLen)) < 0) {
         DieWithError("accept() failed");
      }

      int yes = 1;
      setsockopt(MCPsock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int)); // set keepalive so we disconnect on link failure or timeout
      setnonblocking(MCPsock);

      /* MCPsock is connected to a Master Control Program! */

      /* un-error existing riclient, by disconnecting */
      if (riclient > 0) {
         //write(riclient, "V\n", 2);
         DDCMSG(D_NEW, YELLOW, "Range Interface %i dead @ line %i", riclient, __LINE__);
         close(riclient);
         riclient = -1;
      }


      DCMSG(BLUE,"Good connection to MCP <%s>  (or telnet or somebody)", inet_ntoa(ClntAddr.sin_addr));
      HandleRF(MCPsock,risock, &riclient,RFfd);
      DCMSG(RED,"Connection to MCP closed.   listening for a new MCP");

   }
   DCMSG(BLACK,"RFmaster says goodbye...");
   close(serversock);
}
