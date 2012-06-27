#include <signal.h>
#include "mcp.h"
#include "rf.h"

#define MAX_EVENTS 16
#define MAX_CONNECTIONS 16

const char *__PROGRAM__ = "RFmaster ";

int inittime=0, slottime=0, verbose, rtime, collecting_time, repeat_wait, repeat_count;    // globals from command line

// globals for time management
int elapsed_time, edelta=0; // current in millseconds, and its delta
int xmit_time, xdelta=0;    // next time based on last transmit in milliseconds, and its delta
int collect_time, cdelta=0; // next time based on last message from mcp in milliseconds, and its delta
int select_time, sdelta=0;  // next time based on last select call in milliseconds, and its delta
int remain_time, rdelta=0;  // 

typedef struct rfpair {
   int parent;
   int child;
} rfpair_t;

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

/*
#define MIN_WRITE 15
void padTTY(int tty, int s) {
   if (s < MIN_WRITE) {
      char emptbuf[MIN_WRITE];
      memset(emptbuf, 0, MIN_WRITE);
      DCMSG(GREEN, "TTY PADDING %i BYTES", MIN_WRITE - s);
      write(tty, emptbuf, MIN_WRITE - s);
   }
} */

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

void HandleRF(int MCPsock,int risock, int *riclient,int RFfd,int child){
   struct timespec temp_time;
   int timeout_ms; // timeout time in milliseconds
   queue_item_t *Rx,*Tx,*qi;
   int Queue_Contains[2048]; // used to see what commands the outbound queue contains. max of rf addr 2047, 0 is not used
   int seq=1;           // the packet sequence numbers

   char Rbuf[RF_BUF_SIZE];
   char buf[200];        /* text Buffer  */
   char  *Rptr,*Rstart;
   int end, Rsize=0, size,gathered,gotrf,remaining_time,bytecount,bytecount2;
   int MsgSize,result,sock_ready,pcount=0;                    /* Size of received message */
   fd_set rf_or_mcp;
   struct timeval timeout;
   uint8 crc;
   int total_slots;
   int low_dev,high_dev;
   int burst,ptype,packets;
   LB_burst_t *LBb;
   char mcpbuf[MCP_BUF_SIZE];
   int mcpbuf_len=0;
   // epoll stuff
   struct epoll_event ev, events[MAX_EVENTS]; // temporary event, main event list
   int efd, nfds, n; // file descriptors for sockets
   int ep_rf, ep_mcp, ep_ris, ep_ric; // fds marked as ready in epoll
   int old_xmit_time=0;

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
   ev.data.fd = child; // remember for later
   if (epoll_ctl(efd, EPOLL_CTL_ADD, child, &ev) < 0) {
      EMSG("epoll child insertion error\n");
      close_nicely=1;
   }

   // packet header so we can determine the length from the command in the header
   LB_packet_t *LB;
   LB_request_new_t *LB_new;

   // initialize our gathering buffer
   Rptr=Rbuf;
   Rstart=Rptr;
   gathered=0;
   
   /* old queue stuff
   Rx=queue_init(Rxsize);       // incoming packet buffer
   Tx=queue_init(Txsize);       // outgoing Tx buffer
   end of old queue stuff */
   Rx = malloc(sizeof(queue_item_t));
   memset(Rx, 0, sizeof(queue_item_t));
   Tx = malloc(sizeof(queue_item_t));
   memset(Tx, 0, sizeof(queue_item_t));

   memset(Queue_Contains, 0, 2048);
   DDqueue(D_MEGA|D_POINTER, Rx, "initialized"); 
   DDqueue(D_MEGA|D_POINTER, Tx, "initialized"); 

   remaining_time=100;          //  remaining time before we can transmit (in ms)
   repeat_count = 3; // repeat 3 times by default
   repeat_wait = 50; // repeat every 50 ms by default

   /**   loop until we lose connection  **/
#define CURRENT_TIME(ts) { \
   clock_gettime(CLOCK_MONOTONIC,&temp_time); \
   ts = ts2ms(&temp_time); \
   /*DCMSG(RED, "Getting time @ %s:%i := %3i.%03i = %3i.%03i", __FILE__, __LINE__, DEBUG_TS(temp_time), DEBUG_MS(ts));*/ \
}

   while(!close_nicely) {

      CURRENT_TIME(elapsed_time);
      DDCMSG(D_TIME,CYAN,"Top of loop    %3i.%03i timestamp", DEBUG_MS(elapsed_time));

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


      DDCMSG(D_TIME,YELLOW,"before select remaining_time=%i  Rx[%i] Tx[%i] @ time %3i.%03i"
             ,remaining_time,queueLength(Rx),queueLength(Tx), DEBUG_MS(elapsed_time));
      DDqueue(D_MEGA|D_POINTER, Rx, "top of loop"); 
      DDqueue(D_MEGA|D_POINTER, Tx, "top of loop"); 

      // find out how much time to sleep based on a) not transmitting without having collected for a minimum amount of time and b) not transmitting more often than X milliseconds c) not transmitting over our need to wait for results and

      CURRENT_TIME(elapsed_time);
      // a)
      cdelta = collect_time - elapsed_time; // when we should be done collecting minus now
      if (cdelta > 0) {
         timeout_ms = cdelta; // we should wait at least this much
      } else {
         timeout_ms = 0; // we don't have anything to wait for ... yet
      }

      // b)
      xdelta = xmit_time - elapsed_time; // when we should be done transmitting minus now
      if (xdelta > 0) {
         timeout_ms = max(xdelta, timeout_ms); // we should wait at least this much
      } else {
         timeout_ms = max(0, timeout_ms); // we don't have anything more to wait for ... yet
      }

      // c)
      rdelta = remain_time - elapsed_time; // when we should be done waiting minus now
      if (rdelta > 0) {
         timeout_ms = max(rdelta, timeout_ms); // we should wait at least this much
      } else {
         timeout_ms = max(0, timeout_ms); // we don't have anything more to wait for ... yet
      }

      timeout_ms = max(0, timeout_ms); // a minimum of 0
      DCMSG(GREEN, "Final timeout will be @ %3i.%03i", DEBUG_MS((timeout_ms+elapsed_time)));
      // if we don't have anything in the queue, don't timeout
      bytecount=queueLength(Rx);
      bytecount2=queueLength(Tx);
      if (bytecount <= 0 && mcpbuf_len <= 0) {
         DDCMSG(D_TIME, CYAN, "BEFORE_SELECT: in: %i, out: %i, mcp: %i, time=%3i.%03i, ctime=%3i.%03i, xtime=%3i.%03i, rtime=%3i.%03i, timeout=INFINITE", bytecount, bytecount2, mcpbuf_len, DEBUG_MS(elapsed_time), DEBUG_MS(collect_time), DEBUG_MS(xmit_time), DEBUG_MS(remain_time));
         // new epoll method
         nfds = epoll_wait(efd, events, MAX_EVENTS, -1); // infinite timeout
#if 0 /* old select method */      
         sock_ready=select(FD_SETSIZE,&rf_or_mcp,(fd_set *) 0,(fd_set *) 0, NULL);
#endif /* end of old select method */
      } else {
         DDCMSG(D_TIME, CYAN, "BEFORE_SELECT: in: %i, out: %i, mcp: %i, time=%3i.%03i, ctime=%3i.%03i, xtime=%3i.%03i, rtime=%3i.%03i, timeout=%3i.%03i", bytecount, bytecount2, mcpbuf_len, DEBUG_MS(elapsed_time), DEBUG_MS(collect_time), DEBUG_MS(xmit_time), DEBUG_MS(remain_time), DEBUG_MS(timeout_ms));
         if (timeout_ms <= 0) {
            // will cause an immediate return if we're ready to send another burst
            DDCMSG(D_TIME,MAGENTA,"GOING TO RETURN IMMEDIATELY FROM SELECT (%i <= 0)", timeout_ms);
            timeout_ms = 0;
         }
         // new epoll method
         nfds = epoll_wait(efd, events, MAX_EVENTS, timeout_ms); // use millisecond timeout
#if 0 /* old select method */      
         sock_ready=select(FD_SETSIZE,&rf_or_mcp,(fd_set *) 0,(fd_set *) 0, &timeout);
#endif /* end of old select method */
      }

      // new epoll method
      // find all waiting connections
      ep_rf=-1; ep_mcp=-1; ep_ris=-1; ep_ric=-1; // none marked yet
      for (n = 0; !close_nicely && n < nfds; n++) {
         //DDCMSG(D_NEW, YELLOW, "Looking at %i in events...%i", n, events[n].data.fd);
         if (events[n].data.fd == child) { // child is ready...
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
      CURRENT_TIME(elapsed_time);
      DDCMSG(D_TIME,YELLOW,"select returned, sock_ready=%i @ time %3i.%03i",sock_ready, DEBUG_MS(elapsed_time));
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
      if (!sock_ready) {

         // get the actual current time.
         CURRENT_TIME(elapsed_time);
         sdelta = elapsed_time - select_time; // time we waited
         if (sdelta >= timeout_ms) {
            sdelta = 0; // we waited long enough
         }
         DDCMSG(D_TIME, CYAN, "select timed out at %3i.%03i timestamp, timeout=%3i.%03i, sdelta=%3i.%03i"
                , DEBUG_MS(elapsed_time), DEBUG_MS(timeout_ms), DEBUG_MS(sdelta));
         if (sdelta > 0) {
            // come back quickly
            continue;
         }

         DDCMSG(D_TIME,YELLOW,"select timed out Rx[%i] Tx[%i] @ time %3i.%03i", queueLength(Rx), queueLength(Tx), DEBUG_MS(elapsed_time));
         DDqueue(D_MEGA|D_POINTER, Rx, "timed out"); 
         DDqueue(D_MEGA|D_POINTER, Tx, "timed out"); 

         // if we timed out to process an RF transmission
         bytecount=queueLength(Rx);
         if (bytecount <= 0 && mcpbuf_len <= 0) {
            CURRENT_TIME(elapsed_time);
            DDCMSG(D_TIME,GRAY,"Timed out, but won't do anything: elapsed_t=%3i.%03i bytecount: %i, mcpbuf_len: %i",
                   DEBUG_MS(elapsed_time), bytecount, mcpbuf_len);
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

            DDCMSG(D_MEGA,YELLOW,"ptype rx=%i before Rx to Tx.  Rx[%i] Tx[%i]",queuePtype(Rx),queueLength(Rx),queueLength(Tx));
            DDqueue(D_MEGA|D_POINTER, Rx, "before"); 
            DDqueue(D_MEGA|D_POINTER, Tx, "before"); 

            // loop until we are out of complete packets, and place them into the Tx queue
            burst=3;            // remembers if we upt a type2 into the burst
            packets=0;          // remembers the number of packets in a burst
            remaining_time = 50;       // reset our remaining_time before we are allowed to Tx again   time needs to be smarter

#if 0 /* old queue code */
            while((ptype=queuePtype(Rx))&&              /* we have a complete packet */
                  burst &&                              /* and burst is not 0 (0 forces us to send) */
                  (queueLength(Tx)<239)&&               /*  room for more */
                  !((ptype==1)&&(burst==2))                           /* and if we are NOT a REQUEST_NEW.   requests get bursted like everything else */
                 ){     


               DDCMSG(D_MEGA,CYAN,"in loop.  Rx[%i] Tx[%i]",queueLength(Rx),queueLength(Tx));
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
                     DDCMSG(D_TIME,YELLOW,"setting remaining_time to %i  total_slots=%i slottime=%i",
                         remaining_time,total_slots,slottime);
                  } else {
                     // existing stuff in out-queue this burst
                     remaining_time +=(total_slots)*slottime;       // set up the timer
                     DDCMSG(D_TIME,YELLOW,"adding remaining_time to %i  total_slots=%i slottime=%i",
                         remaining_time,total_slots,slottime);
                  }
                  ReQueue(Tx,Rx,RF_size(LB_new->cmd));  // move it to the Tx queue
                  packets++;


               } else {
                  LB=(LB_packet_t *)Rx->head;                   
                  if (ptype==2) {
                     burst=2;
                     remaining_time +=slottime; // add time for a response to this one
                     DDCMSG(D_TIME,YELLOW,"incrementing remaining_time by slottime to %i  slottime=%i",
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
#endif /* end of old queue code */
            {
               char TransBuf[254]; // max size we can reliably send on dtxm radio
               char *tbuf;
               int tbuf_size, repeat;

               // build burst header into the front of transmission (always, even if only one message is being sent)
               LBb = (LB_burst_t*)TransBuf;
               LBb->cmd = LBC_BURST;
               tbuf = TransBuf + RF_size(LBb->cmd);
               tbuf_size = 254 - RF_size(LBb->cmd);

               // queue packets into the burst buffer
               LBb->sequence = queueBurst(Rx, Tx, tbuf, &tbuf_size, &remaining_time, &slottime, &inittime);
               if (LBb->sequence > 0) {
                  repeat = repeat_count; // send message many times
               } else {
                  repeat = 1; // only send message once
               }
               DDqueue(D_MEGA|D_POINTER, Rx, "after q burst"); 
               DDqueue(D_MEGA|D_POINTER, Tx, "after q burst"); 

               // set values for burst header
               LBb->number=queueSize(Tx)&0x7f; // max of 7 bits for number of packets
               set_crc8(LBb);
               tbuf_size = RF_size(LBb->cmd) + queueLength(Tx); // recalculate tbuf_size based on actual size

               /***********    Send the RF burst
                ***********    repeat multiple times if sequence is not zero
                ***********/
               DDCMSG(D_MEGA,CYAN,"before Tx to RF.  Tx[%i]", tbuf_size);
               DDCMSG(D_TIME, BLACK, "Adding initial time to remaining time: %i+%i+(x*%i)", remaining_time, (inittime*2) + (RF_COLLECT_DELAY), slottime);
               //remaining_time += (inittime*2) + (364*2); // add initial delay time (for beginning and end) and DTXM measured delay (for beggining and end)
               remaining_time += (inittime*2) + (RF_COLLECT_DELAY);

               if (tbuf_size > RF_size(LBb->cmd)) {  // if we have something to Tx, Tx it.
                  char *out_buf = TransBuf;
                  //int tbt;
                  CURRENT_TIME(elapsed_time);
                  DDCMSG(D_TIME, YELLOW, "TX @ Remaining time: %i %i, Elapsed time: %3i.%03i ", remaining_time, slottime, DEBUG_MS(elapsed_time));

                  // when we're sending, we're busy, tell the Radio Interface client
                  if (*riclient > 0) {
                     char ri_buf[128];
                     snprintf(ri_buf, 128, "B %i\n", (5 * (queueLength(Tx)) / 3) + 37 + remaining_time); // number of bytes * baud rate = milliseconds (9600 baud / 2 for overhead => 600 bytes a second => 5/3 second for 1000 bytes) + transmit delays + wait between time
                     DDCMSG(D_RF|D_VERY, BLACK, "writing riclient %s", ri_buf);
                     result=write(*riclient, ri_buf, strnlen(ri_buf, 128));
                     // add radio interface client socket to epoll
                     memset(&ev, 0, sizeof(ev));
                     ev.events = EPOLLIN; // only for reading to start
                     ev.data.fd = *riclient; // remember for later
                  }

                  //tbt=tbuf_size;
                  setblocking(RFfd);
                  while (repeat-- > 0) {
                     out_buf = TransBuf; // reset in case we are repeating and have mucked with it
                     tbuf_size = RF_size(LBb->cmd) + queueLength(Tx); // recalculate tbuf_size in case we are repeating and have mucked with it
                     do {
                        result=write(RFfd, out_buf, tbuf_size);
                        if (result<0){
                           strerror_r(errno,buf,200);                
                           DCMSG(RED,"write Tx queue to RF error %s",buf);
                           // TODO -- requeue to Rx? die? what?
                           DDqueue(D_POINTER, Tx, "dead 1"); 
                           break;
                        } else if (result == 0) {
                           DCMSG(RED,"write Tx queue to RF returned 0");
                           // TODO -- requeue to Rx? die? what?
                           DDqueue(D_POINTER, Tx, "dead 2"); 
                           break;
                        } else {
                           if (result == tbuf_size) {
                              if (verbose&D_RF) {
                                    sprintf(buf,"[%03i] %3i.%03i  ->RF [%2i] ",D_PACKET, DEBUG_MS(elapsed_time), result);
                                    printf("%s",buf);
                                    printf("\x1B[3%i;%im",(BLUE)&7,((BLUE)>>3)&1);
                                    if (result>1) {
                                       for (int i=0; i<result-1; i++) printf("%02x.", (uint8) out_buf[i]);
                                    }
                                    printf("%02x\n", (uint8) out_buf[result-1]);
                              }
                              if (verbose&D_PARSE) {
                                 DDCMSG(D_NEW, GREEN, "-> RF:");
                                 DDpacket(out_buf,result);
                              } else {
                                 DDCMSG(D_NEW, GREEN, "\t->\tTransmitted %i bytes over RF", result);
                              }
                           } else {
                              DDCMSG(D_NEW, GREEN, "\t->\tTransmitted %i bytes over RF, but should have transmitted %i", result, tbuf_size);
                              if (verbose&D_PARSE) {
                                 DDpacket(out_buf,result);
                              }
                              // TODO -- requeue unset packets to Rx? die? what?
                              DDqueue(D_POINTER, Tx, "dead 3"); 
                              // transmit again
                              tbuf_size -= result;
                              out_buf -= result;
                              result = 0;
                           }
                        }
                     } while (result < tbuf_size);
                     // space out repeating bursts so they arrive as individual packets on the clients
                     if (repeat > 0) {
                        usleep(repeat_wait * 1000); // default of 50 milliseconds
                     }
                  }
                  //padTTY(RFfd, tbt);
                  setnonblocking(RFfd);
                  // remove items from Tx queue, sending messages back to mcp as needed
                  DDqueue(D_MEGA|D_POINTER, Tx, "Tx before sentItems...", queueLength(Tx));
                  qi = Tx->next; // remove from head
                  //DDCMSG(D_POINTER, BLACK, "Looking at Tx (%p) head %p", Tx, qi);
                  while (qi != NULL) {
                     char tbuf[256];
                     int t=256;
                     if (qi->ptype == 2 && (Queue_Contains[qi->addr] & (1<<qi->cmd))) {
                        // no longer in Rx queue; un-mark
                        DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: %i currently in queue for addr %i (%p)", qi->cmd, qi->addr, Queue_Contains[qi->addr]);
                        Queue_Contains[qi->addr] ^= (1<<qi->cmd);
                        DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: %i no longer in queue for addr %i (%p)", qi->cmd, qi->addr, Queue_Contains[qi->addr]);
                     }
                     sentItem(qi, tbuf, &t);
                     if (t > 0) {
                        DDCMSG_HEXB(D_POINTER,RED,"Writing to MCPsock: ",tbuf,t);
                        write(MCPsock, tbuf, t);
                     }
                     qi = Tx->next; // look at new head
                     //DDCMSG(D_POINTER, BLACK, "Looking at new Tx (%p) head %p", Tx, qi);
                  }
                  DDqueue(D_MEGA|D_POINTER, Tx, "Tx before sentItems...", queueLength(Tx));
               }
               CURRENT_TIME(elapsed_time);
               // adjust dwait_time to the time to nothing, as we haven't waited anything yet
               if (elapsed_time - select_time < RF_BURST_DELAY) {
                  DDCMSG(D_TIME, RED, "Transmitted again before %i milliseconds @ time %3i.%03i (last:%3i.%03i)", RF_BURST_DELAY, elapsed_time, select_time);
               }
               old_xmit_time = elapsed_time - old_xmit_time;
               xmit_time = elapsed_time;
               remain_time = elapsed_time;
               xmit_time += rtime; // set a future time that is our earliest time allowed to select
               remain_time += remaining_time; // set a future time that is our earliest time allowed to select
               DDCMSG(D_TIME,GRAY,"Just might have Tx'ed to RF   at %3i.%03i timestamp, xmit=%3i.%03i, remain=%3i.%03i (time between = %3i.%03i)"
                      , DEBUG_MS(elapsed_time), DEBUG_MS(xmit_time), DEBUG_MS(remain_time), DEBUG_MS(old_xmit_time));
               old_xmit_time = elapsed_time;
            }
#if 0 /* old queue code */
            } else {
               DDCMSG(D_NEW,GREEN,"Failed to send any messages from Rx buffer of size %i", queueLength(Rx));
               if (D_NEW&verbose==D_NEW) {
                  for (int i=0; i<queueLength(Rx); i++) {
                     LB=(LB_packet_t *)(Rx->head+i);   // map the header in
                     DCMSG(GREEN, "Looking at char %i -> %i:", i, i+RF_size(LB->cmd));
                     DDpacket(Rx->head+i, queueLength(Rx) - i); // parse everything via this offset to see what it looks like
                  }
               }
            }
#endif /* end of old queue code */
         }
         // if we timed out to process MCP messages
         if (mcpbuf_len > 0) {
            result=write(MCPsock,mcpbuf,mcpbuf_len);
            if (result==mcpbuf_len) {
               // debug stuff
               if (verbose&D_RF){
                  sprintf(buf,"[%03i] %3i.%03i ->MCP [%2i] ",D_PACKET, DEBUG_MS(elapsed_time), result);
                  printf("%s",buf);
                  printf("\x1B[3%i;%im",(BLUE)&7,((BLUE)>>3)&1);
                  if(result>1){
                     for (int i=0; i<result-1; i++) printf("%02x.", (uint8) mcpbuf[i]);
                  }
                  printf("%02x\n", (uint8) mcpbuf[result-1]);
               }  
               if (verbose&D_PACKET){
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
               sprintf(buf,"RF ->MCP  [%i!=%i]  ",mcpbuf_len,result);
               DDCMSG_HEXB(D_RF,RED,buf,mcpbuf,mcpbuf_len);
               close(MCPsock);    /* Close socket */    
               return;            /* go back to the main loop waiting for MCP connection */
            }
         }
      }

      // read range interface client
      CURRENT_TIME(elapsed_time);
      DDCMSG(D_TIME|D_VERY,BLACK,"RFmaster checking FD_ISSET(riclient:%i)=%i @ time %3i.%03i", *riclient, ep_ric, DEBUG_MS(elapsed_time));
#if 0 /* old select method */
      if (*riclient > 0 && FD_ISSET(*riclient,&rf_or_mcp))
#endif /* end of old select method */
      if (*riclient > 0 && ep_ric != -1) {
         DDCMSG(D_VERY|D_NEW,BLACK,"RFmaster FD_ISSET(riclient)=%i @ time %3i.%03i", ep_ric, DEBUG_MS(elapsed_time));
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
      CURRENT_TIME(elapsed_time);
      DDCMSG(D_TIME|D_VERY,BLACK,"RFmaster checking FD_ISSET(risock:%i)=%i @ time %3i.%03i", risock, ep_ris, DEBUG_MS(elapsed_time));
#if 0 /* old select method */
      if (FD_ISSET(risock,&rf_or_mcp))
#endif /* end of old select method */
      if (ep_ris != -1) {
         DDCMSG(D_VERY|D_NEW,BLACK,"RFmaster FD_ISSET(risock)=%i @ time %3i.%03i", ep_ris, DEBUG_MS(elapsed_time));
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
      CURRENT_TIME(elapsed_time);
      DDCMSG(D_TIME|D_VERY,BLACK,"RFmaster checking FD_ISSET(RFfd:%i)=%i @ time %3i.%03i", RFfd, ep_rf, DEBUG_MS(elapsed_time));
#if 0 /* old select method */
      if (FD_ISSET(RFfd,&rf_or_mcp))
#endif /* end of old select method */
      if (ep_rf != -1) {
         DDCMSG(D_VERY|D_NEW,BLACK,"RFmaster FD_ISSET(RFfd)=%i @ time %3i.%03i", ep_rf, DEBUG_MS(elapsed_time));

         // while gathering RF data, we're busy, tell the Radio Interface client
         if (*riclient > 0) {
            DDCMSG(D_RF|D_VERY, BLACK, "writing riclient B\\n");
            write(*riclient, "B\n", 2);
         }

         DDCMSG(D_VERY|D_NEW,BLACK,"RFmaster FD_ISSET(RFfd)");
         /* Receive message, or continue to recieve message from RF */
         Rptr = Rbuf + Rsize;
         if ((gotrf = read(child, Rptr, RF_BUF_SIZE-Rsize)) > 0) {
            Rsize += gotrf;
            Rptr = Rbuf + Rsize;
         } else {
            //Rsize = Rptr - Rbuf; -- redundant
         }

         if (gotrf <= 0) {
            DDCMSG(D_NEW,GRAY,"RFmaster RF Received no data: %p %i", riclient, gotrf);
         } else {
            CURRENT_TIME(elapsed_time);
            DCMSG(GRAY, "->RF Rx %i bytes @ %3i.%03i", gotrf, DEBUG_MS(elapsed_time));
         }
         DDpacket(Rptr, gotrf);
         // we change xmit time whether it was us or someone else who transmitted
         CURRENT_TIME(elapsed_time);
         xmit_time = elapsed_time;
         xmit_time += rtime; // set a future time that is our earliest time allowed to select
         //DDCMSG(D_TIME,RED,"Reset xmit after RF Rx at %3i.%03i timestamp, xmit=%3i.%03i"
         //             , DEBUG_MS(elapsed_time), DEBUG_MS(xmit_time));

         // after gathering RF data, we're free, tell the Radio Interface client
         //DDCMSG(D_NEW,BLACK,"RFmaster got to here 1...");
         if (gotrf <= 0) {
            //DDCMSG(D_NEW,BLACK,"RFmaster got to here 3...");
            //DDCMSG(D_NEW,BLACK,"clowing riclient %i", *riclient);
            close(*riclient);
            *riclient = -1;
            // new epoll method
            epoll_ctl(efd, EPOLL_CTL_DEL, *riclient, NULL);
         } else if (*riclient > 0) {
            //DDCMSG(D_RF|D_VERY, BLACK, "writing riclient %i F\\n", *riclient);
            write(*riclient, "F\n", 2);
         }
         //DDCMSG(D_NEW,BLACK,"RFmaster got to here 2...");

         /* send valid message(s) to mcp */
         Rstart = Rbuf;
         while (Rstart < (Rptr - 2)) { /* look up until we don't have enough characters for a full message */
            DDCMSG(D_NEW,BLACK,"RFmaster got to here as well...%p %p", Rstart, Rptr);
            LB=(LB_packet_t *)Rstart;   // map the header in
            size=RF_size(LB->cmd);
            crc=(size > 2) ? crc8(LB) : 1; // only calculate crc if we have a valid size
            if (!crc) {
               /* -- this should happen on receipt of an assign addr from the mcp, not a device reg from a device
               if (LB->cmd == LBC_DEVICE_REG) {
                  DDCMSG(D_NEW,GRAY,"Can't clear queue_contains because I think the address is %i, but LBC_DEVICE_REG has no addr field...", LB->addr);
                  // clear out what we think the queue contains when we have a device register itself
                  //LB_device_reg_t *reg = (LB_device_reg_t*)Rstart;
                  //Queue_Contains[LB->addr] = 0;
               } -- end of "shouldn't happen here" */
               DDpacket(Rstart,size);
               queueMsg(mcpbuf, &mcpbuf_len, Rstart, size); // queue for transmission later
               #if 0 /* start of old, pre-queue, method */
               result=write(MCPsock,Rstart,size);
               if (result==size) {
                  if (verbose&D_RF){
                     sprintf(buf,"[%03i] %3i.%03i ->MCP [%2i] ",D_PACKET, DEBUG_MS(elapsed_time), result);
                     printf("%s",buf);
                     printf("\x1B[3%i;%im",(BLUE)&7,((BLUE)>>3)&1);
                     if(result>1){
                        for (int i=0; i<result-1; i++) printf("%02x.", (uint8) Rstart[i]);
                     }
                     printf("%02x\n", (uint8) Rstart[result-1]);
                  }  
                  if (verbose&D_PARSE){
                     DDpacket(Rstart,result);
                  }
               } else {
                  sprintf(buf,"RF ->MCP  [%i!=%i]  ",size,result);
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
               DDpacket(Rbuf, Rsize);
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
            DDCMSG(D_VERY,GREEN,"gotrf=%i gathered =%2i, incrementing  Rptr=%2i (%p)  Rbuf=%p",
                   gotrf,gathered,(int)(Rptr-Rbuf), Rptr, Rbuf);
            Rptr+=gotrf;
            gathered+=gotrf;
            DDCMSG(D_VERY,GREEN,"gotrf=%i gathered =%2i  Rptr=%2i (%p) Rbuf=%p ",
                   gotrf,gathered,(int)(Rptr-Rbuf), Rptr, Rbuf);
         } else {
            DDCMSG(D_VERY,RED,"gotrf=%i gathered =%2i  Rptr=%2i (%p) ",
                   gotrf,gathered,(int)(Rptr-Rbuf), Rptr);
         }
         /* Receive message, or continue to recieve message from RF */
         DDCMSG(D_VERY,YELLOW,"gotrf=%i  gathered=%2i  Rptr=%2i Rstart=%2i Rptr-Rstart=%2i  ",
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
            DDCMSG(D_VERY,GREEN,"cmd=%i addr=%i RF_size =%2i  Rptr-Rstart=%2i  ",
                   LB->cmd,LB->addr,size,(int)(Rptr-Rstart));

            if ((Rptr-Rstart) >= size){
               //  we do have a complete packet
               // we could check the CRC and dump it here

               crc=crc8(LB);
               if (!crc) {
                  result=write(MCPsock,Rstart,size);
                  if (result==size) {
                     if (verbose&D_RF){
                        sprintf(buf,"[%03i] %3i.%03i ->MCP [%2i] ",D_PACKET, DEBUG_MS(elapsed_time), result);
                        printf("%s",buf);
                        printf("\x1B[3%i;%im",(BLUE)&7,((BLUE)>>3)&1);
                        if(result>1){
                           for (int i=0; i<result-1; i++) printf("%02x.", (uint8) Rstart[i]);
                        }
                        printf("%02x\n", (uint8) Rstart[result-1]);
                     }  
                     if (verbose&D_PARSE){
                        DDpacket(Rstart,result);
                     }
                     
                  } else {
                     sprintf(buf,"RF ->MCP  [%i!=%i]  ",size,result);
                     DDCMSG_HEXB(D_RF,RED,buf,Rstart,size);
                  }

                  if ((Rptr-Rstart) > size){
                     Rstart+=size; // step ahead to the next packet
                     gathered-=size;
                     DDCMSG(D_VERY,RED,"Stepping to next packet, Rstart=%i Rptr=%i size=%i ",(int)(Rstart-Rbuf),(int)(Rptr-Rbuf),size);
                     sprintf(buf,"  Next 8 chars in Rbuf at Rstart  ");
                     DDCMSG_HEXB(D_VERY,RED,buf,Rstart,8);

                  } else {
                     gathered=0;
                     Rptr=Rstart=Rbuf;     // reset to the beginning of the buffer
                     DDCMSG(D_VERY,RED,"Resetting to beginning of Rbuf, Rstart=%i Rptr=%i size=%i ",(int)(Rstart-Rbuf),(int)(Rptr-Rbuf),size);
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
      CURRENT_TIME(elapsed_time);
      DDCMSG(D_TIME|D_VERY,BLACK,"RFmaster checking FD_ISSET(MCPsock:%i)=%i @ time %3i.%03i", MCPsock, ep_mcp, DEBUG_MS(elapsed_time));
#if 0 /* old select method */
      if (FD_ISSET(MCPsock,&rf_or_mcp))
#endif /* end of old select method */
      if (ep_mcp != -1) {
         char RecvBuf[1024], *rbuf;
         int err;
         rbuf = RecvBuf; // for now, point to start of receive buffer
         DDCMSG(D_VERY|D_NEW,BLACK,"RFmaster FD_ISSET(MCPsock)=%i @ time %3i.%03i", ep_mcp, DEBUG_MS(elapsed_time));
         /* Receive message from MCP and read it directly into the Rx buffer */
         MsgSize = recv(MCPsock, rbuf, 1024, 0);
         err=errno;

         if (verbose&D_PACKET){
            sprintf(buf,"[%03i] %3i.%03i MCP-> [%2i] ",D_PACKET, DEBUG_MS(elapsed_time), MsgSize);
            printf("%s",buf);
            printf("\x1B[3%i;%im",(GREEN)&7,((GREEN)>>3)&1);
            if(MsgSize>1){
               for (int i=0; i<MsgSize-1; i++) printf("%02x.", (uint8) rbuf[i]);
            }
            printf("%02x\n", (uint8) rbuf[MsgSize-1]);
         }
         if (verbose&D_POINTER) {
            DDCMSG(D_POINTER, GRAY, "Received %i bytes from MCP...", MsgSize);
            DDpacket(rbuf, MsgSize);
         }

         if (MsgSize<0){
            strerror_r(err,buf,200);
            DCMSG(RED,"read from MCP fd=%i failed  %s ",MCPsock,buf);
            sleep(1);
         }
         if (!MsgSize){
            DCMSG(RED,"read from MCP returned 0 - MCP closed");
            clearQueue(Rx);
            clearQueue(Tx);
            DDCMSG(D_POINTER, GREEN, "Freeing queue %p", Rx);
            free(Rx);
            DDCMSG(D_POINTER, GREEN, "Freeing queue %p", Tx);
            free(Tx);
            close(MCPsock);    /* Close socket */    
            return;                     /* go back to the main loop waiting for MCP connection */
         }

         if (MsgSize) {

            /***************    Sorting is needlessly complicated.
             ***************       Just queue it up in our Rx queue as long as it doesn't
             ***************       exist already in the queue.
             ***************
             ***************       I suppose it should check for fullness
             ***************
             ***************    there might also need to be some kind of throttle to slow the MCP if we are full.
             ***************    
             ***************    */
            int last_control_sequence = -1, last_control_addr = -1;

            // check against our existing messages in the queue
            while (MsgSize > 0) {
               LB=(LB_packet_t *)rbuf;   // map the header in
               ptype = Ptype(rbuf);
               DDCMSG(D_POINTER, GREEN, "Looking at individual message %i of size %i", LB->cmd, RF_size(LB->cmd));
               DDpacket(rbuf, RF_size(LB->cmd));
               if (LB->cmd == LBC_ASSIGN_ADDR) {
                  // clear out what we think the queue contains when we assign a device an new address
                  LB_assign_addr_t *laa = (LB_assign_addr_t*)rbuf;
                  DDCMSG(D_POINTER,GRAY,"Clearing QUEUE_CONTAINS for address %i, as it is new", laa->new_addr);
                  Queue_Contains[laa->new_addr] = 0;
               }
               if (ptype==4) {
                  // only going to receive LB_CONTROL_QUEUE messages from minions
                  LB_control_queue_t *lcq = (LB_control_queue_t*)LB;
                  last_control_sequence = lcq->sequence;
                  last_control_addr = lcq->addr;
                  DDCMSG(D_POINTER, GRAY, "Found control queue message with addr %i and sequence %i", last_control_addr, last_control_sequence);
               } else if (ptype==0) {
#if 0 /* bad idea, poorly implimented...don't clear status requests, just let the process work as designed */
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
                           DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: Removing %i from queue for addr %i (%i-%i...)", lb->cmd, lb->addr, queueLength(Rx), RF_size(lb->cmd)); memcpy(tbuf, ts + RF_size(lb->cmd), rs);
                           memcpy(ts, tbuf, rs);
                           Rx->tail -= RF_size(lb->cmd); // move the tail down
                           LB=(LB_packet_t *)Rx->tail;   // re-map the header in to the new tail position
                           DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: Removed from queue (...=%i)", queueLength(Rx));
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
#endif /* ... end of bad idea */
               } else {
                  int add_to_q = 1;
                  if (ptype==2) {
                     // only type 2 messages can be rejected from queue: they are the only 
                     //   messages that cause wait time and have only one destination.
                     //   they are also 100% redundant to have in multiple times.
                     if (Queue_Contains[LB->addr] & (1<<LB->cmd)) {
                        // already in queue, don't add Rx buffer
                        DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: Rejecting %i from queue for addr %i (%p)", LB->cmd, LB->addr, Queue_Contains[LB->addr]);
                        // don't move the tail, as we'll need to check the next message which is now at tail
                        add_to_q = 0;
                     } else {
                        // not in queue already, add and mark
                        DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: Going to allow %i in queue for addr %i (%p)", LB->cmd, LB->addr, Queue_Contains[LB->addr]);
                        Queue_Contains[LB->addr] |= (1<<LB->cmd);
                        DDCMSG(D_QUEUE,MAGENTA,"QUEUE_CONTAINS: Allowing %i in queue for addr %i (%p)", LB->cmd, LB->addr, Queue_Contains[LB->addr]);
                        add_to_q = 1;
                     }
                  }
                  // add to Rx queue
                  if (add_to_q) {
                     qi = queueTail(Rx);
                     enQueue(qi, rbuf, 0);
                     qi = queueTail(Rx); // we're the tail item
                     if (qi->addr == last_control_addr) {
                        DDCMSG(D_POINTER, GRAY, "Used control sequence %i with message %i", last_control_sequence, qi->cmd);
                        qi->sequence = last_control_sequence;
                     } else {
                        qi->sequence = -1;
                     }
                  }
               }
               // move our pointer on to the next message
               rbuf+=RF_size(LB->cmd);
               MsgSize-=RF_size(LB->cmd); // we've looked at this message, now look for more
            }
            if (ptype != 4) {
               // we've now used our last_control_* variables, so let's reset them
               last_control_sequence = -1;
               last_control_addr = -1;
            }
            // check to see if we're the first of many messages from the mcp
            if (bytecount == 0 && queueLength(Rx) > 0) {
               // adjust collect_time to the time to nothing, as we haven't waited anything yet
               CURRENT_TIME(collect_time);
               collect_time += collecting_time; // set a future time that is our earliest time allowed to select
               DDCMSG(D_NEW,CYAN,"Just might have Rx'ed from MCP   at %3i.%03i timestamp, xmit=%3i.%03i"
                      , DEBUG_MS(elapsed_time), DEBUG_MS(collect_time));
            }
            bytecount=queueLength(Rx);
            DDCMSG(D_QUEUE,YELLOW,"Rx[%i]",bytecount);
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

   clearQueue(Rx);
   clearQueue(Tx);
   DDCMSG(D_POINTER, GREEN, "Freeing queue %p", Rx);
   free(Rx);
   DDCMSG(D_POINTER, GREEN, "Freeing queue %p", Tx);
   free(Tx);
}  // end of handle_RF


void print_help(int exval) {
   printf("RFmaster [-h] [-v num] [-t comm] [-p port] [-x delay] \n\n");
   printf("  -h            print this help and exit\n");
   printf("  -p 4004       set mcp listen port\n");
   printf("  -i 4008       set radio interface listen port\n");
   printf("  -t /dev/ttyS1 set serial port device\n");
   printf("  -j 3          Number of times to repeat commands\n");
   printf("  -J 50         Amount of milliseconds to wait between repeats\n");
   printf("  -x 800        xmit test, xmit a request devices every arg milliseconds (default=disabled=0)\n");
   printf("  -s 150        slottime in ms (5ms granules, 1275 ms max  ONLY WITH -x\n");
   printf("  -c 300        collection time for burst messages in ms\n");
   printf("  -w 1          Fork process into parent/child for read/write seperation of radio\n");
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
   int total_slots;
   int low_dev,high_dev;
   rfpair_t rf_pair;
   int child=1; // default to fork

   rtime=0;
   collecting_time=RF_COLLECT_DELAY; //  minimum time to wait for collecting messages for a burst (in ms)
   slottime=0;
   verbose=0;
   xmit=0;              // used for testing
   RFmasterport = defaultPORT;
   SRport = smartrangePORT;
   strcpy(ttyport,"/dev/ttyS0");
   hardflow=6;
   
   while((opt = getopt(argc, argv, "hv:r:i:f:t:p:s:x:l:d:D:c:w:j:")) != -1) {
      switch(opt) {
         case 'h':
            print_help(0);
            break;

         case 'f':
            hardflow=7 & atoi(optarg);            
            break;

         case 'w':
            child = atoi(optarg);            
            break;

         case 'j':
            repeat_count = atoi(optarg);            
            break;

         case 'J':
            repeat_wait = atoi(optarg);            
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
            inittime = atoi(optarg);
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

   if (rtime<RF_BURST_DELAY) rtime = RF_BURST_DELAY;  // enforce a minimum time to wait  - also the default if no -r option
   if (rtime>RF_BURST_DELAY*10) rtime = RF_BURST_DELAY*10;  // enforce a maximum rtime
   //hardflow|=2; // force BLOCKING for the time being
   
   DCMSG(YELLOW,"RFmaster: verbosity is set to 0x%x", verbose);
   DCMSG(YELLOW,"RFmaster: hardflow is set to 0x%x", hardflow);
   DCMSG(YELLOW,"RFmaster: listen for MCP on TCP/IP port %i",RFmasterport);
   DCMSG(YELLOW,"RFmaster: listen for SmartRange on TCP/IP port %i",SRport);
   DCMSG(YELLOW,"RFmaster: comm port for Radio transciever = %s",ttyport);
   print_verbosity_bits();


   //  this section is just used for testing   
   if (xmit){   
      //   Okay,   set up the RF modem link here
      RFfd=open_port(ttyport,hardflow);   // with hardware flow

      if (RFfd<0) {
         DCMSG(RED,"RFmaster: comm port could not be opened. Shutting down");
         exit(-1);
      }

      int i = 0;
      char obuf[200];
      LB_packet_t *pkt = (LB_packet_t*)obuf;
      pkt->cmd = LBC_STATUS_REQ;
      pkt->addr = 500;
      set_crc8(pkt);
      memset(obuf, 0, 200);
      while (!close_nicely) {
         DCMSG(BLACK, "Transmitting %i...", ++i);
         write(RFfd, obuf, 200);
         //fsync(RFfd);
         sleep(3);
      }
      exit(-1);
#if 0
      LB_packet_t LB;
      LB_request_new_t *RQ;
      int result,size,more;
      char buf[200];
      char rbuf[300];

      total_slots=high_dev-low_dev+2;
      if (!slottime || (high_dev<low_dev)){
         DCMSG(RED,"\nRFmaster: xmit test: slottime=%i must be set and high_dev=%i cannot be less than low_dev=%i",slottime,high_dev,low_dev);
         exit(-1);
      } else {

         DCMSG(GREEN,"RFmaster: xmit test: slottime=%i  high_dev=%i low_dev=%i  total_slots=%i",slottime,high_dev,low_dev,total_slots);
      }

      if (slottime>1275){
         DCMSG(RED,"\nRFmaster: xmit test: slottime=%i must be set between 50? and 1275 milliseconds",slottime);
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
         DCMSG(YELLOW,"top usleep for %i ms",xmit);
         
         // now send it to the RF master
         setblocking(RFfd);
         result=write(RFfd,RQ,size);
         if (result==size) {
            sprintf(buf,"Xmit test  ->RF  [%2i]  ",size);
            DCMSG_HEXB(BLUE,buf,RQ,size);
         } else {
            sprintf(buf,"Xmit test  ->RF  [%i!=%i]  ",size,result);
            DCMSG_HEXB(RED,buf,RQ,size);
         }
         setnonblocking(RFfd);
      
         
         usleep(slottime + ((1+total_slots)*slottime*1000)); // use slottime as initial time
         DCMSG(YELLOW,"usleep for %i ms",total_slots*slottime);
         
         result = gather_rf(RFfd,rbuf,275);

         if (result>0) {
            sprintf(buf,"Rcved [%2i]  ",result);
//            sprintf(buf,"%3i.%03i  %2i    ", DEBUG_MS(elapsed_time), gathered);
//	   printf("\x1B[3%i;%im%s",(GREEN)&7,((GREEN)>>3)&1,buf);
            printf("%s",buf);
            printf("\x1B[3%i;%im",(GREEN)&7,((GREEN)>>3)&1);
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
//            sprintf(buf,"%3i.%03i  %2i    ", DEBUG_MS(elapsed_time), gathered);
//	   printf("\x1B[3%i;%im%s",(GREEN)&7,((GREEN)>>3)&1,buf);
            printf("%s",buf);
            printf("\x1B[3%i;%im",(GREEN)&7,((GREEN)>>3)&1);
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
#endif
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

   // open a bidirectional pipe for communication with the child
   if (socketpair(AF_UNIX,SOCK_STREAM,0,((int *) &rf_pair.parent))){
      DieWithError("socketpair() failed");
   }

   //   fork child process for reading, and leave parent for writing, or have single process do both
   if (child) {
      if ((child = fork()) == -1) {
         DieWithError("fork failed");
      }
      if (child) {
         // install signal handlers
         signal(SIGINT, quitproc);
         signal(SIGQUIT, quitproc);
         // we're the parent
         close(rf_pair.parent);
      } else {
         // install signal handlers
         signal(SIGINT, quitproc);
         signal(SIGQUIT, quitproc);
         // we're the child
         close(rf_pair.child);
         // do child thread here...
         __PROGRAM__ = "RFmaster:child ";

         //   Okay,   set up the RF modem link here
         RFfd=open_port(ttyport,hardflow | 2 | 0x08);   // with hardflow (and force blocking and read-only)

         if (RFfd<0) {
            DCMSG(RED,"RFmaster: comm port could not be opened. Shutting down");
            exit(-1);
         }

         while (!close_nicely) {
            char rbuf[RF_BUF_SIZE];
            int gotrf;
            if ((gotrf = read(RFfd,rbuf,RF_BUF_SIZE)) > 0) {
               DDCMSG(D_NEW, GRAY, "Childing pushing to parent %i bytes", gotrf);
               write(rf_pair.parent, rbuf, gotrf);
            } else {
               DDCMSG(D_NEW, RED, "Failed to gather_rf"); 
               perror("on rf read");
               sleep(1);
            }
         }
         exit(0);
      }

      //   Okay,   set up the RF modem link here
      RFfd = open_port(ttyport,hardflow | 0x10);   // with hardware flow (and force write-only)
   } else {
      // no child process forked, but keep everything the same so it seems so
      RFfd = open_port(ttyport, hardflow); // hardware flow (and leave read-write)
      rf_pair.child = RFfd;
   }

   if (RFfd<0) {
      DCMSG(RED,"RFmaster: comm port could not be opened. Shutting down");
      exit(-1);
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
      HandleRF(MCPsock,risock, &riclient,RFfd,rf_pair.child);
      DCMSG(RED,"Connection to MCP closed.   listening for a new MCP");

   }
   DCMSG(BLACK,"RFmaster says goodbye...");
   close(serversock);
}
