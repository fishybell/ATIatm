#include "mcp.h"
#include "rf.h"

int verbose;    // globals

// this makes the warning go away
extern size_t strnlen (__const char *__string, size_t __maxlen)
__THROW __attribute_pure__ __nonnull ((1));

// tcp port we'll listen to for new connections
#define defaultPORT 4004

// tcp port we'll listen to for SmartRange radio interface
#define smartrangePORT 4008

// size of client buffer
#define CLIENT_BUFFER 1024

void DieWithError(char *errorMessage){
   char buf[200];
   strerror_r(errno,buf,200);
   DCMSG(RED,"RFmaster %s %s \n", errorMessage,buf);
   exit(1);
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

void HandleRF(int MCPsock,int risock,int RFfd){
   struct timespec elapsed_time, start_time, istart_time,delta_time;
   queue_t *Rx,*Tx;
   int seq=1;           // the packet sequence numbers

   char Rbuf[512];
   char buf[200];        /* text Buffer  */
   char  *Rptr,*Rstart;
   int size,gathered,gotrf,delta,remaining_time,bytecount;
   int riclient=-1;    /* radio interface client socket */
   int MsgSize,result,sock_ready,pcount=0;                    /* Size of received message */
   fd_set rf_or_mcp;
   struct timeval timeout;
   int maxcps=500,rfcount=0;            /*  characters per second that we can transmit without melting - 1000 is about 100% */
   double cps;
   uint8 crc;
   int slottime=0,total_slots;
   int low_dev,high_dev;
   int burst,ptype;


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

   remaining_time=100;          //  remaining time before we can transmit (in ms)

   /**   loop until we lose connection  **/
   clock_gettime(CLOCK_MONOTONIC,&istart_time); // get the intial current time
   timestamp(&elapsed_time,&istart_time,&delta_time);   // make sure the delta_time gets set    
   while(1) {

      timestamp(&elapsed_time,&istart_time,&delta_time);
      DDCMSG(D_TIME,CYAN,"Top of loop    %5ld.%09ld timestamp, delta=%5ld.%09ld"
             ,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
      delta = (delta_time.tv_sec*1000)+(delta_time.tv_nsec/1000000);
      DDCMSG(D_TIME,CYAN,"delta=%2dms",delta);

      /*   do a select to see if we have a message from either the RF or the MCP  */
      /* then based on the source, send the message to the destination  */
      /* create a fd_set so we can monitor both the mcp and the connection to the RCC*/
      FD_ZERO(&rf_or_mcp);
      FD_SET(MCPsock,&rf_or_mcp);               // we are interested hearing the mcp
      FD_SET(risock,&rf_or_mcp);                // we are interested hearing the Radio Interface listen socket
      if (riclient > 0) {
         FD_SET(riclient,&rf_or_mcp); // only add riclient to select when it exists
      }
      FD_SET(RFfd,&rf_or_mcp);          // we also want to hear from the RF world

      /*   actually for now we will block until we have data - no need to timeout [yet?]
       *     I guess a timeout that will make sense later is if the radio is ready for data
       *   and we have no MCP data to send, then we should make the rounds polling
       *   for status.  At the moment I don't want to impliment this at the RFmaster level,
       *   for my initial working code I am shooting for the MCP to do all the work and
       *   the RFmaster to just pass data back and forth.
       */


      DDCMSG(D_TIME,YELLOW,"before select remaining_time=%d  Rx[%d] Tx[%d]"
             ,remaining_time,Queue_Depth(Rx),Queue_Depth(Tx));

      timeout.tv_sec=remaining_time/1000;
      timeout.tv_usec=(remaining_time%1000)*1000;
      sock_ready=select(FD_SETSIZE,&rf_or_mcp,(fd_set *) 0,(fd_set *) 0, &timeout);
      DDCMSG(D_TIME,YELLOW,"select returned, sock_ready=%d",sock_ready);
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
         timestamp(&elapsed_time,&istart_time,&delta_time);
         DDCMSG(D_TIME,CYAN,"select timed out at %5ld.%09ld timestamp, delta=%5ld.%09ld"
                ,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
         delta = (delta_time.tv_sec*1000)+(delta_time.tv_nsec/1000000);
         DDCMSG(D_TIME,CYAN,"delta=%2dms",delta);

         DDCMSG(D_TIME,YELLOW,"select timed out  delta=%d remaining_time=%d  Rx[%d] Tx[%d]"
                ,delta,remaining_time,Queue_Depth(Rx),Queue_Depth(Tx));

         if (delta>remaining_time-1) {


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
             ******    Algorithm:  for up to 240 bytes, as long as the packets are not type #1 (request new devices) we
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
            remaining_time =1000;       // reset our remaining_time before we are allowed to Tx again   time needs to be smarter

            while((ptype=QueuePtype(Rx))&&              /* we have a complete packet */
                  burst &&                              /* and burst is not 0 (0 forces us to send) */
                  (Queue_Depth(Tx)<240)&&               /*  room for more */
                  !((ptype==1)&&(burst==2))             /* and if we are NOT a REQUEST_NEW AND haven't got a type 2  yet */
                 ){     


               DDCMSG(D_MEGA,CYAN,"in loop.  Rx[%d] Tx[%d]",Queue_Depth(Rx),Queue_Depth(Tx));
               if (ptype==1){   /*  parse it to get the slottime and devid range, and set burst to 0 */
                  LB_new =(LB_request_new_t *) Rx->head;        // we need to parse out the slottime, high_dev and low_dev from this packet.
                  slottime=LB_new->slottime*5;          // convert passed slottime back to milliseconds
                  low_dev=LB_new->low_dev;
                  total_slots=34;               // total number of slots with end padding
                  burst=0;
                  remaining_time =(total_slots)*slottime;       // set up the timer
                  DDCMSG(D_TIME,YELLOW,"setting remaining_time to %d  total_slots=%d slottime=%d",
                         remaining_time,total_slots,slottime);
                  ReQueue(Tx,Rx,RF_size(LB_new->cmd));  // move it to the Tx queue

                  //                    DCMSG(YELLOW,"requeued into M1[%d:%d]:%d s%d  size=%d",
                  //                          M1->head-M1->buf,M1->tail-M1->buf,Queue_Depth(M1),QueueSeq_peek(M1),size);
                  //                    sprintf(buf,"M1[%2d:%2d]  ",M1->head-M1->buf,M1->tail-M1->buf);
                  //                    DCMSG_HEXB(YELLOW,buf,M1->head,Queue_Depth(M1));
               } else {
                  LB=(LB_packet_t *)Rx->head;                   
                  ReQueue(Tx,Rx,RF_size(LB->cmd));      // move it to the Tx queue
                  if (ptype==2) {
                     burst=2;
                     remaining_time +=slottime; // add time for a response to this one
                     DDCMSG(D_TIME,YELLOW,"incrementing remaining_time by slottime to %d  slottime=%d",
                            remaining_time,slottime);

                  }                         
               }
            }  // end of while loop to build the Tx packet


            /***********    Send the RF burst
             ***********
             ***********/

            DDCMSG(D_MEGA,CYAN,"before Tx to RF.  Tx[%d]",Queue_Depth(Tx));

            if (Queue_Depth(Tx)){  // if we have something to Tx, Tx it.

               // when we're sending, we're busy, tell the Radio Interface client
               if (riclient > 0) {
                  char ri_buf[128];
                  DDCMSG(D_TIME, YELLOW, "Remaining time: %d %d", remaining_time, slottime);
                  snprintf(ri_buf, 128, "B %i\r", (5 * (Queue_Depth(Tx)) / 3) + 37 + remaining_time); // number of bytes * baud rate = milliseconds (9600 baud / 2 for overhead => 600 bytes a second => 5/3 second for 1000 bytes) + transmit delays
                  result=write(riclient, ri_buf, strnlen(ri_buf, 128));
               }

               result=write(RFfd,Tx->head,Queue_Depth(Tx));
               if (result<0){
                  strerror_r(errno,buf,200);                
                  DCMSG(RED,"write Tx queue to RF error %s",buf);
               } else {
                  if (!result){
                     DCMSG(RED,"write Tx queue to RF returned 0");
                  }
                  if (verbose&D_RF){
                        timestamp(&elapsed_time,&istart_time,&delta_time);
                        sprintf(buf,"[%03d] %4ld.%03ld  ->RF [%2d] ",D_PACKET,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000,result);
                        printf("%s",buf);
                        printf("\x1B[3%d;%dm",(BLUE)&7,((BLUE)>>3)&1);
                        if(result>1){
                           for (int i=0; i<result-1; i++) printf("%02x.", (uint8) Tx->head[i]);
                        }
                        printf("%02x\n", (uint8) Tx->head[result-1]);
                  }  
                  if (verbose&D_PARSE){
                     DDpacket(Tx->head,result);
                  }
               }
               DeQueue(Tx,Queue_Depth(Tx));

            }
            timestamp(&elapsed_time,&istart_time,&delta_time);
            DDCMSG(D_MEGA,CYAN,"Just might have Tx'ed to RF   at %5ld.%09ld timestamp, delta=%5ld.%09ld"
                   ,elapsed_time.tv_sec, elapsed_time.tv_nsec,delta_time.tv_sec, delta_time.tv_nsec);
            /*****    this stuff below isn't really tested  *****/              
            rfcount+=MsgSize;
            cps=(double) rfcount/(double)elapsed_time.tv_sec;
            DDCMSG(D_TIME,CYAN,"RFmaster average cps = %f.  max at current duty is %d",cps,maxcps);

         }
      }

      //  Testing has shown that we generally get 8 character chunks from the serial port
      //  when set up the way it is right now.
      // we must splice them back together
      //  which means we have to be aware of the Low Bandwidth protocol
      //  if we have data to read from the RF, read it then blast it back upstream to the MCP
      DDCMSG(D_MEGA,BLACK,"RFmaster checking FD_ISSET(RFfd)");
      if (FD_ISSET(RFfd,&rf_or_mcp)){

         // while gathering RF data, we're busy, tell the Radio Interface client
         if (riclient > 0) {
            write(riclient, "B\r", 2);
         }

         DDCMSG(D_MEGA,BLACK,"RFmaster FD_ISSET(RFfd)");
         /* Receive message, or continue to recieve message from RF */

         gotrf = gather_rf(RFfd,Rptr,300);
         if (gotrf>0){  // increment our current pointer
            DDCMSG(D_VERY,GREEN,"gotrf=%d gathered =%2d, incrementing  Rptr=%2d (%p)  Rbuf=%p",
                   gotrf,gathered,(int)(Rptr-Rbuf), Rptr, Rbuf);
            Rptr+=gotrf;
            gathered+=gotrf;
            DDCMSG(D_VERY,GREEN,"gotrf=%d gathered =%2d  Rptr=%2d (%p) Rbuf=%p ",
                   gotrf,gathered,(int)(Rptr-Rbuf), Rptr, Rbuf);
         } else {
            DDCMSG(D_VERY,RED,"gotrf=%d gathered =%2d  Rptr=%2d (%p) ",
                   gotrf,gathered,(int)(Rptr-Rbuf), Rptr);
         }
         /* Receive message, or continue to recieve message from RF */
         DDCMSG(D_VERY,YELLOW,"gotrf=%d  gathered=%2d  Rptr=%2d Rstart=%2d Rptr-Rstart=%2d  ",
                gotrf,gathered,(int)(Rptr-Rbuf),(int)(Rstart-Rbuf),(int)(Rptr-Rstart));

         // after gathering RF data, we're free, tell the Radio Interface client
         if (riclient > 0) {
            write(riclient, "F\r", 2);
         }

         while (gathered>=3){
            // we have a chance of a compelete packet
            LB=(LB_packet_t *)Rstart;   // map the header in
            size=RF_size(LB->cmd);

            /* Receive message, or continue to recieve message from RF */
            DDCMSG(D_VERY,GREEN,"cmd=%d addr=%d RF_size =%2d  Rptr-Rstart=%2d  ",
                   LB->cmd,LB->addr,size,(int)(Rptr-Rstart));

            if ((Rptr-Rstart) >= size){
               //  we do have a complete packet
               // we could check the CRC and dump it here

               crc=crc8(LB);
               if (!crc) {
                  result=write(MCPsock,Rstart,size);
                  if (result==size) {
                     if (verbose&D_RF){
                        timestamp(&elapsed_time,&istart_time,&delta_time);
                        sprintf(buf,"[%03d] %4ld.%03ld ->MCP [%2d] ",D_PACKET,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000,result);
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
      } // if this fd is ready

      // read range interface client
      DDCMSG(D_MEGA,BLACK,"RFmaster checking FD_ISSET(riclient)");
      if (riclient > 0 && FD_ISSET(riclient,&rf_or_mcp)){
         DDCMSG(D_MEGA,BLACK,"RFmaster FD_ISSET(riclient)");
         int size;
         char buf[128];
         int err;
         size=read(riclient,buf,128);
         err = errno;
         if (size <= 0) {
            DDCMSG(D_PACKET, YELLOW, "Range Interface dead");
            close(riclient);
            riclient = -1;
         } else {
            DDCMSG(D_PACKET, YELLOW, "Read from Range Interface %i bytes: %s", size,buf);
         }
      }

      // accept range interface client
      DDCMSG(D_MEGA,BLACK,"RFmaster checking FD_ISSET(risock)");
      if (FD_ISSET(risock,&rf_or_mcp)){
         DDCMSG(D_MEGA,BLACK,"RFmaster FD_ISSET(risock)");
         int newclient = -1;
         struct sockaddr_in ClntAddr;   /* Client address */
         unsigned int clntLen;               /* Length of client address data structure */
         // close existing one
         if (riclient > 0) {
            close(riclient);
            riclient = -1;
         }
         if ((newclient = accept(risock, (struct sockaddr *) &ClntAddr,  &clntLen)) > 0) {
            // replace existing riclient with new one
            riclient = newclient;
         }// if error, ignore
      }

      /***************     reads the message from MCP into the Rx Queue
       ***************/
      DDCMSG(D_MEGA,BLACK,"RFmaster checking FD_ISSET(MCPsock)");
      if (FD_ISSET(MCPsock,&rf_or_mcp)){
         int err;
         DDCMSG(D_MEGA,BLACK,"RFmaster FD_ISSET(MCPsock)");
         /* Receive message from MCP and read it directly into the Rx buffer */
         MsgSize = recv(MCPsock, Rx->tail, Rxsize-(Rx->tail-Rx->buf),0);
         err=errno;

         if (verbose&D_PACKET){
            timestamp(&elapsed_time,&istart_time,&delta_time);
            sprintf(buf,"[%03d] %4ld.%03ld MCP-> [%2d] ",D_PACKET,elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000,MsgSize);
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
             ***************       Just queue it up in our Rx queue .
             ***************
             ***************       I suppose it should check for fullness
             ***************
             ***************    there might also need to be some kind of throttle to slow the MCP if we are full.
             ***************    
             ***************    */

            Rx->tail+=MsgSize;  // add on the new length
            bytecount=Queue_Depth(Rx);
            DDCMSG(D_QUEUE,YELLOW,"Rx[%d]",bytecount);

            // we don't need to parse or anything....

         }  // if msgsize was positive

      }  // end of MCP_sock
   } // end while 1
}  // end of handle_RF


void print_help(int exval) {
   printf("RFmaster [-h] [-v num] [-t comm] [-p port] [-x delay] \n\n");
   printf("  -h            print this help and exit\n");
   printf("  -p 4004       set mcp listen port\n");
   printf("  -i 4008       set radio interface listen port\n");
   printf("  -t /dev/ttyS1 set serial port device\n");
   printf("  -x 800        xmit test, xmit a request devices every arg milliseconds (default=disabled=0)\n");
   printf("  -s 150        slottime in ms (5ms granules, 1275 ms max  ONLY WITH -x\n");
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
   int risock;                  /* Socket descriptor for radio interface connection */
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

   slottime=0;
   verbose=0;
   xmit=0;              // used for testing
   RFmasterport = defaultPORT;
   SRport = smartrangePORT;
   strcpy(ttyport,"/dev/ttyS0");
   hardflow=0;
   
   while((opt = getopt(argc, argv, "hv:f:t:p:s:x:l:d:D:")) != -1) {
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
            fprintf(stderr, "Error - Option `%c' needs a value\n\n", optopt);
            print_help(1);
            break;

         case '?':
            fprintf(stderr, "Error - No such option: `%c'\n\n", optopt);
            print_help(1);
            break;
      }
   }
   DCMSG(YELLOW,"RFmaster: verbosity is set to 0x%x", verbose);
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
      RQ->slottime=slottime/5;
      // calculates the correct CRC and adds it to the end of the packet payload
      // also fills in the length field
      size = RF_size(RQ->cmd);
      set_crc8(RQ);


      while(1){

         usleep(xmit*1000);
         DCMSG(YELLOW,"top usleep for %d ms",xmit);
         
         // now send it to the RF master
         result=write(RFfd,RQ,size);
         if (result==size) {
            sprintf(buf,"Xmit test  ->RF  [%2d]  ",size);
            DCMSG_HEXB(BLUE,buf,RQ,size);
         } else {
            sprintf(buf,"Xmit test  ->RF  [%d!=%d]  ",size,result);
            DCMSG_HEXB(RED,buf,RQ,size);
         }
      
         
         usleep((1+total_slots)*slottime*1000);
         DCMSG(YELLOW,"usleep for %d ms",total_slots*slottime);
         
         result = gather_rf(RFfd,rbuf,275);

         if (result>0) {
            sprintf(buf,"Rcved [%2d]  ",result);
//            timestamp(&elapsed_time,&istart_time,&delta_time);
//            sprintf(buf,"%4ld.%03ld  %2d    ",elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000,gathered);
//	   printf("\x1B[3%d;%dm%s",(GREEN)&7,((GREEN)>>3)&1,buf);
            printf("%s",buf);
            printf("\x1B[3%d;%dm",(GREEN)&7,((GREEN)>>3)&1);
            if(result>1){
               for (int i=0; i<result-1; i++) printf("%02x.", rbuf[i]);
            }
            printf("%02x\n", rbuf[result-1]);
            if (verbose&D_PARSE){
               DDpacket(rbuf,result);
            }
         } else {
            printf("Rcved [%2d] \n",result);
         }

//         usleep(total_slots*slottime*1000);
         DCMSG(YELLOW,"x2 usleep for 0 ms"/*,total_slots*slottime*/);

         result = gather_rf(RFfd,rbuf,275);

         if (result>0) {
            sprintf(buf,"x2 Rcved [%2d]  ",result);
//            timestamp(&elapsed_time,&istart_time,&delta_time);
//            sprintf(buf,"%4ld.%03ld  %2d    ",elapsed_time.tv_sec, elapsed_time.tv_nsec/1000000,gathered);
//	   printf("\x1B[3%d;%dm%s",(GREEN)&7,((GREEN)>>3)&1,buf);
            printf("%s",buf);
            printf("\x1B[3%d;%dm",(GREEN)&7,((GREEN)>>3)&1);
            if(result>1){
               for (int i=0; i<result-1; i++) printf("%02x.", rbuf[i]);
            }
            printf("%02x\n", rbuf[result-1]);
            if (verbose&D_PARSE){
               DDpacket(rbuf,result);
            }
         } else {
            printf("x2 Rcved [%2d] \n",result);
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

   /* Bind to the local address */
   if (bind(risock, (struct sockaddr *) &RiAddr, sizeof(RiAddr)) < 0)
      DieWithError("bind(risock) failed");

   /* Set the size of the in-out parameter */
   clntLen = sizeof(ClntAddr);

   /* Mark the socket so it will listen for incoming connections */
   /* the '1' for MAXPENDINF might need to be a '0')  */
   if (listen(serversock, 2) < 0)
      DieWithError("listen(serversock) failed");

   /* Mark the socket so it will listen for incoming connections */
   /* the '1' for MAXPENDINF might need to be a '0')  */
   if (listen(risock, 2) < 0)
      DieWithError("listen(risock) failed");

   for (;;) {

      /* Wait for a client to connect */
      if ((MCPsock = accept(serversock, (struct sockaddr *) &ClntAddr,  &clntLen)) < 0)
         DieWithError("accept() failed");

         int yes = 1;
         setsockopt(MCPsock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int)); // set keepalive so we disconnect on link failure or timeout

      /* MCPsock is connected to a Master Control Program! */

      DCMSG(BLUE,"Good connection to MCP <%s>  (or telnet or somebody)", inet_ntoa(ClntAddr.sin_addr));
      HandleRF(MCPsock,risock,RFfd);
      DCMSG(RED,"Connection to MCP closed.   listening for a new MCP");

   }
   /* NOT REACHED */
}
