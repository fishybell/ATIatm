#include <signal.h>
#include "mcp.h"
#include "fasit_c.h"
#include "rf.h"

const char *__PROGRAM__ = "MCP ";

#ifdef ATMEL_ARM
   #define EPOLLRDHUP EPOLLHUP /* redefine EPOLLRDHUP as we don't have it on the arm board */
#endif

thread_data_t minions[2046];    // we do start at 0 and there cannot be more than 2046
struct sockaddr_in fasit_addr;
int verbose;
int slottime,total_slots,inittime;
int low_dev,high_dev;

void print_help(int exval) {
   printf("mcp [-h] [-v num] [-f ip] [-p port] [-r ip] [-m port] [-n minioncount]\n\n");
   printf("  -h            print this help and exit\n");
   printf("  -f 127.0.0.1  set FASIT server IP address\n");
   printf("  -p 4000       set FASIT server port address\n");
   printf("  -r 127.0.0.1  set RFmaster server IP address\n");
   printf("  -m 4004       set RFmaster server port address\n");
   printf("  -t 50         hunttime in seconds.  Time we wait before doing a slave hunt again\n");
   printf("  -i 250        initial wait time in ms (5ms granules, 1275 ms max\n");
   printf("  -s 150        slottime in ms (5ms granules, 1275 ms max\n");
   printf("  -d 0x20       Lowest devID to find\n");
   printf("  -D 0x30       Highest devID to find\n");
   print_verbosity();
   exit(exval);
}

#define RF_MASTER_DELAY 250 /* the amount of time to wait for the RFmaster to receive the data and send it */

#define EXIT(_e) { \
   DCMSG(RED, "\n\nExiting MCP on line %i, current errno: %i", __LINE__, errno); \
   exit(_e); \
}
// kill switch to program
int close_nicely = 0;
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



#define BufSize 1024


/**  has to figure out what devid's are in use so we can set up the forget bits
 **
 **  */
uint8 set_forget_bits(int low_dev,int high_dev,addr_t *addr_pool,int max_addr){
   int addr,testid;
   uint8 bits=0xff;              // them all by default

   addr=1;
   DDCMSG(D_MEGA,RED,"SFB:------------------------ max_addr=%d",max_addr);
   high_dev=min(low_dev+7,high_dev);   // look for only devid's within 7 of low_dev, or < high_dev
   while (addr<max_addr){       // spin through our addresses
      testid=addr_pool[addr].devid;
      if (addr_pool[addr].inuse&&(testid>=low_dev)&&(testid<=high_dev)){       // we have match
         bits^=BV(testid-low_dev);      // turn off the forget bit for this devid
      }
      DDCMSG(D_MEGA,BLUE,"SFB:  addr=%d testid=%x  bits=0x%8x",addr,testid,bits);
      addr++;           // try the next address
   }
   return(bits);
}


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
   int timeout,addr_cnt;
   int cmd,crcFlag,plength,dev_addr;
   thread_data_t *minion;
   int rereg=1;
   addr_t addr_pool[2048];      // 0 and 2047 cannot be used
   int max_addr;                //  count of our max_addr given out
   int hunttime,slave_hunting,low,hunt_rotate=0, delta;
   struct timespec elapsed_time, start_time, istart_time,delta_time, dwait_time;
   int tempslot = 0; // keep track if of what we're adding to our timeout based on having the slavehunt not overlap normal commands:requests

   LB_packet_t *LB,LB_buf;
   LB_request_new_t *LB_new;
   LB_device_reg_t *LB_devreg;
   LB_assign_addr_t *LB_addr;

   // install signal handlers
   //signal(SIGINT, quitproc);
   //signal(SIGQUIT, quitproc);

   clock_gettime(CLOCK_MONOTONIC,&istart_time); // get the intial current time
   timestamp(&elapsed_time,&istart_time,&delta_time);   // make sure the delta_time gets set    

   // process the arguments
   //  -f 192.168.10.203   RCC ip address
   //  -p 4000          RCC port number
   //  -r 127.0.0.1     RFmaster ip address
   //  -m 4004          RFmaster port number
   //  -n 1             Number of minions to fire up
   //  -v 1             Verbosity level

   // MAX_NUM_Minions is defined in mcp.h, and minnum - the current number of minions.
   minnum = 0;  // now start with no minions
   verbose=0;

   slave_hunting=1;     // want to hunt on startup
   hunttime=30;

   /* start with a clean address structures */
   memset(&fasit_addr, 0, sizeof(struct sockaddr_in));
   fasit_addr.sin_family = AF_INET;
   fasit_addr.sin_addr.s_addr = inet_addr("192.168.10.203");    // fasit server the minions will connect to
   fasit_addr.sin_port = htons(4000);                           // fasit server port number
   memset(&RF_addr, 0, sizeof(struct sockaddr_in));
   RF_addr.sin_family = AF_INET;
   RF_addr.sin_addr.s_addr = inet_addr("127.0.0.1");            // RFmaster server the MCP connects to
   RF_addr.sin_port = htons(4004);                              // RFmaster server port number

   while((opt = getopt(argc, argv, "hv:m:r:i:p:f:s:d:D:t:")) != -1) {
      switch(opt) {
         case 'h':
            print_help(0);
            break;

         case 'v':
            verbose = strtoul(optarg,NULL,16);
            break;

         case 'i':
            inittime = atoi(optarg);    // leave it in ms until it gets sent in a packet
            break;

         case 's':
            slottime = atoi(optarg);    // leave it in ms until it gets sent in a packet
            break;

         case 't':
            hunttime = atoi(optarg);    // time in seconds before we hunt for slaves again
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
            EMSG("Error - Option `%c' needs a value\n\n", optopt);
            print_help(1);
            break;

         case '?':
            EMSG("Error - No such option: `%c'\n\n", optopt);
            print_help(1);

            break;
      }
   }
   DCMSG(BLUE,"MCP: verbosity is set to 0x%x", verbose);
   DCMSG(BLUE,"MCP: FASIT SERVER address = %s:%d", inet_ntoa(fasit_addr.sin_addr),htons(fasit_addr.sin_port));
   DCMSG(BLUE,"MCP: RFmaster SERVER address = %s:%d", inet_ntoa(RF_addr.sin_addr),htons(RF_addr.sin_port));
   print_verbosity_bits();

   // zero the address pool
   for(i=0; i<2048; i++) {
      addr_pool[i].devid=0xffffffff; // invalid devid
      addr_pool[i].inuse=0;
      addr_pool[i].mID=0;
   }
   max_addr=1;  // 0 is unused.  max_addr==1 also means empty

   if (!slottime || (high_dev<low_dev)){
      DCMSG(RED,"\nMCP: slottime=%d must be set and high_dev=%d cannot be less than low_dev=%d",slottime,high_dev,low_dev);
      EXIT(-1);
   } else {

      DCMSG(RED,"MCP: slottime=%d  low_dev=0x%x high_dev=0x%x  total_slots=%d",slottime,low_dev,high_dev,total_slots);
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
      PERROR("socket() failed");
   }

   // use the RF_addr structure that was set by default or option arguments
   result=connect(RF_sock,(struct sockaddr *) &RF_addr, sizeof(struct sockaddr_in));
   if (result<0){
      strerror_r(errno,buf,BufSize);
      DCMSG(RED,"MCP RF Master server not found! connect(%s:%d) error: %s  ", inet_ntoa(RF_addr.sin_addr),htons(RF_addr.sin_port),buf);
      EXIT(-1);
   }

   // we now have a socket to the RF Master.
   DCMSG(RED,"MCP has a socket to the RF Master server at %s:%d ", inet_ntoa(RF_addr.sin_addr),htons(RF_addr.sin_port));

   /****************************************************************
    ******
    ******    Acutally we need to wait for the RF master to report
    ******  registered Slaves, and then we spawn a minion for each
    ******  new one of those.
    ******
    ******
    ****************************************************************/

   /* set up polling so we can monitor the minions and the RFmaster*/
   fds = epoll_create(2048);
   memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN;
   // listen to the RFmaster
   ev.data.fd = RF_sock; // indicates listener fd
   if (epoll_ctl(fds, EPOLL_CTL_ADD, RF_sock, &ev) < 0) {
      PERROR("MCP: epoll RF_sock insertion error:\n");
      EXIT(-1);
   }

   mID=0;
   timeout = 3; // quick the first time

   // loop until we lose connection to the rfmaster.
   while(!close_nicely) {
      // grab timestamp before epoll..
      timestamp(&elapsed_time,&istart_time,&delta_time);
      // now add my delta to my dwait 
      dwait_time.tv_sec+=delta_time.tv_sec;	 // add seconds
      dwait_time.tv_nsec+=delta_time.tv_nsec; // add nanoseconds
      if (dwait_time.tv_nsec>=1000000000L) {  // fix nanoseconds
         dwait_time.tv_sec++;
         dwait_time.tv_nsec-=1000000000L;
      }
      delta = (dwait_time.tv_sec*1000)+(dwait_time.tv_nsec/1000000);

      // wait for data from either the RFmaster or from a minion
      DDCMSG(D_NEW,RED,"MCP: epoll_wait with timeout=%d  slave_hunting=%d low=%x hunttime=%d, dwait=%5ld.%09ld, delta=%i, timeout-delta=%i", timeout, slave_hunting, low, hunttime, dwait_time.tv_sec, dwait_time.tv_nsec, delta, timeout-delta);

      ready_fd_count = epoll_wait(fds, events, MAX_NUM_Minions, max(0,timeout-delta));
      DDCMSG(D_POLL,RED,"MCP: epoll_wait over   ready_fd_count = %d",ready_fd_count);

      // ...and grab timestamp after epoll (without this second one the times don't stay properly set)
      timestamp(&elapsed_time,&istart_time,&delta_time);
      // now add my delta to my dwait 
      dwait_time.tv_sec+=delta_time.tv_sec;	 // add seconds
      dwait_time.tv_nsec+=delta_time.tv_nsec; // add nanoseconds
      if (dwait_time.tv_nsec>=1000000000L) {  // fix nanoseconds
         dwait_time.tv_sec++;
         dwait_time.tv_nsec-=1000000000L;
      }
      delta = (dwait_time.tv_sec*1000)+(dwait_time.tv_nsec/1000000);
      DDCMSG(D_NEW,YELLOW,"MCP: after epoll_wait with timeout=%d  slave_hunting=%d low=%x hunttime=%d, dwait=%5ld.%09ld, delta=%i, timeout-delta=%i", timeout, slave_hunting, low, hunttime, dwait_time.tv_sec, dwait_time.tv_nsec, delta, timeout-delta);
      if (close_nicely) {break;}


      //  if we have no minions, or we have been idle long enough - so we
      //      build a LB packet "request new devices"
      //  send_LB();   // send it
      //
      if (!ready_fd_count /* || idle long enough */  ){
         if (slave_hunting==1){
            low=low_dev;
            slave_hunting++;
         } else if (slave_hunting>1&&slave_hunting<(((high_dev-low_dev)/8)+2)){
            low=low_dev+((slave_hunting-1)*8); // step through 16 at a time after the first 2
            if (low>=high_dev) low=low_dev;             // if we went to far, redo the bottom end
            slave_hunting++;
         } else {
            // the hunt is over, for now
            slave_hunting=1;
            low=low_dev;                // restart the hunt at the beginning
            hunt_rotate = 1;
         } 

         // reset our pseudo-burst slot value
         tempslot = 0;

         // if we've rotated through the hunt, use the hunttime, otherwise use slottime
         if (hunt_rotate) {
            timeout=hunttime*1000;      // idle time to wait for next go around
         } else {
            timeout=slottime*10;        // idle time to wait for next go around
         }
         timeout+=(inittime + RF_MASTER_DELAY); // add initial time and just a little bit more so the RFmaster can keep up (RFmaster will collect messages for 200ms minimum)
         // adjust dwait_time to the time to nothing, as we haven't waited anything yet
         dwait_time.tv_sec=0; dwait_time.tv_nsec=0;

         DDCMSG(D_NEW,RED,"MCP:  Build a LB request new devices messages. timeout=%d slave_hunting=%d low=%x hunttime=%d, dwait=%5ld.%09ld",timeout,slave_hunting,low,hunttime, dwait_time.tv_sec, dwait_time.tv_nsec);
         /***  we need to use the range we were invoked with
          ***  we need to handle a range greater than 32
          ***  the forget_addr needs to be set by looking at the addr_pool
          ***     call a function to get the forget bits - so we can reduce code clutter
          ***  */

         //   for now the request new devices will just use payload len=0
         LB_new=(LB_request_new_t *) &LB_buf;
         LB_new->cmd=LBC_REQUEST_NEW;

         LB_new->forget_addr=set_forget_bits(low,high_dev,addr_pool,max_addr);  // tell everybody to forget
         LB_new->low_dev=low;
         LB_new->inittime=inittime/5;   // adjust to 5ms granularity
         LB_new->slottime=slottime/5;   // adjust to 5ms granularity

         // calculates the correct CRC and adds it to the end of the packet payload
         // also fills in the length field
         set_crc8(LB_new);
         // now send it to the RF master
         result=write(RF_sock,LB_new,RF_size(LB_new->cmd));
         if (verbose&D_RF){     // don't do the sprintf if we don't need to
            sprintf(hbuf,"MCP: sent %d (%d) bytes  LB request_newdev  ",result,RF_size(LB_new->cmd));
            DDCMSG_HEXB(D_RF,YELLOW,hbuf,LB_new, RF_size(LB_new->cmd));
         }
      } else { // we have some fd's ready
         // check for ready minions or RF
         for (ready_fd=0; ready_fd<ready_fd_count; ready_fd++) {
            // if we have data from RF, it is a LB packet we need to decode and pass on,
            // or one of our 'special packets' that means something else - irregardless we have to
            // have a case statement to process it properly

            //  only in the RF_sock case is the event[].data.fd actually an fd and not a ptr
            if (RF_sock==events[ready_fd].data.fd){
               DDCMSG(D_POLL,RED,"MCP: RF_sock(%d)==events[ready_fd=%d].data.fd=%d   MCP socket is ready",
                      RF_sock,ready_fd,events[ready_fd].data.fd);
               
               msglen=read(RF_sock, buf, 1); // grab first byte so we know how many to grab
               if (msglen == 1) {
                  LB=(LB_packet_t *)buf;   // map the header in
                  msglen+=read(RF_sock, (buf+1), RF_size(LB->cmd)-1); // grab the rest, but leave the rest to be read again later
                  DDCMSG(D_RF, RED, "MCP %i read bytes with %i cmd", msglen, LB->cmd);
                  DDpacket(buf,msglen);
               }
               if (msglen<0) {
                  strerror_r(errno,buf,BufSize);
                  DCMSG(RED,"MCP: RF_sock error %s  fd=%d\n",buf,RF_sock);
                  EXIT(-1);
               }
               if (msglen==0) {
                  DCMSG(RED,"MCP: RF_sock returned 0 -   Socket to RFmaster closed.");
                  DCMSG(RED,"MCP:   This is where the code should loop and try to reconnect to the RFmaster.");
                  DCMSG(RED,"MCP:   The whole chunk of code for attaching to the RFmaster should be brought inside the while(forever) loop");
                  DCMSG(RED,"MCP:   then the loop will hang until there is an RFmaster to attach or re-attach to.");
                  EXIT(-1);
               }

               DDCMSG(D_POLL,RED,"MCP:  read %d from RF socket",msglen);

               LB=(LB_packet_t *)buf;
               // do some checking - like to see if the CRC is OKAY
               // although the CRC should be done in the RFmaster process

               // actually can't check until we know how long it is
               //                   DDCMSG(D_RF,RED,"crc=%d  checked = %d",LB->crc,crc8(&LB,2));
               // Iff the CRC is okay we can process it, otherwise throw it away

               if (verbose&D_RF){       // don't do the sprintf if we don't need to
                  sprintf(hbuf,"MCP: read of LB packet from RFmaster address=%d  cmd=%d  msglen=%d",LB->addr,LB->cmd,msglen);
                  DDCMSG2_HEXB(D_RF,YELLOW,hbuf,buf,msglen);
               }
               if (verbose&D_PARSE){       // don't do the debug print if we don't need to
                  DDpacket(buf,msglen);
               }
               //      process recieved LB packets 
               //  mcp only handles registration and addressing packets,
               //  mcp also has to pass new_address LB packets on to the minion so it can figure out it's own RF_address
               //  mcp passes all other LB packets on to the minions they are destined for

               // reset our pseudo-burst slot value
               tempslot = 0;

               if  (LB->cmd==LBC_DEVICE_REG){
                  LB_devreg =(LB_device_reg_t *)(LB);   // change our pointer to the correct packet type
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
                  DDCMSG(D_RF|D_MEGA,GRAY,"MCP: CHECK for minion with this devID   max_addr=%d  devID=%06x",max_addr,LB_devreg->devid);

                  addr_cnt=1;           //  step through to check if we already had this one
                  DDCMSG(D_RF|D_MEGA,GRAY,"MCP:     ( (addr_cnt=%d)<(max_addr=%d)) && ((addr_pool[%d].devid=%x)!=(LB_devreg->devid=%x)) && (addr_pool[%d].inuse=%d)",
                         addr_cnt,max_addr,addr_cnt,addr_pool[addr_cnt].devid, LB_devreg->devid, addr_cnt,addr_pool[addr_cnt].inuse);

                  while ((addr_cnt<max_addr)&&(addr_pool[addr_cnt].devid!=LB_devreg->devid)&&addr_pool[addr_cnt].inuse) {
                     DDCMSG(D_RF|D_MEGA,GRAY,"MCP: ( (addr_cnt=%d)<(max_addr=%d)) && ((addr_pool[%d].devid=%x)!=(LB_devreg->devid=%x)) ... (addr_pool[%d].inuse=%d)",
                            addr_cnt,max_addr,addr_cnt,addr_pool[addr_cnt].devid, LB_devreg->devid, addr_cnt,addr_pool[addr_cnt].inuse);
                     addr_cnt++;
                  }

                  if (addr_cnt<max_addr && addr_pool[addr_cnt].inuse &&
                      addr_pool[addr_cnt].mID > 0 &&
                      minions[addr_pool[addr_cnt].mID].status==S_open) {
                     // we already have this devID, so reconnect because the RFslave probably rebooted or something
                     LB_status_resp_t lbsr;
                     DCMSG(GRAY,"MCP: just send a new address assignment to reconnect the minon and RFslave  devid=%06x",LB_devreg->devid);

                     // create a message to send to the already initiliazed minion
#if 0
                     mID = addr_pool[addr_cnt].mID;
                     lbsr.cmd = LBC_STATUS_RESP_EXT; // copy stuff to extended response, it's got it all
                     lbsr.hits = LB_devreg->hits;
                     lbsr.expose = LB_devreg->expose;
                     lbsr.speed = LB_devreg->speed;
                     lbsr.move = LB_devreg->move;
                     lbsr.react = LB_devreg->react;
                     lbsr.location = LB_devreg->location;
                     lbsr.hitmode = LB_devreg->hitmode;
                     lbsr.timehits = LB_devreg->timehits;
                     lbsr.fault = LB_devreg->fault;
                     set_crc8(&lbsr);
                     result=write(minions[mID].minion,&lbsr,RF_size(lbsr.cmd));
                     if (result<0) {
                        DCMSG(RED,"MCP: reconnect Sent %d bytes to minion %d at fd=%d",
                              result,mID,minions[mID].minion);
                        EXIT(-1);
                     }

                     DDCMSG(D_NEW,BLUE,"MCP: reconnect Sent %d bytes to minion %d  fd=%d",
                            result,mID,minions[mID].minion);
#endif


                  } else {
                     int oldID=mID+1; // assume we're incrementing minion count
                     if (addr_pool[addr_cnt].mID > 0 &&
                         minions[addr_pool[addr_cnt].mID].status==S_closed) {
                        // we used to have this devID, but the minion disconnected, re-use it
                        mID=addr_pool[addr_cnt].mID;
                        oldID--; // un-increment minion count
                     } else {
                        // we do not have this devID.  Look for the first free address to use
                        addr_cnt=1;                //  step through to check to find unused
                        while (addr_pool[addr_cnt].inuse) addr_cnt++;      // look for an unused address slot
                        mID+=1; // increment our minion count
                     }
                     DDCMSG(D_RF|D_VERY,YELLOW,"MCP: check for unused addr addr_cnt=%d devtype=%d devid=0x%06x",
                            addr_cnt,LB_devreg->dev_type,LB_devreg->devid);

                     if (addr_cnt>2046){
                        DCMSG(RED,"MCP: all RF addresses in use  devtype=%d devid=0x%06x"
                              ,LB_devreg->dev_type,LB_devreg->devid);
                     } else {
                        // we have a free address at addr_cnt
                        max_addr=max(max_addr,addr_cnt+1);      // we probably have a higher address in use
                        addr_pool[addr_cnt].mID=mID;                    // update the addr_pool
                        addr_pool[addr_cnt].devid=LB_devreg->devid;
                        addr_pool[addr_cnt].inuse=1;                    // make sure we mark that we are in use!

                        /* open a bidirectional pipe for communication with the minion  */
                        if (socketpair(AF_UNIX,SOCK_STREAM,0,((int *) &minions[mID].mcp_sock))){
                           PERROR("opening stream socket pair");
                           minions[mID].status=S_closed;
                        } else {
                           minions[mID].status=S_open;
                        }

                        /****
                         ****    Initialize the minion thread data -
                         ****/

                        minions[mID].mID=mID;                   // make sure we pass the minion ID down
                        minions[mID].RF_addr=addr_cnt;          // RF_addr
                        minions[mID].devid=LB_devreg->devid;    // use the actual device id (MAC address)
                        minions[mID].seq=0;     // the seq number for this minions transmissions to RCC

                        switch (LB_devreg->dev_type) {
                           case RF_Type_SIT_W_MFS:
                              minions[mID].S.cap|=PD_NES;       // add the NES capability
                              minions[mID].S.dev_type = Type_SIT;
                              break;
                           case RF_Type_SIT:
                              minions[mID].S.dev_type = Type_SIT;
                              break;
                           case RF_Type_SAT:
                              minions[mID].S.dev_type = Type_SAT;
                              break;
                           case RF_Type_HSAT:
                              minions[mID].S.dev_type = Type_HSAT;
                              break;
                           case RF_Type_SES:
                              minions[mID].S.dev_type = Type_SES;
                              break;
                           case RF_Type_BES:
                              minions[mID].S.dev_type = Type_BES;
                              break;
                           case RF_Type_MIT:
                              minions[mID].S.dev_type = Type_MIT;
                              break;
                           case RF_Type_MAT:
                              minions[mID].S.dev_type = Type_MAT;
                              break;
                           case RF_Type_Unknown:
                              minions[mID].S.dev_type = Type_SIT;
                              break;
                        }

                        ////////  initialize the rest of the minion state from what was reported in the registration packet

                        minions[mID].S.hit.data = 0;
                        minions[mID].S.exp.data = LB_devreg->expose?90:0;
                        DCMSG(GRAY, "Found REG expose: exp.data=%i, LB_devreg->expose=%i @ %i", minions[mID].S.exp.data, LB_devreg->expose, __LINE__);
                        minions[mID].S.speed.data = LB_devreg->speed*100.0;
                        minions[mID].S.move.data = LB_devreg->move;
                        minions[mID].S.react.data = LB_devreg->react;
                        minions[mID].S.position.data = LB_devreg->location;
                        minions[mID].S.mode.data = LB_devreg->hitmode;
                        minions[mID].S.burst.newdata = LB_devreg->timehits;        // not sure what this is, it is in an ext response packet

                        minions[mID].S.fault.data = LB_devreg->fault;

                        // new state timer code
                        // initialize state of expose or conceal timer
                        if (LB_devreg->expose) {
                           START_EXPOSE_TIMER(minions[mID].S);
                        } else {
                           START_CONCEAL_TIMER(minions[mID].S);
                        }

                        // maybe there should also be stuff for MFS (NES?) MGS MILES and GPS in the device_reg packet

                        /*   fork a minion */    
                        if ((child = fork()) == -1) PERROR("fork");

                        if (child) {
                           /* This is the parent. */
                           DDCMSG(D_MINION,RED,"MCP forked minion %d", mID);
                           DDCMSG(D_MINION|D_VERY, BLACK, "-------------------------------\nMinion %i:%i\n-------------------------------", mID, addr_cnt);
                           close(minions[mID].mcp_sock);

                        } else {
                           /* This is the child. */
                           close(minions[mID].minion);
                           minion_thread(&minions[mID]);
                           DDCMSG(D_MINION|D_VERY,BLACK,"MCP: minion_thread(...) returned. that minion must have died, so do something smart here like remove it");

                           EXIT(0);
                           break;       // if we come back , bail to another level or something more fatal
                        }

                        // add the minion to the set of file descriptors
                        // that are monitored by epoll
                        ev.events = EPOLLIN|EPOLLPRI|EPOLLRDHUP;
//                        ev.data.fd = minions[mID].minion;     // since fd is a union with ptr, this is a failure!
                        ev.data.ptr = (void *) &minions[mID]; 

                        if (epoll_ctl(fds, EPOLL_CTL_ADD, minions[mID].minion, &ev) < 0) {
                           PERROR("MCP: epoll set insertion error: \n");
                           EXIT(-1);
                        }

                        DDCMSG(D_MINION|D_VERY,RED,"MCP: assigning address slot #%d  mID=%d inuse=%d fd=%d"
                               ,addr_cnt,addr_pool[addr_cnt].mID,addr_pool[addr_cnt].inuse,minions[addr_pool[addr_cnt].mID].minion);


                        /***   end of create a new minion 
                         ****************************************************************************/
                     } // else addr_cnt>2046   - not all used
                     mID=oldID;
                  } // if addr_cnt>2046  devid not found so we made a new minon

                  // Create an LB ASSIGN ADDR packet to send back to the slaves
                  // addr_cnt  should be the RF_addr slot  it already had

                  LB_addr =(LB_assign_addr_t *)(&LB_buf);       // map our bitfields in

                  LB_addr->cmd=LBC_ASSIGN_ADDR;
                  LB_addr->reregister=1;                        // might be useless
                  LB_addr->devid=addr_pool[addr_cnt].devid;     // get what might have been given out earlier
                  LB_addr->new_addr=addr_cnt;                   // the actual slot is the perm address

                  DDCMSG(D_RF|D_VERY,RED,"MCP: regX Build a LB device addr packet to assign the address slot %4i %4i"
                         ,addr_cnt,LB_addr->new_addr);

                  // calculates the correct CRC and adds it to the end of the packet payload
                  // also fills in the length field
                  set_crc8(LB_addr);
                  if (verbose&D_RF){    // don't do the sprintf if we don't need to
                     sprintf(hbuf,"MCP: regX LB packet: RF_addr=%4i new_addr=%d cmd=%2i msglen=%d",
                             addr_cnt,LB_addr->new_addr,LB_addr->cmd,RF_size(LB_addr->cmd));
                     DDCMSG2_HEXB(D_RF,BLUE,hbuf,LB_addr,7);
                  }     

                  // this packet must also get sent to the minion
                  result=write(minions[addr_pool[addr_cnt].mID].minion,LB_addr,RF_size(LB_addr->cmd));
                  if (result<0) {
                     DCMSG(RED,"MCP: regX Sent %d bytes to minion %d at fd=%d",
                           result,addr_cnt,minions[addr_pool[addr_cnt].mID].minion);
                     EXIT(-1);
                  }

                  DDCMSG(D_RF|D_VERY,BLUE,"MCP: regX  Sent %d bytes to minion %d  fd=%d",
                         result,addr_pool[addr_cnt].mID,minions[addr_pool[addr_cnt].mID].minion);

                  // now send it to the RF master
                  result=write(RF_sock,LB_addr,RF_size(LB_addr->cmd));
                  if (result<0) {
                     strerror_r(errno,buf,BufSize);                         
                     DCMSG(RED,"MCP: regX write to RF_sock error %s  fd=%d\n",buf,RF_sock);
                     EXIT(-1);
                  }

                  DDCMSG(D_RF,RED,"MCP: regX Sent %d bytes to RF fd=%d",result,RF_sock);

                  // done handling the device_reg packet
               } else { // it is any other command than dev regx
                  // which means we just copy it on to the minion so it can process it

                  ////   There WAS/is? a bug here where we (the MCP) was sending the packet to the wrong minion
                  ////    No, we are sending the packet back to ourselves..................
                  //       display the packet for debugging
                  LB=(LB_packet_t *)buf;
                  if (addr_pool[LB->addr].inuse && minions[addr_pool[LB->addr].mID].status != S_closed) {
                     if (verbose&D_RF){ // don't do the sprintf if we don't need to
                        sprintf(hbuf,"MCP: passing RF packet from RF_addr %d on to Minion %d (fd=%d).   cmd=%2i  length=%d msglen=%d"
                                ,LB->addr,addr_pool[LB->addr].mID,minions[addr_pool[LB->addr].mID].minion,LB->cmd,RF_size(LB->cmd),msglen);
                        DDCMSG2_HEXB(D_RF,RED,hbuf,LB,RF_size(LB->cmd));
                     }
                     ////////////////////////////////////////////////////////////////  minions[mID].minion  should be the right fd for the minion   //////////////////////////////////////
                     // do the copy down here
                     result=write(minions[addr_pool[LB->addr].mID].minion,LB,RF_size(LB->cmd));    
                     if (result<0) {
                        strerror_r(errno,buf,BufSize);                      
                        DCMSG(RED,"MCP:  write to minion %d error %s  fd=%d",
                              addr_pool[LB->addr].mID,buf,minions[addr_pool[addr_cnt].mID].minion);
                        EXIT(-1);
                     }

                     DDCMSG(D_RF,BLUE,"MCP: @3 Sent %d bytes to minion %d at fd=%d",
                            result,addr_pool[LB->addr].mID,minions[addr_pool[LB->addr].mID].minion);
                  }  // the address in in use - which is good, we wnat to send it somewhere
               } // else it is a command other than devreg
            } // if it was a RF_sock fd that was ready


            else { // it is from a minion
               //   we have to do some processing, mainly just pass on to the RF_sock

               DDCMSG(D_POLL,RED,"MCP: ----------------\n-------------------\n------- minion is ready   ");

               /***
                *** we are ready to read from a minion - use the event.data.ptr to know the minion
                ***   we should just have to copy the message down to the RF
                ***   and we need to deal with special cases like the minion dieing.
                ***   */

               //                   
               minion=(thread_data_t *)events[ready_fd].data.ptr;
               minion_fd=minion->minion;                    
               //mID=minion->mID; don't overwrite this value, we are keeping track of stuff elsewhere with it
DDCMSG(D_POINTER, GRAY, "Events for %i:\tEPOLLIN:%i\tEPOLLPRI:%i\tEPOLLRDHUP:%i\tEPOLLERR:%i\tEPOLLHUP:%i", minion->mID, (events[ready_fd].events&EPOLLIN)!=0, (events[ready_fd].events&EPOLLPRI)!=0, (events[ready_fd].events&EPOLLRDHUP)!=0, (events[ready_fd].events&EPOLLERR)!=0, (events[ready_fd].events&EPOLLHUP)!=0);
               DDCMSG(D_POLL,RED,"MCP: events[ready_fd=%d] minion->minion=%d for minion %d ready  [RF_addr=%d]  -}",
                      ready_fd,minion_fd,minion->mID,minion->RF_addr);             

               DDCMSG(D_POLL,BLUE,"MCP: fd %d for minion %d ready  [RF_addr=%d]  -}",minion_fd,minion->mID,minion->RF_addr);
               if(minion_fd>2048){
                  DCMSG(RED,"MCP:  Trying create more than 2048 minions!  Call the Wizard");
                  EXIT(-1);
               }
               // pre-empt a failure to read by looking at *HUP statuses
               if (events[ready_fd].events&EPOLLRDHUP || events[ready_fd].events&EPOLLRDHUP) {
                  DDCMSG(D_RF|D_VERY, RED, "MCP: pretended to read from minion %i (%i:%s)!\n", minion->mID, errno, strerror(errno));
                  msglen = 0; // pretend we closed
                  buf[0] = '\0'; // clear buffer
               } else {
                  msglen=read(minion_fd, buf, 1023);
               }
               LB=(LB_packet_t *)buf;
               if (verbose&D_RF){       // don't do the sprintf if we don't need to
                  sprintf(hbuf,"MCP:    read Minion %2i's LB packet(s). address=%2i  cmd=%2i  length=%2i msglen=%2i  -}"
                          ,minion->mID,LB->addr,LB->cmd,RF_size(LB->cmd),msglen);
                  DDCMSG_HEXB(D_RF,BLUE,hbuf,buf,msglen);
               }
// handle bad read as proper disconnect below
               if (LB->cmd==LBC_ILLEGAL || msglen <= 0){
                  DDCMSG(D_RF, RED, "MCP:  attempted read from minion returned %i (%i:%s)!\n", msglen, errno, strerror(errno));
                  DDCMSG(D_RF,BLACK,"Dead minion %i:%i, closing down fd %i", minion->mID, minion->RF_addr, minion_fd);
                  // minion is dead, reset forget bit for it
                  addr_pool[minion->RF_addr].inuse=0;
                  // reset various information
                  minion->S.fault.data = 0;
                  minion->S.hit.data = 0;
// re-use this spot when slave reconnects
//                  addr_pool[minion->RF_addr].devid=0xffffffff; // invalid devid
//                  addr_pool[minion->RF_addr].mID=0;
                  // remove from epoll
                  if (epoll_ctl(fds, EPOLL_CTL_DEL, minion_fd, NULL) < 0) { /* still remove from epoll as we're not properly handling this */
                     DCMSG(RED, "Failure to remove %i from epoll: %s", minion_fd, strerror(errno));
//                     //close(minion_fd);
                     EXIT(-1);
                  }
                  // close connection
                  minion->status=S_closed;
                  close(minion_fd);
               } else {
                  // move packet to RFmaster and display the packet for debugging
                  LB=(LB_packet_t *)buf;
                  // do the copy down here
//                  DDCMSG(D_POINTER, GRAY, "Writing buf %p len %i", LB, msglen);
                  result=write(RF_sock,LB,msglen);
                  if (result<0) {
                     strerror_r(errno,buf,BufSize);                         
                     DCMSG(RED,"MCP:  write to RF_sock error %s  fd=%d\n",buf,RF_sock);
                     EXIT(-1);
                  }
                  if (verbose&D_RF){    // don't do the sprintf if we don't need to
                     sprintf(hbuf,"MCP: passing Minion %2i's LB packet(s) to RF_addr=%2i cmd=%2i length=%2i result=%2i  -}"
                             ,minion->mID,LB->addr,LB->cmd,msglen,result);
                     DDCMSG_HEXB(D_RF,BLUE,hbuf,buf,result);
                  }
                  if (tempslot == 0) {
                     // first in pseudo-burst to RFmaster, include inittime
                     tempslot = inittime + slottime;
                  } else {
                     // not the first, just use slottime
                     tempslot = slottime;
                  }
                  timeout+=tempslot; // we now need to wait for this packet to respond
                  DDCMSG(D_NEW, MAGENTA, "Minion %i caused increase of timeout by %i to %i (timeout-delta=%i)",minion->mID, tempslot, timeout, timeout-delta);
               }

            } // it is from a minion

         } //  for all the ready fd's
      }  // else we did not time out
   } //while forever loop
   DCMSG(BLACK,"mcp says goodbye...");
} // end of main

