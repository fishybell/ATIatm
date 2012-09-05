#include "math.h"
#include "mcp.h"
#include "rf.h"
#include "fasit_c.h"
#include "../../fasit/faults.h"

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

void minion_state(thread_data_t *minion, minion_bufs_t *mb) {

   static minion_time_t mt_t;
   static minion_time_t *mt=NULL;
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

   if (mt == NULL) {
      // initialize state machine times
      clock_gettime(CLOCK_MONOTONIC,&mt_t.istart_time);
      mt=&mt_t;
   }

   timestamp(mt);
   DDCMSG(D_MSTATE,CYAN,"Minion one=%2x: Begin timer updates at %3i.%03i timestamp, delta=%3i.%03i"
          ,minion->devid, DEBUG_TS(mt->elapsed_time), DEBUG_TS(mt->delta_time));

   elapsed_tenths = mt->delta_time.tv_sec*10+(mt->delta_time.tv_nsec/100000000L);

// --- this whole section was/is unused, we set mt.timeout via minion->S.state_timer in minion.c (~2822)
//   if (elapsed_tenths==0){
//      // set the next timeout
//      mt->timeout.tv_sec=0;
//      mt->timeout.tv_usec=(100000000L-mt->elapsed_time.tv_nsec)*1000; // remainder of 1 decisecond in microseconds
//
//   } else {
//      // set the next timeout
//      mt->timeout.tv_sec=0;
//      mt->timeout.tv_usec=100000; // 1 tenth later
//   }

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
   //}

      // check each timer, running its state code if it is needed, moving its timer otherwise
      #define CHECK_TIMER(S, T, F, CODE) { \
         if (S.F) { \
            DDCMSG(D_MSTATE, GREEN, "Checking timer " #F ": %i/%i @ %s.%i against %i", S.T, S.F, __FILE__, __LINE__, elapsed_tenths); \
            if (S.T>elapsed_tenths) { \
               S.T-=elapsed_tenths;   /* not timed out yet.  but decrement out timer */ \
               DDCMSG(D_MSTATE, MAGENTA, "Altered timer " #F ": %i/%i @ %s.%i against %i", S.T, S.F, __FILE__, __LINE__, elapsed_tenths); \
            } else { \
               DDCMSG(D_MSTATE, YELLOW, "Running timer " #F ": %i/%i @ %s.%i against %i", S.T, S.F, __FILE__, __LINE__, elapsed_tenths); \
               CODE \
            } \
         } \
      }

      CHECK_TIMER (minion->S.resp, timer, flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking reponse timer flags: %i", minion->S.resp.flags);
         switch (minion->S.resp.flags) {
            case F_resp_handle: {
               Handle_Status_Resp(minion, mt); // if everything went well, no 2102 will be created
               stopTimer(minion->S.resp, timer, flags);
            } break;
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in response flags: %i", minion->S.resp.flags);
               stopTimer(minion->S.resp, timer, flags);
            }
         }
      ); // end of CHECK_TIMER for response state timer

#if 0 /* old fast/slow timer code */
      CHECK_TIMER (minion->S.rf_t, fast_timer, fast_flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking fast timer flags: %i", minion->S.rf_t.fast_flags);
         switch (minion->S.rf_t.fast_flags) {
            case F_fast_start: {
               // might get here from a miss or just because it's our time again, assume miss
               // incriment miss number and check against max
               if (++minion->S.rf_t.fast_missed > FAST_TIME_MAX_MISS) {
                  DISCONNECT;
               } else {
                  SEND_STATUS_REQUEST(&LB_buf);
                  setTimerTo(minion->S.rf_t, fast_timer, fast_flags, fast_time, F_fast_start); // same state over and over (short interval)
               }
            } break;
            case F_fast_medium: {
               // might get here from a miss or just because it's our time again, assume miss
               // incriment miss number and check against max
               if (++minion->S.rf_t.fast_missed > FAST_TIME_MAX_MISS) {
                  DISCONNECT;
               } else {
                  SEND_STATUS_REQUEST(&LB_buf);
                  setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_RESPONSE_TIME, F_fast_medium); // same state over and over (medium interval)
               }
            } break;
            case F_fast_once: {
               // should only get here as start of message, so just add one to the miss counter as we normally assume a miss
               ++minion->S.rf_t.fast_missed;
               SEND_STATUS_REQUEST(&LB_buf);
               setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_RESPONSE_TIME, F_fast_end); // move to end (medium interval)
            } break;
            case F_fast_end: {
               // might get here from a miss or just because it's our time again, assume miss
               // incriment miss number and check against max
               if (++minion->S.rf_t.fast_missed > FAST_TIME_MAX_MISS) {
                  DISCONNECT;
               } else if (minion->S.rf_t.fast_missed > 1) {
                  // missed, keep trying fast
                  SEND_STATUS_REQUEST(&LB_buf);
                  setTimerTo(minion->S.rf_t, fast_timer, fast_flags, FAST_RESPONSE_TIME, F_fast_end); // move to end (medium interval)
               } else {
                  // didn't miss, so go slow again
                  if (minion->S.rf_t.old_slow_flags == 0) {
                     // didn't save previous slow state, so make a new one
                     setTimerTo(minion->S.rf_t, slow_timer, slow_flags, slow_time, F_slow_start);
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
               SEND_STATUS_REQUEST(&LB_buf);
               setTimerTo(minion->S.rf_t, slow_timer, slow_flags, SLOW_RESPONSE_TIME, F_slow_continue); // will only get to F_slow_continue if we don't reset (code in minion.c)
            } break;
            case F_slow_continue: {
               // shouldn't get here unless we missed
               // incriment miss number and check against max
               if (++minion->S.rf_t.slow_missed > SLOW_TIME_MAX_MISS) {
                  DISCONNECT;
               } else {
                  SEND_STATUS_REQUEST(&LB_buf);
                  setTimerTo(minion->S.rf_t, slow_timer, slow_flags, SLOW_RESPONSE_TIME, F_slow_continue);
               }
            } break;
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in slow flags: %i", minion->S.rf_t.slow_flags);
            }
         }
      ); // end of CHECK_TIMER for slow state timer
#endif /* end of old fast/slow timer code */

      CHECK_TIMER (minion->S.rf_t, fast_timer, fast_flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking fast timer flags: %i", minion->S.rf_t.fast_flags);
         switch (minion->S.rf_t.fast_flags) {
            case F_fast_start: {
               // each time we send a status request assume we've missed
               if (++minion->S.rf_t.fast_missed > FAST_TIME_MAX_MISS) {
                  // we've missed too many times
                  DISCONNECT;
               } else {
                  SEND_STATUS_REQUEST(&LB_buf);
                  setTimerTo(minion->S.rf_t, fast_timer, fast_flags, fast_time, F_fast_start); // same state over and over (short interval)
               }
            } break;
            case F_fast_wait: {
               if (minion->S.rf_t.slow_flags == F_slow_none) { // we haven't switched timers
                  // we waited too long to cancel the fast timer, resume it
                  setTimerTo(minion->S.rf_t, fast_timer, fast_flags, fast_time, F_fast_start);
               }
            } break;
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in fast flags: %i", minion->S.rf_t.fast_flags);
               stopTimer(minion->S.rf_t, fast_timer, fast_flags);
            }
         }
      ); // end of CHECK_TIMER for fast state timer

      CHECK_TIMER (minion->S.rf_t, slow_timer, slow_flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking slow timer flags: %i", minion->S.rf_t.slow_flags);
         switch (minion->S.rf_t.slow_flags) {
            case F_slow_start: {
               // each time we send a status request assume we've missed
               if (++minion->S.rf_t.slow_missed > SLOW_TIME_MAX_MISS) {
                  // we've missed too many times
                  DISCONNECT;
               } else {
                  SEND_STATUS_REQUEST(&LB_buf);
                  setTimerTo(minion->S.rf_t, slow_timer, slow_flags, slow_time, F_slow_start); // same state over and over (short interval)
               }
            } break;
            case F_slow_wait: {
               if (minion->S.rf_t.fast_flags == F_fast_none) { // we haven't switched timers
                  // we waited too long to cancel the slow timer, resume it
                  setTimerTo(minion->S.rf_t, slow_timer, slow_flags, slow_time, F_slow_start);
               }
            } break;
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in slow flags: %i", minion->S.rf_t.slow_flags);
               stopTimer(minion->S.rf_t, slow_timer, slow_flags);
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
               SEND_STATUS_REQUEST(&LB_buf);
            } break;
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in expose flags: %i", minion->S.exp.exp_flags);
               stopTimer(minion->S.exp, exp_timer, exp_flags);
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
               SEND_STATUS_REQUEST(&LB_buf);
            } break;
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in conceal flags: %i", minion->S.exp.con_flags);
               stopTimer(minion->S.exp, con_timer, con_flags);
            }
         }
      ); // end of CHECK_TIMER for conceal state timer

      CHECK_TIMER (minion->S.move, timer, flags, 
         DDCMSG(D_MSTATE, BLACK, "Now checking move timer flags: %i", minion->S.move.flags);
         switch (minion->S.move.flags) {
            case F_move_start_movement: {
               // we haven't started moving yet, so let's prepare variables
               minion->S.speed.lastdata = -1.0; // force cache to be dirty
               minion->S.speed.data = 0.0; // we're not moving
               minion->S.move.data = minion->S.move.newdata; // but we're going to move a direction
               sendStatus2102(0, NULL,minion,mt); // tell FASIT server

               //DDCMSG(D_POINTER, GRAY, "Should start fake movement for minion %i", minion->mID);
               switch (minion->S.dev_type) {
                  default:
                  case Type_MIT:
                     setTimerTo(minion->S.move, timer, flags, MIT_MOVE_START_TIME, F_move_continue_movement);
                     break;
                  case Type_MAT:
                     setTimerTo(minion->S.move, timer, flags, MAT_MOVE_START_TIME, F_move_continue_movement);
                     break;
               }
               minion->S.last_move_time = ts2ms(&mt->elapsed_time);
               minion->S.speed.last_index = 0;
            } break;
            case F_move_continue_movement: {
               // first and foremost, find out the difference in time
               int nt;
               int dt; 
               int vel;
               int accel;
               int dir;
               float cs;
               //DDCMSG(D_POINTER, GRAY, "Started fake movement for minion %i", minion->mID);
               nt = ts2ms(&mt->elapsed_time);
               dt = min((nt - minion->S.last_move_time), 1); // diff = now - last (minimum of 1 millisecond)
               minion->S.last_move_time = nt; // last is now, now

               // get velocity and acceleration for MIT or MAT
               switch (minion->S.dev_type) {
                  default:
                  case Type_MIT:
                     vel = MIT_SPEEDS[minion->S.speed.last_index];
                     if (minion->S.speed.last_index < minion->S.speed.towards_index) {
                        accel = MIT_ACCEL[minion->S.speed.towards_index];
                     } else {
                        accel = 0; // not accelerating any more
                     }
                     cs = ((float)(MIT_SPEEDS[minion->S.speed.towards_index]) / 1000.0) * 2.24; // convert to mph
                     break;
                  case Type_MAT:
                     vel = MAT_SPEEDS[minion->S.speed.last_index];
                     if (minion->S.speed.last_index < minion->S.speed.towards_index) {
                        accel = MAT_ACCEL[minion->S.speed.towards_index];
                     } else {
                        accel = 0; // not accelerating any more
                     }
                     cs = ((float)(MAT_SPEEDS[minion->S.speed.towards_index]) / 1000.0) * 2.24; // convert to mph
                     break;
               }
               
               // find dir value (-1 for left, 1 for right)
               minion->S.move.data = minion->S.move.newdata;
               switch (minion->S.move.data) {
                  case 1:
                     // 1 = right = distance gets bigger over time
                     dir = 1;
                     break;
                  case 2:
                     // 2 = left = distance gets smaller over time
                     dir = -1;
                     break;
                  default:
                     // everything else implies we're not moving at all
                     dir = 0;
               }

               // distance = last distance + last speed * time + acceleration * time^2
               minion->S.speed.fpos = minion->S.speed.lastfpos + /* in meters */
                                    ((float)(dir) * ((float)(vel) / 1000.0) * ((float)(dt) / 1000.0)) + /* convert to meters and seconds */
                                    (((float)(accel) / 1000.0) * ((float)(dt) / 1000.0) * ((float)(dt) / 1000.0)); /* convert to meters and seconds */
               /*DDCMSG(D_POINTER, GREEN, "pos: %f = %f + ((%i * %i * %i) / 1000000) + ((%i * %i * %i) / 1000000000)",
                                         minion->S.speed.fpos, minion->S.speed.lastfpos,
                                         dir, vel, dt, accel, dt, dt);*/

               // speed = last speed + acceleration * time
               if (minion->S.speed.lastdata != cs) {
                  minion->S.speed.data = minion->S.speed.lastdata + /* in mph */
                                         ((((float)(accel) / 1000.0) * 2.237) * /* convert to m/s then to mph */
                                          ((float)(dt) / 1000.0)); /* convert to seconds */
                  /*DDCMSG(D_POINTER, GREEN, "speed: %f = %f + (((%i / 1000.0) * 2.237) * (%i / 1000.0))",
                                            minion->S.speed.data, minion->S.speed.lastdata, accel, dt);*/
                  if (minion->S.speed.data >= cs) {
                     // we've overshot our "correct" goal speed
                     minion->S.speed.data = cs;
                  } else if (minion->S.speed.data == minion->S.speed.lastdata) {
                     // we've settled on a speed, show the "correct" goal speed from now on
                     minion->S.speed.data = cs;
                  }
                  if (minion->S.speed.data == cs) {
                     SEND_STATUS_REQUEST(&LB_buf); // should only happen once
                  }
               }

               // have we reached the end?
               minion->S.position.data = (int)minion->S.speed.fpos;
               minion->S.speed.lastfpos = minion->S.speed.fpos;
               switch (minion->S.dev_type) {
                  default:
                  case Type_MIT:
                     minion->S.speed.fpos = max(0, min(minion->S.speed.fpos, minion->mit_length)); // clamp at 0 to length of track
                     if (minion->S.position.data <= minion->mit_home && dir < 0) {
                        minion->S.fault.data = ERR_stop_left_limit; // stopped by left limit
                        minion->S.speed.data = 0.0;
                     } else if (minion->S.position.data >= minion->mit_end && dir > 0) {
                        minion->S.fault.data = ERR_stop_right_limit; // stopped by right limit
                        minion->S.speed.data = 0.0;
                     }
                     break;
                  case Type_MAT:
                     minion->S.speed.fpos = max(0, min(minion->S.speed.fpos, minion->mat_length)); // clamp at 0 to length of track
                     if (minion->S.position.data <= minion->mat_home && dir < 0) {
                        minion->S.fault.data = ERR_stop_left_limit; // stopped by left limit
                        minion->S.speed.data = 0.0;
                     } else if (minion->S.position.data >= minion->mat_end && dir > 0) {
                        minion->S.fault.data = ERR_stop_right_limit; // stopped by left limit
                        minion->S.speed.data = 0.0;
                     }
                     break;
               }

               // tell the good news
               /*DDCMSG(D_POINTER, GRAY, "Calculated movement values from dt=%i: pos:%i mph:%f move:%i", dt,
                      minion->S.position.data, minion->S.speed.data, minion->S.move.data);*/
               sendStatus2102(0, NULL,minion,mt); // tell FASIT server
               //DDCMSG(D_POINTER, GRAY, "Possibly sent 2102 to FASIT server from minion %i", minion->mID);

               // remember what has transpired
               switch (minion->S.dev_type) {
                  default:
                  case Type_MIT:
                     // look at average speed (in mph) between speed index set points
                     //DDCMSG(D_POINTER, GRAY, "Finding last_index from mph %f", minion->S.speed.data);
                     if (minion->S.speed.data > 0.0) {
                        if (minion->S.speed.data > 2.33) {
                           if (minion->S.speed.data > 4.35) {
                              if (minion->S.speed.data > 8.08) {
                                 // moving above average of 3 and 4, so we'll use 4
                                 minion->S.speed.last_index = 3;
                              } else {
                                 // moving between average of 2 and 3 and 3 and 4, so we'll use 3
                                 minion->S.speed.last_index = 3;
                              }
                           } else {
                              // moving between average of 1 and 2 and 2 and 3, so we'll use 2
                              minion->S.speed.last_index = 2;
                           }
                        } else {
                           // moving, but not reached average of speed 1 and 2, so we'll use 1
                           minion->S.speed.last_index = 1;
                        }
                     } else {
                        // not moving at all, so we'll use 0
                        minion->S.speed.last_index = 0;
                     }
                     //DDCMSG(D_POINTER, GRAY, "Found last_index %i from mph %f", minion->S.speed.last_index, minion->S.speed.data);
                     break;
                  case Type_MAT:
                     //DDCMSG(D_POINTER, GRAY, "Finding last_index from mph %f", minion->S.speed.data);
                     // look at average speed (in mph) between speed index set points
                     if (minion->S.speed.data > 0.0) {
                        if (minion->S.speed.data > 8.23) {
                           if (minion->S.speed.data > 11.65) {
                              if (minion->S.speed.data > 15.53) {
                                 // moving above average of 3 and 4, so we'll use 4
                                 minion->S.speed.last_index = 3;
                              } else {
                                 // moving between average of 2 and 3 and 3 and 4, so we'll use 3
                                 minion->S.speed.last_index = 3;
                              }
                           } else {
                              // moving between average of 1 and 2 and 2 and 3, so we'll use 2
                              minion->S.speed.last_index = 2;
                           }
                        } else {
                           // moving, but not reached average of speed 1 and 2, so we'll use 1
                           minion->S.speed.last_index = 1;
                        }
                     } else {
                        // not moving at all, so we'll use 0
                        // MATs take a while to get going, so leave this be -- minion->S.speed.last_index = 0;
                     }
                     //DDCMSG(D_POINTER, GRAY, "Found last_index %i from mph %f", minion->S.speed.last_index, minion->S.speed.data);
                     break;
               }

               // and come back later
               if (minion->S.speed.data <= 0.0) {
                  if (minion->S.resp.mover_command == move_continuous) {
                  } else {
                     switch (minion->S.dev_type) {
                        default:
                        case Type_MIT:
                           setTimerTo(minion->S.move, timer, flags, MIT_MOVE_START_TIME, F_move_end_movement);
                           break;
                        case Type_MAT:
                           setTimerTo(minion->S.move, timer, flags, MAT_MOVE_START_TIME, F_move_end_movement);
                           break;
                     }
                  }
               } else {
                  // come back after we've moved 1/2 meters
                  float t = 0.75 * (0.44 * minion->S.speed.data); // find number of seconds to go 3/4 meter (converting mph to m/s first) -- spec want every 2 meters, but as we're faking it, we may as well make it look smooth
                  setTimerTo(minion->S.move, timer, flags, (int)(t * 10.0), F_move_continue_movement); // convert time to deciseconds
                  //DDCMSG(D_POINTER, GRAY, "Coming back in %i deciseconds (%f seconds)", (int)(t * 10.0), t);
               }
            } break;
            case F_move_end_movement: {
               // Clean up movement data
               //DDCMSG(D_POINTER, GRAY, "Minion %i is ending movement", minion->mID);
               minion->S.speed.towards_index = 0;
               minion->S.speed.last_index = 0;
               minion->S.speed.lastdata = -1; // force cache to be dirty
               minion->S.speed.data = 0.0;
               minion->S.speed.newdata = 0.0;
               minion->S.move.data = 0; // stopped, no direction
               if (minion->S.fault.data == ERR_stop_right_limit || minion->S.fault.data == ERR_stop_left_limit) {
                  minion->S.fault.data = 0;
               }
               sendStatus2102(0, NULL,minion,mt); // tell FASIT server
               stopTimer(minion->S.move, timer, flags); // don't be coming back now
            } break;
            default: {
               DDCMSG(D_MSTATE, RED, "Default reached in move flags: %i", minion->S.move.flags);
               stopTimer(minion->S.move, timer, flags);
            }
         }
      ); // end of CHECK_TIMER for move state timer

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
#define Set_Timer(T) { \
   if ((T>0)&&(T<minion->S.state_timer)) { minion->S.state_timer=T; } \
}
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
   Set_Timer(minion->S.resp.timer); // timer for response state

   DDCMSG(D_MSTATE,GRAY,"Minion %i:   Set_Timer=%i   exp.exp=%i exp.con=%i rf_t.fast=%i rt_t.slow=%i move=%i (or event timer)",
          minion->mID,minion->S.state_timer, minion->S.exp.exp_timer, minion->S.exp.con_timer, minion->S.rf_t.fast_timer, minion->S.rf_t.slow_timer, minion->S.move.timer);

}
