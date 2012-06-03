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

#define __PROGRAM__ "MINION "

extern int verbose;
extern struct sockaddr_in fasit_addr;
extern int close_nicely;

int psend_mcp(thread_data_t *minion,void *Lc);

void minion_state(thread_data_t *minion, minion_time_t *mt, minion_bufs_t *mb) {

   long elapsed_tenths;
   int result;
   int i;
   report_memory_t *this_r; // a pointer to use with the report memory chain
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
   DDCMSG(D_TIME,CYAN,"MINION %i: Begin timer updates at %3i.%03i timestamp, delta=%3i.%03i"
          ,minion->mID, DEBUG_TS(mt->elapsed_time), DEBUG_TS(mt->delta_time));

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

   
   // new state timer code
   if (!minion->S.state_timer)  { // timer reached zero
      //   We should only do the section if the next timer is really up, and
      //   not just if another fd was ready about the same time
      DDCMSG(D_MSTATE,RED,"MINION %i: state timers update:\nfast flags/timer=%i/%i\nslow flags/timer=%i/%i\nexpose flags/timer=%i/%i\nconceal flags/timer=%i/%i\nmove flags/timer=%i/%i", minion->mID,
               minion->S.rf_t.fast_flags, minion->S.rf_t.fast_timer,
               minion->S.rf_t.slow_flags, minion->S.rf_t.slow_timer,
               minion->S.exp.exp_flags, minion->S.exp.exp_timer,
               minion->S.exp.con_flags, minion->S.exp.con_timer,
               minion->S.move.flags, minion->S.move.timer);

      // macro to send a status request
      #define SEND_STATUS_REQUEST { \
         /* send out a request for actual position */ \
         LB_status_req_t *L=(LB_status_req_t *)&LB_buf; \
         L->cmd=LBC_STATUS_REQ;          /* start filling in the packet */ \
         L->addr=minion->RF_addr; \
         DDCMSG(D_MSTATE,GREEN,"Minion %i:   build and send L LBC_STATUS_REQ", minion->mID); \
         result= psend_mcp(minion,&LB_buf); \
      }

      // check each timer, running its state code if it is needed, moving its timer otherwise
      #define CHECK_TIMER(S, T, F, CODE) { \
         if (S.F) { \
            DDCMSG(D_MSTATE, GREEN, "Checking timer " #F ": %i/%i @ %s.%i", S.T, S.F, __FILE__, __LINE__); \
            if (S.T>elapsed_tenths) { \
               S.T-=elapsed_tenths;   /* not timed out yet.  but decrement out timer */ \
               DDCMSG(D_MSTATE, MAGENTA, "Altered timer " #F ": %i/%i @ %s.%i", S.T, S.F, __FILE__, __LINE__); \
            } else { \
            DDCMSG(D_MSTATE, YELLOW, "Running timer " #F ": %i/%i @ %s.%i", S.T, S.F, __FILE__, __LINE__); \
               CODE \
            } \
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
                  setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_TIME, F_fast_start); // same state over and over (short interval)
               }
            } break;
            case F_fast_medium: {
               // might get here from a miss or just because it's our time again, assume miss
               // incriment miss number and check against max
               if (++minion->S.rf_t.fast_missed > FAST_TIME_MAX_MISS) {
                  DISCONNECT;
               } else {
                  SEND_STATUS_REQUEST;
                  setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_RESPONSE_TIME, F_fast_medium); // same state over and over (medium interval)
               }
            } break;
            case F_fast_once: {
               // should only get here as start of message, so just add one to the miss counter as we normally assume a miss
               ++minion->S.rf_t.fast_missed;
               SEND_STATUS_REQUEST;
               setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_RESPONSE_TIME, F_fast_end); // move to end (medium interval)
            } break;
            case F_fast_end: {
               // might get here from a miss or just because it's our time again, assume miss
               // incriment miss number and check against max
               if (++minion->S.rf_t.fast_missed > FAST_TIME_MAX_MISS) {
                  DISCONNECT;
               } else if (minion->S.rf_t.fast_missed > 1) {
                  // missed, keep trying fast
                  SEND_STATUS_REQUEST;
                  setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_RESPONSE_TIME, F_fast_end); // move to end (medium interval)
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
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in fast flags: %i", minion->S.rf_t.fast_flags);
            }
         }
      ); // end of CHECK_TIMER for fast state timer

      CHECK_TIMER (minion->S.rf_t, slow_timer, slow_flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking slow timer flags: %i", minion->S.rf_t.slow_flags);
         switch (minion->S.rf_t.slow_flags) {
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
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in slow flags: %i", minion->S.rf_t.slow_flags);
            }
         }
      ); // end of CHECK_TIMER for slow state timer

      CHECK_TIMER (minion->S.exp, exp_timer, exp_flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking expose timer flags: %i", minion->S.exp.exp_flags);
         switch (minion->S.exp.exp_flags) {
            case F_exp_start_transition: {
               int wait = 0;
               switch (minion->S.dev_type) {
                  case Type_BES:
                  case Type_SES:
                  default:
                     wait = TRANSMISSION_TIME; // time it takes to send radio message (about)
                     break;
                  case Type_MIT:
                  case Type_SIT:
                     wait = SIT_TRANSITION_TIME; // time it takes an infantry lifter to lift
                     break;
                  case Type_MAT:
                  case Type_HSAT:
                     wait = HSAT_TRANSITION_TIME; // time it takes a heavy armor lifter to lift
                     break;
                  case Type_SAT:
                     wait = SAT_TRANSITION_TIME; // time it takes a light armor lifter to lift
                     break;
               }
               // sendStatus2102(0,mb->header,minion,mt); // forces sending of a 2102 -- removed in favor of change_states()
               //DDCMSG(D_POINTER|D_NEW, YELLOW, "calling change_states(%s) @ %s:%i", step_words[TS_con_to_exp], __FILE__, __LINE__);
               change_states(minion, mt, TS_con_to_exp, 0);
               setTimerTo(minion->S.exp, exp_timer, exp_flags, wait, F_exp_end_transition);
            } break;
            case F_exp_end_transition: {
               change_states(minion, mt, TS_exposed, 0);
               stopTimer(minion->S.exp, exp_timer, exp_flags);
            } break;
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in expose flags: %i", minion->S.exp.exp_flags);
            }
         }
      ); // end of CHECK_TIMER for expose state timer

      CHECK_TIMER (minion->S.exp, con_timer, con_flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking conceal timer flags: %i", minion->S.exp.con_flags);
         switch (minion->S.exp.con_flags) {
            case F_con_start_transition: {
               int wait = 0;
               switch (minion->S.dev_type) {
                  case Type_BES:
                  case Type_SES:
                  default:
                     wait = TRANSMISSION_TIME; // time it takes to send radio message (about)
                     break;
                  case Type_MIT:
                  case Type_SIT:
                     wait = SIT_TRANSITION_TIME; // time it takes an infantry lifter to lift
                     break;
                  case Type_MAT:
                  case Type_HSAT:
                     wait = HSAT_TRANSITION_TIME; // time it takes a heavy armor lifter to lift
                     break;
                  case Type_SAT:
                     wait = SAT_TRANSITION_TIME; // time it takes a light armor lifter to lift
                     break;
               }
               // sendStatus2102(0,mb->header,minion,mt); // forces sending of a 2102 -- removed in favor of change_states()
               //DDCMSG(D_POINTER|D_NEW, YELLOW, "calling change_states(%s) @ %s:%i", step_words[TS_exp_to_con], __FILE__, __LINE__);
               change_states(minion, mt, TS_exp_to_con, 0);
               setTimerTo(minion->S.exp, con_timer, con_flags, wait, F_con_end_transition);
            } break;
            case F_con_end_transition: {
               change_states(minion, mt, TS_concealed, 0);
               stopTimer(minion->S.exp, con_timer, con_flags);
            } break;
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in conceal flags: %i", minion->S.exp.con_flags);
            }
         }
      ); // end of CHECK_TIMER for conceal state timer

#if 0 /* something in here is broken */
      CHECK_TIMER (minion->S.move, timer, flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking move timer flags: %i", minion->S.move.flags);
         switch (minion->S.move.flags) {
            case F_move_start_movement: {
               // we haven't started moving yet, so let's prepare variables
               minion->S.speed.lastdata = -1.0; // force cache to be dirty
               minion->S.speed.data = 0.0; // we're not moving
               sendStatus2102(0, NULL,minion,mt); // tell FASIT server

               DDCMSG(D_POINTER, GRAY, "Should start fake movement for minion %i", minion->mID);
               switch (minion->S.dev_type) {
                  default:
                  case Type_MIT:
                     setTimerTo(S.move, timer, flags, MIT_MOVE_START_TIME, F_move_continue_movement);
                     break;
                  case Type_MAT:
                     setTimerTo(S.move, timer, flags, MAT_MOVE_START_TIME, F_move_continue_movement);
                     break;
               }
               timestamp(&mt->elapsed_time,&mt->istart_time,&mt->delta_time);      
               minion->S.last_move_time = ts2ms(&mt->elapsed_time);
               minion->S.speed.last_index = 0;
            } break;
            case F_move_continue_movement: {
               DDCMSG(D_POINTER, GRAY, "Started fake movement for minion %i", minion->mID);
               // first and foremost, find out the difference in time
               int nt, dt; 
               timestamp(&mt->elapsed_time,&mt->istart_time,&mt->delta_time);      
               nt = ts2ms(&mt->elapsed_time);
               dt = minion->S.last_move_time - nt; // diff = last - now

               // distance = last distance + last speed * time + acceleration * time * time

               // speed = last speed * acceleration * time
               

               // tell the good news
               sendStatus2102(0, NULL,minion,mt); // tell FASIT server

               // and come back later
               if (minion->S.speed.data <= 0.0) {
                  switch (minion->S.dev_type) {
                     default:
                     case Type_MIT:
                        setTimerTo(S.move, timer, flags, MIT_MOVE_START_TIME, F_move_end_movement);
                        break;
                     case Type_MAT:
                        setTimerTo(S.move, timer, flags, MAT_MOVE_START_TIME, F_move_end_movement);
                        break;
                  }
               } else {
                  switch (minion->S.dev_type) {
                     default:
                     case Type_MIT:
                        setTimerTo(S.move, timer, flags, MIT_MOVE_START_TIME, F_move_continue_movement);
                        break;
                     case Type_MAT:
                        setTimerTo(S.move, timer, flags, MAT_MOVE_START_TIME, F_move_continue_movement);
                        break;
                  }
               }
            } break;
            case F_move_end_movement: {
               // Clean up movement data
               minion->S.speed.towards_index = 0;
               minion->S.speed.last_index = 0;
               minion->S.speed.data = 0.0;
               minion->S.speed.newdata = 0.0;
               sendStatus2102(0, NULL,minion,mt); // tell FASIT server
               stopTimer(minion->S.move, timer, flags); // don't be coming back now
            } break;
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in move flags: %i", minion->S.move.flags);
            }
         }
      ); // end of CHECK_TIMER for move state timer
#endif

      this_r = minion->S.report_chain;
      while (this_r != NULL) {
         // check to see if this one is looking to send an ack back now
         CHECK_TIMER (this_r->s, timer, flags, 
            DDCMSG(D_MSTATE, BLACK, "Now checking this_r(%p) timer flags: %i", this_r, this_r->s.flags);
            switch (this_r->s.flags) {
               case F_event_ack: {
                  // we received a report, send an ack
                  LB_report_ack_t L;

                  L.cmd=LBC_REPORT_ACK;
                  L.addr=minion->RF_addr;
                  L.report = this_r->report;
                  L.event = this_r->event;
                  L.hits = this_r->hits;

                  DDCMSG(D_PACKET,BLUE,"Minion %i:  LB_report_req cmd=%i", minion->mID,L.cmd);
                  psend_mcp(minion,&L);
                  stopTimer(this_r->s, timer, flags); // one ack per received report
               } break;
               default: {
                  DDCMSG(D_MSTATE, RED, "Default reached in this_r(%p) flags: %i", this_r, this_r->s.flags);
               }
            }
         ); // end of CHECK_TIMER for this_r state timer
         this_r = this_r->next;
      } // end of while loop for report memory chain

   } // ...end of timer reached zero
   

        // run through all the timers and get the next time we need to process
#define Set_Timer(T) { if ((T>0)&&(T<minion->S.state_timer)) minion->S.state_timer=T; }
   minion->S.state_timer = 900;    // put in a worst case starting value of 90 seconds
   // if arg is >0 and <timer, it is the new timer
   Set_Timer(minion->S.exp.exp_timer); // timer for expose state
   Set_Timer(minion->S.exp.con_timer); // timer for conceal state
   this_r = minion->S.report_chain;
   while (this_r != NULL) {
      Set_Timer(this_r->s.timer); // timer for event response
      this_r = this_r->next;
   }
   Set_Timer(minion->S.rf_t.fast_timer); // timer for fast status check
   Set_Timer(minion->S.rf_t.slow_timer); // timer for slow status check
   Set_Timer(minion->S.move.timer); // timer for movement state

   DDCMSG(D_MSTATE,GRAY,"Minion %i:   Set_Timer=%i   exp.exp=%i exp.con=%i rf_t.fast=%i rt_t.slow=%i move=%i (or event timer)",
          minion->mID,minion->S.state_timer, minion->S.exp.exp_timer, minion->S.exp.con_timer, minion->S.rf_t.fast_timer, minion->S.rf_t.slow_timer, minion->S.move.timer);

}
