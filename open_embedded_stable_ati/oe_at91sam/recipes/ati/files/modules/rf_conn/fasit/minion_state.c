#include "math.h"
#include "mcp.h"
#include "rf.h"
#include "fasit_c.h"

#define S_set(ITEM,D,ND,F,T) \
    { \
       S->ITEM.data = D; \
       S->ITEM.newdata = ND; \
       S->ITEM.flags = F; \
       S->ITEM.timer = T; \
    }

#undef CLOCK_MONOTONIC_RAW 
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC

extern int verbose;
extern struct sockaddr_in fasit_addr;
extern int close_nicely;

int psend_mcp(thread_data_t *minion,void *Lc);

void minion_state(thread_data_t *minion, minion_time_t *mt, minion_bufs_t *mb) {

   long elapsed_tenths;
   int result;
   LB_packet_t         LB_buf;
   LB_status_req_t     *LB_status_req;
   /***   what we do is use the minion's state_timer to determine how long to wait
    ***   set up above in the select.
    ***      if the select times out, we are good.
    ***      if the select had a file descriptor ready, then we probably didn't wait long enough.
    ***      [[[[ FIX THAT ]]]]
    ***
    ***      otherwise, if we got here at least one of our timers has expired signifying that
    ***      some item[s] in the state need some action.
    ***/

#if 1
   timestamp(&mt->elapsed_time,&mt->istart_time,&mt->delta_time);
   DDCMSG(D_TIME,CYAN,"MINION %d: Begin timer updates at %6ld.%09ld timestamp, delta=%1ld.%09ld"
          ,minion->mID,mt->elapsed_time.tv_sec, mt->elapsed_time.tv_nsec,mt->delta_time.tv_sec, mt->delta_time.tv_nsec);

   elapsed_tenths = mt->elapsed_time.tv_sec*10+(mt->elapsed_time.tv_nsec/100000000L);

   if (elapsed_tenths==0){
      // set the next timeout
      mt->timeout.tv_sec=0;
      mt->timeout.tv_usec=(100000000L-mt->elapsed_time.tv_nsec)*1000; // remainder of 1 decisecond in microseconds

   } else {
      // set the next timeout
      mt->timeout.tv_sec=0;
      mt->timeout.tv_usec=100000; // 1 tenth later
   }
#endif

   /**********    now update all the time related bits
    **
    **  we reached this point because either there was a RCC or MCP communication,
    **  or a timeout on that select which started at 100ms (one tenth of a second)
    **
    **  so we now run through the flags of all the state varibles,
    **  and update them if they were waiting on a timer
    **
    **  each state variable has a timer for physical reaction time simulation.
    **    each one of those is up to 64k (tenths of a second)
    **
    **  and the entire minion should have a timer to keep track of when the actual
    **  slave it is simulating last responded - or something
    **
    **  */

   if (!minion->S.state_timer)  { // timer reached zero
      //   We should only do the section if the next timer is really up, and
      //   not just if another fd was ready about the same time
      DDCMSG(D_MSTATE,RED,"MINION %d: timer update.   rf_t.flags/timer=0x%x/%i   exp.flags/timer=0x%x/%i   event.flags/timer=0x%x/%i",
             minion->mID,minion->S.rf_t.flags,minion->S.rf_t.timer,minion->S.exp.flags,minion->S.exp.timer,minion->S.event.flags,minion->S.event.timer);

      // if there is an rf timer flag set, we then need to do something
      if (1||minion->S.rf_t.flags) {

         if (minion->S.rf_t.timer>elapsed_tenths){
            minion->S.rf_t.timer-=elapsed_tenths;   // not timed out.  but decrement out timer
            DDCMSG(D_MSTATE,RED,"MINION %d: not timed out.   S.rf_t.timer=%d   S.rf_t.timer=%d",minion->mID,minion->S.rf_t.timer,minion->S.rf_t.timer);

         } else {    // timed out,  move to the next state, do what it should.

            switch (minion->S.rf_t.flags) {
               case F_rf_t_waiting_short:
                  // break connection to FASIT server
                  close(minion->rcc_sock); // close FASIT
                  DCMSG(BLACK,"\n\n-----------------------------------\nDisconnected minion %i:%i:%i\n-----------------------------------\n\n",
                        minion->mID, minion->rcc_sock, minion->mcp_sock);
                  minion->rcc_sock = -1;
                  // set "forget" flag for this minion
                  // clear waiting flags
                  minion->S.rf_t.flags = 0;
                  minion->S.rf_t.timer = 0;
                  minion->status = S_closed;
                  LB_buf.cmd = LBC_ILLEGAL; // send an illegal packet, which makes mcp forget me
                  // now send it to the MCP master
                  DDCMSG(D_PACKET,BLUE,"Minion %d:  LBC_ILLEGAL cmd=%d", minion->mID,LB_buf.cmd);
                  result= psend_mcp(minion,&LB_buf);
                  close(minion->mcp_sock); // close mcp
                  exit(0); // exit the forked minion

                  break;

               case F_rf_t_waiting_long:
                  // ask for a status
                  LB_status_req  =(LB_status_req_t *)&LB_buf; // make a pointer to our buffer so we can use the bits right
                  LB_status_req->cmd=LBC_STATUS_REQ;
                  LB_status_req->addr=minion->RF_addr;

                  minion->S.status.flags=F_told_RCC;
                  minion->S.status.timer=20;

                  // now do the crc and send it to the MCP master
                  DDCMSG(D_PACKET,BLUE,"Minion %d:  LB_status_req cmd=%d", minion->mID,LB_status_req->cmd);
                  result= psend_mcp(minion,&LB_status_req);
                  
                  // I'm expecting a response within 3 seconds
                  minion->S.rf_t.flags=F_rf_t_waiting_short;
                  minion->S.rf_t.timer=30; // give it three seconds
                  break;
            }  // end of switch
         }   // else clause - rf_t flag
      } // if clause - rf_t flag not set

      // if there is an expose flag set, we then need to do something
      if (minion->S.exp.flags!=F_exp_ok) {

         if (minion->S.exp.timer>elapsed_tenths){
            minion->S.exp.timer-=elapsed_tenths;    // not timed out.  but decrement out timer
            DDCMSG(D_MSTATE,RED,"MINION %d: not timed out.   S.exp.timer=%d   S.exp.timer=%d",minion->mID,minion->S.exp.timer,minion->S.exp.timer);

         } else {    // timed out,  move to the next state, do what it should.

            switch (minion->S.exp.flags) {

               //   we send the 45 right away, then we wait.
               // if/when we get a response, we are already in whatever state the RF slave sent, and that
               // should supercede this state machine.
               
               case F_exp_expose_A:
                  minion->S.exp.data=45;  // make the current positon in movement
                  minion->S.exp.flags=F_exp_expose_B;     // something has to happen
                  minion->S.exp.timer=100;  // it should happen in this many deciseconds
                  DDCMSG(D_MSTATE,MAGENTA,"exp_A %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n"
                         ,mt->elapsed_time.tv_sec, (int)(mt->elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
                  sendStatus2102(0,mb->header,minion); // forces sending of a 2102

                  LB_status_req  =(LB_status_req_t *)&LB_buf; // make a pointer to our buffer so we can use the bits right
                  LB_status_req->cmd=LBC_STATUS_REQ;
                  LB_status_req->addr=minion->RF_addr;

                  // now send it to the MCP master
                  DDCMSG(D_PACKET,BLUE,"Minion %d: @2 LB_status_req cmd=%d", minion->mID,LB_status_req->cmd);
                  result= psend_mcp(minion,&LB_buf);

                 // I'm expecting a response within 3 seconds
                  minion->S.rf_t.flags=F_rf_t_waiting_short;
                  minion->S.rf_t.timer=30; // give it three seconds
                  
                  break;

#if 0   // I think this all is superflous                  
               case F_exp_expose_B:
                  DDCMSG(D_MSTATE,RED,"\nexp_B    some kind of error - there was not a LBC status response quick enough\n");

                  minion->S.exp.data=90;  // make the current positon in movement
                  minion->S.exp.flags=F_exp_expose_C;     // we have reached the exposed position, now ask for an update
                  minion->S.exp.timer=15; // 1.5 second later
                  DDCMSG(D_MSTATE,MAGENTA,"exp_B %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n"
                         ,mt->elapsed_time.tv_sec, (int)(mt->elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
                  sendStatus2102(0,mb->header,minion); // forces sending of a 2102

                  break;

               case F_exp_expose_C:
                  //                  also build an LB packet  to send
                  LB_status_req  =(LB_status_req_t *)&LB_buf; // make a pointer to our buffer so we can use the bits right
                  LB_status_req->cmd=LBC_STATUS_REQ;
                  LB_status_req->addr=minion->RF_addr;

                  minion->S.status.flags=F_told_RCC;
                  minion->S.status.timer=20;

                  // now send it to the MCP master
                  DDCMSG(D_PACKET,BLUE,"Minion %d: @9 LB_status_req cmd=%d", minion->mID,LB_status_req->cmd);
                  result= psend_mcp(minion,&LB_status_req);

                  // I'm expecting a response within 3 seconds
                  minion->S.rf_t.flags=F_rf_t_waiting_short;
                  minion->S.rf_t.timer=30; // give it three seconds

                  // try again and again until we're sent the conceal command
                  minion->S.exp.flags=F_exp_expose_D;     // we have reached the exposed position, now ask for an update
                  minion->S.exp.timer=0;  // no timeout, we don't actually handle _D, just look at it
                  break;

               case F_exp_expose_D:
                  break;
#endif

//   this might be superfous because it always sends a qconceal
               case F_exp_conceal_A:
                  minion->S.exp.data=45;  // make the current positon in movement
                  minion->S.exp.flags=F_exp_conceal_B;    // something has to happen
                  minion->S.exp.timer=50;  // it should happen in this many deciseconds
                  DDCMSG(D_MSTATE,MAGENTA,"conceal_A %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n"
                         ,mt->elapsed_time.tv_sec, (int)(mt->elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
                  sendStatus2102(0,mb->header,minion); // forces sending of a 2102
                  LB_status_req  =(LB_status_req_t *)&LB_buf; // make a pointer to our buffer so we can use the bits right
                  LB_status_req->cmd=LBC_STATUS_REQ;
                  LB_status_req->addr=minion->RF_addr;

                  // now send it to the MCP master
                  DDCMSG(D_PACKET,BLUE,"Minion %d: @3 LB_status_req cmd=%d", minion->mID,LB_status_req->cmd);
                  result= psend_mcp(minion,&LB_buf);

                  break;

#if 0    // again, this is superflous     
               case F_exp_conceal_B:
                  DDCMSG(D_MSTATE,RED,"\nconceal_B    some kind of error - there was not a LBC status response quick enough\n");
                  
                  minion->S.exp.data=0;   // make the current positon in movement
                  minion->S.exp.flags=F_exp_conceal_C;    // we have reached the concealed position, ask for status update again in one second
                  minion->S.exp.timer=15; // 1.5 second later
                  DDCMSG(D_MSTATE,MAGENTA,"conceal_B %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n"
                         ,mt->elapsed_time.tv_sec, (int)(mt->elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
                  sendStatus2102(0,mb->header,minion); // forces sending of a 2102

                  break;

               case F_exp_conceal_C:
                  //                  also build an LB packet  to send
                  LB_status_req  =(LB_status_req_t *)&LB_buf; // make a pointer to our buffer so we can use the bits right
                  LB_status_req->cmd=LBC_STATUS_REQ;
                  LB_status_req->addr=minion->RF_addr;

                  minion->S.status.flags=F_told_RCC;
                  minion->S.status.timer=20;

                  // now send it to the MCP master
                  DDCMSG(D_PACKET,BLUE,"Minion %d: @4 LB_status_req cmd=%d", minion->mID,LB_status_req->cmd);
                  result= psend_mcp(minion,&LB_buf);
                  // I'm expecting a response within 3 seconds
                  minion->S.rf_t.flags=F_rf_t_waiting_short;
                  minion->S.rf_t.timer=30; // give it three seconds

                  // clear out expose status...I won't try again
                  minion->S.exp.flags=0;
                  minion->S.exp.timer=0;
                  break;
#endif
                  
            }  // end of switch
         }   // else clause - exp flag
      } // if clause - exp flag not set

      // if there is an move  flag set, we then need to pretend we are moving
      if (minion->S.move.flags) {
         LB_status_req_t *L=(LB_status_req_t *)&LB_buf;
         L->cmd=LBC_STATUS_REQ;          // start filling in the packet
         L->addr=minion->RF_addr;
         
         DDCMSG(D_MSTATE,RED,"MINION %d:  move.flags=0x%x move.timer=%i",
                minion->mID,minion->S.move.flags, minion->S.move.timer);

         if (minion->S.move.timer>elapsed_tenths){
            minion->S.move.timer-=elapsed_tenths;  // not timed out.  but decrement out timer

         } else {    // timed out,  move to the next state, do what it should.
            
            switch (minion->S.move.data) {
               case 0:
//                  not moving, don't do anything?

                  break;
               case 1:
               case 2:
// make a pointer to our buffer so we can use the bits right
                  // moving forward 
                  if (fabs(minion->S.speed.data-minion->S.speed.newdata)<.5){
                     // we are close enough to target speed, randomize it alittle
//                     minion->S.speed.data

                  } else {
                     minion->S.speed.data+=(minion->S.speed.newdata-minion->S.speed.data)*.6;
                     // increment our speed

                  }

                  if (minion->S.move.data==1){
                     if (minion->S.position.data<2044) minion->S.position.data+=2;
                  } else {
                     if (minion->S.position.data>2) minion->S.position.data-=2;
                  }

                  DDCMSG(D_MSTATE,MAGENTA,"exp_A %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n"
                         ,mt->elapsed_time.tv_sec, (int)(mt->elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
                  sendStatus2102(0,mb->header,minion); // forces sending of a 2102

                  break;

               case 3:
//                      E-stop


                  break;
            }
         }
         
         // send out a request for actual position
         DDCMSG(D_PACKET,BLUE,"Minion %d: @5 LB_status_req cmd=%d", minion->mID,LB_status_req->cmd);
         result= psend_mcp(minion,&LB_buf);

         // try to calculate how long it will take to go 2 meters.
         minion->S.move.timer=minion->S.speed.data/.5;  // it should happen in this many deciseconds

      } // end of move.flags


      // if there is an event flag set, we then need to do something
      if (minion->S.event.flags) {
         DDCMSG(D_MSTATE,RED,"MINION %d:----- event.flags=0x%x event.timer=%i",minion->mID,minion->S.event.flags, minion->S.event.timer);

         if (minion->S.event.timer>elapsed_tenths){
            minion->S.event.timer-=elapsed_tenths;  // not timed out.  but decrement out timer

         } else {    // timed out,  move to the next state, do what it should.
            switch (minion->S.event.flags) {

               case F_needs_report:
//                  build an LB packet  report_req packet
               {
                  LB_report_req_t *L=(LB_report_req_t *)mb->buf;

                  L->cmd=LBC_REPORT_REQ;
                  L->addr=minion->RF_addr;
                  L->event=minion->S.event.data;


                  DDCMSG(D_MSTATE,RED,"MINION %d:----- building a report request LB packet event.flags=0x%x",minion->mID,minion->S.event.flags);

                    // now send it to the MCP master
                  DDCMSG(D_PACKET,BLUE,"Minion %d:  LB_report_req cmd=%d", minion->mID,L->cmd);
                  result= psend_mcp(minion,L);

                  minion->S.event.flags = 0;
                  minion->S.event.timer = 0;
                        // TODO -- what do we do when we don't receive a report?
                        //minion->S.event.flags=F_waiting_for_report;
                        //minion->S.event.timer = 20;   // lets try 5.0 seconds
               }
               break;


            }
         }
      } //end of if(...flags)


        
   }

        // run through all the timers and get the next time we need to process
#define Set_Timer(T) { if ((T>0)&&(T<minion->S.state_timer)) minion->S.state_timer=T; }
   minion->S.state_timer = 900;    // put in a worst case starting value of 90 seconds
   Set_Timer(minion->S.exp.timer);         // if arg is >0 and <timer, it is the new timer
   Set_Timer(minion->S.event.timer);               // if arg is >0 and <timer, it is the new timer
   Set_Timer(minion->S.rf_t.timer);                // if arg is >0 and <timer, it is the new timer
   //      Set_Timer(minion->S.speed.timer);               // if arg is >0 and <timer, it is the new timer

}
