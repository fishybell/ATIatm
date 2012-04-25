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
   int result,force_stat_req=0;
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

#if 0 /* start of old state timer code */
   if (!minion->S.state_timer)  { // timer reached zero
      //   We should only do the section if the next timer is really up, and
      //   not just if another fd was ready about the same time
      DDCMSG(D_MSTATE,RED,"MINION %d: timer update.   rf_t.flags/timer=0x%x/%i   exp.flags/timer=0x%x/%i   event.flags/timer=0x%x/%i",
             minion->mID,minion->S.rf_t.flags,minion->S.rf_t.timer,minion->S.exp.flags,minion->S.exp.timer,minion->S.event.flags,minion->S.event.timer);


      
      // if there is an rf timer flag set, we then need to do something
      if (minion->S.rf_t.flags) { 

         if (minion->S.rf_t.timer>elapsed_tenths){
            minion->S.rf_t.timer-=elapsed_tenths;   // not timed out.  but decrement out timer
            DDCMSG(D_MSTATE,RED,"MINION %d: not timed out.   S.rf_t.timer=%d   S.rf_t.timer=%d",minion->mID,minion->S.rf_t.timer,minion->S.rf_t.timer);

         } else {    // timed out,  move to the next state, do what it should.

            switch (minion->S.rf_t.flags) {
               case F_rf_t_waiting_short:

#if 1
                  DDCMSG(D_MSTATE,RED,"*&#$*&#$\n*&#$*&#$\nMINION %d: Would time out  and disconnect.   S.rf_t.timer=%d   S.rf_t.timer=%d",minion->mID,minion->S.rf_t.timer,minion->S.rf_t.timer);
#else
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
#endif
                  break;

               case F_rf_t_waiting_long:
                  // ask for a status

                  minion->S.status.flags=F_told_RCC;
                  minion->S.status.timer=50;

                  force_stat_req=1;     // so we don't send repeated status_req

                  // I'm expecting a response within 3 seconds
                  minion->S.rf_t.flags=F_rf_t_waiting_short;
                  minion->S.rf_t.timer=50; // give it three seconds
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

                  force_stat_req=1;
                  
                 // I'm expecting a response within 3 seconds
                  minion->S.rf_t.flags=F_rf_t_waiting_short;
                  minion->S.rf_t.timer=51; // give it three seconds
                  
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

                  force_stat_req=1;
                  
                  minion->S.status.flags=F_told_RCC;
                  minion->S.status.timer=20;

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

                  force_stat_req=1;
                  // I'm expecting a response within 3 seconds
                  minion->S.rf_t.flags=F_rf_t_waiting_short;
                  minion->S.rf_t.timer=52; // give it three seconds

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

                  minion->S.status.flags=F_told_RCC;
                  minion->S.status.timer=20;

                  force_stat_req=1;
                  
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
         
         DDCMSG(D_MSTATE,GREEN,"**********\n***MINION %d:  move.flags=0x%x move.timer=%i move.data=%d",
                minion->mID,minion->S.move.flags, minion->S.move.timer, minion->S.move.data);

         if (minion->S.move.timer>elapsed_tenths){
            minion->S.move.timer-=elapsed_tenths;  // not timed out.  but decrement out timer
            if (minion->S.move.timer<10) minion->S.move.timer=10;
         } else {    // timed out,  move to the next state, do what it should.
            
            switch (minion->S.move.data) {
               case 0:
//                  not moving, don't do anything?
                  minion->S.move.flags=0;       // clear move flags
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
                  
                  DDCMSG(D_MSTATE,GREEN,"**********\n***MINION %d:  need to push up a status2102   move.flags=0x%x move.timer=%i move.data=%d",
                         minion->mID,minion->S.move.flags, minion->S.move.timer, minion->S.move.data);

//                  DDCMSG(D_MSTATE,MAGENTA,"exp_A %06ld.%1d   elapsed_tenths=%ld 2102 simulated %d \n" ,mt->elapsed_time.tv_sec, (int)(mt->elapsed_time.tv_sec/100000000L),elapsed_tenths,minion->S.exp.data);
                  sendStatus2102(0,mb->header,minion); // forces sending of a 2102

                  break;

               case 3:
//                      E-stop

                  minion->S.move.flags=0;
                  break;
            }
         }
         
         DDCMSG(D_MSTATE,GREEN,"**********\n---MINION %d:  send a lbc status_req   move.flags=0x%x move.timer=%i move.data=%d",
                minion->mID,minion->S.move.flags, minion->S.move.timer, minion->S.move.data);

         force_stat_req=1;         // send out a request for actual position
         
         // try to calculate how long it will take to go 2 meters.
         minion->S.move.timer=minion->S.speed.data/.5;  // it should happen in this many deciseconds
         DDCMSG(D_PACKET,GREEN,"Minion %d:  \n\n**********\n    setting move timer=%d", minion->mID,minion->S.move.timer);

      } // end of move.flags


      // if there is an event flag set, we then need to do something
      if (minion->S.event.flags) {
         DDCMSG(D_MSTATE,RED,"MINION %d:----- event.flags=0x%x event.timer=%i",minion->mID,minion->S.event.flags, minion->S.event.timer);

         if (minion->S.event.timer>elapsed_tenths){
            minion->S.event.timer-=elapsed_tenths;  // not timed out.  but decrement out timer

         } else {    // timed out,  move to the next state, do what it should.
            switch (minion->S.event.flags) {

               case F_needs_report:

                  force_stat_req=1;
//                  minion->S.event.flags = 0;
//                  minion->S.event.timer = 0;
                  // TODO -- what do we do when we don't receive a report?
                  minion->S.event.flags=F_waiting_for_report;
                  minion->S.event.timer = 100;   // lets try 5.0 seconds

                  break;

               case F_waiting_for_report:

                  force_stat_req=1;
//                  minion->S.event.flags = 0;
//                  minion->S.event.timer = 0;
                  // TODO -- what do we do when we don't receive a report?
                  minion->S.event.flags=F_waiting_for_report;
                  minion->S.event.timer = 150;   // lets try 5.0 seconds

                  break;

            }
         }
      } //end of if(...flags)


      if (0/*force_stat_req*/){         //  don't send status requests at all, for now 
         // send out a request for actual position
         LB_status_req_t *L=(LB_status_req_t *)&LB_buf;
         L->cmd=LBC_STATUS_REQ;          // start filling in the packet
         L->addr=minion->RF_addr;
         DDCMSG(D_MSTATE,GREEN,"Minion %d:   build and send L LBC_STATUS_REQ", minion->mID);
         result= psend_mcp(minion,&LB_buf);
         force_stat_req=0;
         minion->S.event.flags=F_waiting_for_report;
         minion->S.event.timer = 55;   // lets try 5.0 seconds         
      }
   }
#endif /* end of old state timer code */

   
   // new state timer code
   if (!minion->S.state_timer)  { // timer reached zero
      //   We should only do the section if the next timer is really up, and
      //   not just if another fd was ready about the same time
      DDCMSG(D_MSTATE,RED,"MINION %d: state timers update:\nfast flags/timer=%i/%i\nslow flags/timer=%i/%i\nexpose flags/timer=%i/%i\nconceal flags/timer=%i/%i\nmove flags/timer=%i/%i\nevent flags/timer=%i/%i", minion->mID,
               minion->S.rf_t.fast_flags, minion->S.rf_t.fast_timer,
               minion->S.rf_t.slow_flags, minion->S.rf_t.slow_timer,
               minion->S.exp.exp_flags, minion->S.exp.exp_timer,
               minion->S.exp.con_flags, minion->S.exp.con_timer,
               minion->S.move.flags, minion->S.move.timer,
               minion->S.event.flags, minion->S.event.timer);

      // macro to disconnect minion
      #define DISCONNECT { \
         /* break connection to FASIT server */ \
         close(minion->rcc_sock); /* close FASIT */ \
         DCMSG(BLACK,"\n\n-----------------------------------\nDisconnected minion %i:%i:%i\n-----------------------------------\n\n", \
               minion->mID, minion->rcc_sock, minion->mcp_sock); \
         minion->rcc_sock = -1; \
         minion->status = S_closed; \
         LB_buf.cmd = LBC_ILLEGAL; /* send an illegal packet, which makes mcp forget me */ \
         /* now send it to the MCP master */ \
         DDCMSG(D_PACKET,BLUE,"Minion %d:  LBC_ILLEGAL cmd=%d", minion->mID,LB_buf.cmd); \
         result= psend_mcp(minion,&LB_buf); \
         close(minion->mcp_sock); /* close mcp */ \
         exit(0); /* exit the forked minion */ \
      }

      // macro to send a status request
      #define SEND_STATUS_REQUEST { \
         /* send out a request for actual position */ \
         LB_status_req_t *L=(LB_status_req_t *)&LB_buf; \
         L->cmd=LBC_STATUS_REQ;          /* start filling in the packet */ \
         L->addr=minion->RF_addr; \
         DDCMSG(D_MSTATE,GREEN,"Minion %d:   build and send L LBC_STATUS_REQ", minion->mID); \
         result= psend_mcp(minion,&LB_buf); \
      }

      // check each timer, running its state code if it is needed, moving its timer otherwise
      #define CHECK_TIMER(S, T, F, CODE) { \
         if (!S.F) { \
            if (S.T>elapsed_tenths) { \
               S.T-=elapsed_tenths;   /* not timed out.  but decrement out timer */ \
            } \
         } else { \
            CODE \
         } \
      }

      CHECK_TIMER (minion->S.rf_t, fast_timer, fast_flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking fast timer flags: %i", minion->S.rf_t.fast_flags);
         switch (minion->S.rf_t.fast_flags) {
            case F_fast_start: {
               // might get here from a miss or just because it's our time again, assume miss
               // incriment miss number and check against max
               if (++minion->S.rf_t.fast_missed > FAST_TIME_MAX_MISS) {
                  DISCONNECT;
               } else {
                  SEND_STATUS_REQUEST;
                  setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_TIME, F_fast_start); // same state over and over
               }
            } break;
            case F_fast_once: {
               // should only get here as start of message, so just add one to the miss counter as we normally assume a miss
               ++minion->S.rf_t.fast_missed;
               SEND_STATUS_REQUEST;
               setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_TIME, F_fast_end); // move to end
            } break;
            case F_fast_end: {
               // might get here from a miss or just because it's our time again, assume miss
               // incriment miss number and check against max
               if (++minion->S.rf_t.fast_missed > FAST_TIME_MAX_MISS) {
                  DISCONNECT;
               } else if (minion->S.rf_t.fast_missed > 1) {
                  // missed, keep trying fast
                  SEND_STATUS_REQUEST;
                  setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_TIME, F_fast_end); // move to end
               } else {
                  // didn't miss, so go slow again
                  if (minion->S.rf_t.old_slow_flags == 0) {
                     // didn't save previous slow state, so make a new one
                     setTimerTo(minion->S.rf_t, slow_timer, slow_flags, SLOW_TIME, F_slow_start);
                  } else {
                     // saved previous slow state, resume it
                     resumeTimer(minion->S.rf_t, slow_timer, slow_flags);
                  }
                  // ... either way, stop the fast timer
                  stopTimer(minion->S.rf_t, fast_timer, fast_flags);
               }
            } break;
         }
      ); // end of CHECK_TIMER for fast state timer

      CHECK_TIMER (minion->S.rf_t, slow_timer, slow_flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking fast timer flags: %i", minion->S.rf_t.slow_flags);
         switch (minion->S.rf_t.fast_flags) {
            case F_slow_start: {
               SEND_STATUS_REQUEST;
               setTimerTo(minion->S.rf_t, slow_timer, slow_flags, SLOW_RESPONSE_TIME, F_slow_continue); // will only get to F_slow_continue if we don't reset (code in minion.c)
            } break;
            case F_slow_continue: {
               // shouldn't get here unless we missed
               // incriment miss number and check against max
               if (++minion->S.rf_t.slow_missed > SLOW_TIME_MAX_MISS) {
                  DISCONNECT;
               } else {
                  SEND_STATUS_REQUEST;
                  setTimerTo(minion->S.rf_t, slow_timer, slow_flags, SLOW_RESPONSE_TIME, F_slow_continue);
               }
            } break;
         }
      ); // end of CHECK_TIMER for slow state timer

      CHECK_TIMER (minion->S.exp, exp_timer, exp_flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking expose timer flags: %i", minion->S.exp.exp_flags);
         switch (minion->S.exp.exp_flags) {
            case F_exp_start_transition: {
               minion->S.exp.data=45;  // make the current positon in movement
               sendStatus2102(0,mb->header,minion); // forces sending of a 2102
               setTimerTo(minion->S.exp, exp_timer, exp_flags, TRANSITION_TIME, F_exp_end_transition);
            } break;
            case F_exp_end_transition: {
               // nothing to do here?
               stopTimer(minion->S.exp, exp_timer, exp_flags);
            } break;
         }
      ); // end of CHECK_TIMER for expose state timer

      CHECK_TIMER (minion->S.exp, con_timer, con_flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking conceal timer flags: %i", minion->S.exp.con_flags);
         switch (minion->S.exp.con_flags) {
            case F_con_start_transition: {
               minion->S.exp.data=45;  // make the current positon in movement
               sendStatus2102(0,mb->header,minion); // forces sending of a 2102
               setTimerTo(minion->S.exp, con_timer, con_flags, TRANSITION_TIME, F_con_end_transition);
            } break;
            case F_con_end_transition: {
               // nothing to do here?
               stopTimer(minion->S.exp, con_timer, con_flags);
            } break;
         }
      ); // end of CHECK_TIMER for conceal state timer

      CHECK_TIMER (minion->S.move, timer, flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking move timer flags: %i", minion->S.move.flags);
         switch (minion->S.move.flags) {
            case F_move_start_movement: {
               // TODO -- something here?
               stopTimer(minion->S.move, timer, flags);
            } break;
            case F_move_end_movement: {
               // TODO -- something here?
               stopTimer(minion->S.move, timer, flags);
            } break;
         }
      ); // end of CHECK_TIMER for move state timer

      CHECK_TIMER (minion->S.event, timer, flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking event timer flags: %i", minion->S.event.flags);
         switch (minion->S.event.flags) {
            case F_event_start: {
               // shouldn't get here unless we missed
               // incriment miss number and check against max
               if (++minion->S.event.missed > EVENT_MAX_MISS) {
                  setTimerTo(minion->S.event, timer, flags, EVENT_RESPONSE_TIME, F_event_end); // disconnect later to give a slight chance to the response coming in
               } else {
                  // send another request
                  LB_report_req_t L;

                  L.cmd=LBC_REPORT_REQ;
                  L.addr=minion->RF_addr;
                  L.event=minion->S.exp.last_event; // use saved event number

                  DDCMSG(D_PACKET,BLUE,"Minion %d:  LB_report_req cmd=%d", minion->mID,L.cmd);
                  psend_mcp(minion,&L);
                  setTimerTo(minion->S.event, timer, flags, EVENT_RESPONSE_TIME, F_event_start); // try again
               }
            } break;
            case F_event_end: {
               // we missed too many times
               DISCONNECT;
            } break;
         }
      ); // end of CHECK_TIMER for event state timer

   } // ...end of timer reached zero
   

        // run through all the timers and get the next time we need to process
#define Set_Timer(T) { if ((T>0)&&(T<minion->S.state_timer)) minion->S.state_timer=T; }
   minion->S.state_timer = 900;    // put in a worst case starting value of 90 seconds
   // if arg is >0 and <timer, it is the new timer
   Set_Timer(minion->S.exp.exp_timer); // timer for expose state
   Set_Timer(minion->S.exp.con_timer); // timer for conceal state
   Set_Timer(minion->S.event.timer); // timer for event response
   Set_Timer(minion->S.rf_t.fast_timer); // timer for fast status check
   Set_Timer(minion->S.rf_t.slow_timer); // timer for slow status check
   Set_Timer(minion->S.move.timer); // timer for movement state

   DDCMSG(D_MSTATE,GRAY,"\nMinion %d:   Set_Timer=%d   exp.exp=%d exp.con=%d event=%d rf_t.fast=%d ft_t.slow=%d move=%d",
          minion->mID,minion->S.state_timer, minion->S.exp.exp_timer, minion->S.exp.con_timer, minion->S.event.timer, minion->S.rf_t.fast_timer, minion->S.rf_t.slow_timer, minion->S.move.timer);

}
