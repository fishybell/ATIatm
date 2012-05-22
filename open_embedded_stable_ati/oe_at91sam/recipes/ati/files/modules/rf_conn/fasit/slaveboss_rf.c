#include "mcp.h"
#include "slaveboss.h"
#include "rf.h"
#include "eeprom.h"
#include "rf_debug.h"

// we don't worry about clearing the data before a valid message, just up to the end
static void clearBuffer(fasit_connection_t *fc, int end) {
   if (end >= fc->rf_ilen) {
      // clear the entire buffer
      fc->rf_ilen = 0;
   } else {
      // clear out everything up to and including end
      char tbuf[RF_BUF_SIZE];
      D_memcpy(tbuf, fc->rf_ibuf + (sizeof(char) * end), fc->rf_ilen - end);
      D_memcpy(fc->rf_ibuf, tbuf, fc->rf_ilen - end);
      fc->rf_ilen -= end;
   }
}

// add this message to the write buffer
// as this function does not actually write and will not cause the caller function to be
//   preempted, the caller may call this function multiple times to create a complete
//   message and be sure that the entire message is sent
static void queueMsg(fasit_connection_t *fc, void *msg, int size) {

   // check buffer remaining...
   if ((size + fc->rf_olen) > RF_BUF_SIZE) {
      // buffer not big enough
      return;
   }

   // append to existing message
   D_memcpy(fc->rf_obuf + (sizeof(char) *fc->rf_olen), msg, size);
   fc->rf_olen += size;
}

// see if the given packet is for this fasit connection
static int packetForMe(fasit_connection_t *fc, int start) {
   LB_packet_t *hdr = (LB_packet_t*)(fc->rf_ibuf + start);
   int i, j;
   DDCMSG_HEXB(D_RF, RED, "RF packetForMe:", fc->rf_ibuf+start, fc->rf_ilen-start);
   if (hdr->cmd == LBC_REQUEST_NEW) {
      // special case check: check reregister or devid range
      LB_request_new_t *msg = (LB_request_new_t*)hdr;

      if (msg->low_dev <= fc->devid && msg->low_dev+7 >= fc->devid) {
         if (msg->forget_addr&BV(fc->devid - msg->low_dev)){    // checks if our bit forget bit is set
            DDCMSG(D_RF,RED, "RF Packet for my devid range: %x:%x  and forget bit is set for our devid", msg->low_dev, msg->low_dev+7);
            return 1;    // it is for us, (and up to 7 others)  and the forget bit is set so in HANDLE request_new we mustchange our address to 2047
         } else if (fc->id==2047){
            DDCMSG(D_RF,RED, "RF Packet for my devid range: %x:%x  and our address is 2047", msg->low_dev, msg->low_dev+7);
            return 1;    // we have no address so it is for us (and up to 7 other slaves)
         }
      } else if (fc->target_type == RF_Type_Unknown) {
DDCMSG(D_RF,RED, "RF Packet ignored based on not having finished connection: %x", msg->low_dev);
         return 0; // doesn't matter if I'm in the range or not, I've haven't finished connecting
      } else if (fc->id != 2047) {
DDCMSG(D_RF,RED, "RF Packet ignored based on already having registered as %i: %x", fc->id, msg->low_dev);
         return 0; // have already registered
      }
DDCMSG(D_RF,RED, "RF Packet outside my devid range: %x  OR forget bit not set OR our address %d not 2047", msg->low_dev,fc->id);
      return 0;
   } else if (hdr->cmd == LBC_ASSIGN_ADDR) {
      // special case: check devid match (ignore reregister bit if it still exists in packet)
      LB_assign_addr_t *msg = (LB_assign_addr_t*)hdr;
      if (msg->devid == fc->devid) {
DDCMSG(D_RF,RED, "RF Packet for my devid: %x:%x", msg->devid, fc->devid);
         return 1;
      } else if (fc->target_type == RF_Type_Unknown) {
DDCMSG(D_RF,RED, "RF Packet ignored based on not having finished connection: %x", msg->devid);
         return 0;
      } else if (msg->new_addr == fc->id) {
DDCMSG(D_RF,RED, "RF Packet for someone else's devid: %x:%x, but for my addr %i", msg->devid, fc->devid, msg->new_addr);
         fc->id = 2047;
         return 0;
      } else {
DDCMSG(D_RF,RED, "RF Packet for someone else's devid: %x:%x", msg->devid, fc->devid);
         return 0;
      }
   }
   if (hdr->addr == fc->id) {
DDCMSG(D_RF,RED, "RF Packet for me: %i", hdr->addr);
      return 1; // directly for me
   }

   for (i = 0; i<MAX_GROUPS; i++) {
      if (hdr->addr == fc->groups[i]) {
         for (j = 0; j<MAX_GROUPS; j++) {
            if (hdr->addr == fc->groups_disabled[j]) {
DDCMSG(D_RF,RED, "RF Packet for disabled group %i", hdr->addr);
               return 0; // is one of my groups, but it's disabled
            }
         }
DDCMSG(D_RF,RED, "RF Packet for enabled group %i", hdr->addr);
         return 1; // is one of my groups
      }
   }

   // not for me
DDCMSG(D_RF,RED, "RF Packet for other listener %i:%i", hdr->addr, fc->id);
   return 0;
}

// read a single RF message into given buffer, return do next
int rfRead(int fd, char *dest, int *dests) {
   int err;
   DDCMSG(D_RF,YELLOW, "RF READING");
   // read as much as possible
   *dests = read(fd, dest, RF_BUF_SIZE);
   err = errno; // save errno
   DDCMSG(D_RF,YELLOW, "RF READ %i BYTES", *dests);

   // did we read nothing?
   if (*dests <= 0) {
      if (err == EAGAIN) {
         // try again later
         *dests = 0;
         return doNothing;
      } else {
         // connection dead, remove it
         *dests = 0;
PERROR("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %s:%i", __FILE__, __LINE__);
         return rem_rfEpoll;
      }
   }

   // data found, let the handler parse it
   return doNothing;
}

// write all RF messages for connection in fconns
int rfWrite(fasit_connection_t *fc) {
   int s, err;
   DDCMSG(D_RF,BLUE, "RF WRITE");

   // have something to write?
   if (fc->rf_olen <= 0) {
      // we only send data, or listen for writability, if we have something to write
      return mark_rfRead;
   }

   // write all the data we can
   s = write(fc->rf, fc->rf_obuf, fc->rf_olen);
   err = errno; // save errno
   DDCMSG(D_RF,BLUE, "RF WROTE %i BYTES", s);
   debugRF(blue, fc->rf_obuf, fc->rf_olen);

   // did it fail?
   if (s <= 0) {
      if (s == 0 || err == EAGAIN) {
         // try again later
         return doNothing;
      } else {
         // connection dead, remove it
PERROR("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %s:%i", __FILE__, __LINE__);
         return rem_rfEpoll;
      }
   } else if (s < fc->rf_olen) {
      // we can't leave only a partial message going out, finish writing even if we block
      int opts;

      // change to blocking from non-blocking
      opts = fcntl(fc->rf, F_GETFL); // grab existing flags
      if (opts < 0) {
PERROR("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %s:%i", __FILE__, __LINE__);
         return rem_rfEpoll;
      }
      opts = (opts ^ O_NONBLOCK); // remove nonblock from existing flags
      if (fcntl(fc->rf, F_SETFL, opts) < 0) {
PERROR("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %s:%i", __FILE__, __LINE__);
         return rem_rfEpoll;
      }

      // loop until written (since we're blocking, it won't loop forever, just until timeout)
      while (s >= 0) {
         int err, ns = write(fc->rf, fc->rf_obuf + (sizeof(char) * s), fc->rf_olen - s);
         err = errno; // save errno
         if (ns < 0 && err != EAGAIN) {
            // connection dead, remove it
PERROR("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %s:%i", __FILE__, __LINE__);
            return rem_rfEpoll;
         }
         s += ns; // increase total written, possibly by zero
      }

      // change to non-blocking from blocking
      opts = (opts | O_NONBLOCK); // add nonblock back into existing flags
      if (fcntl(fc->rf, F_SETFL, opts) < 0) {
PERROR("RF Died because ");
DDCMSG(D_RF,RED, "RF Dead at %s:%i", __FILE__, __LINE__);
         return rem_rfEpoll;
      }

      // don't try writing again
      return mark_rfRead;
   } else {
      // everything was written, clear write buffer
      fc->rf_olen = 0;

      // don't try writing again
      return mark_rfRead;
   }

   // partial success, leave writeable so we try again
   return doNothing;
}

void addToRFBuffer(fasit_connection_t *fc, char *buf, int s) {
   // replace buffer?
   if (fc->rf_ilen <= 0) {
      D_memcpy(fc->rf_ibuf, buf, s);
      fc->rf_ilen = s;
   } else {
      // add to buffer
      D_memcpy(fc->rf_ibuf + (sizeof(char) * fc->rf_ilen), buf, s);
      fc->rf_ilen += s;
   }
}

// the start and end values may be set even if no valid message is found
static int validMessage(fasit_connection_t *fc, int *start, int *end) {
   LB_packet_t *hdr;
   int hl = -1;
   int mnum = -1;
   *start = 0;
   // loop through entire buffer, parsing starting at each character
   while (*start < fc->rf_ilen && mnum == -1) {
      // map the memory, we don't need to manipulate it
      hdr = (LB_packet_t*)(fc->rf_ibuf + *start);
      
      // check for valid message first
      hl = RF_size(hdr->cmd);
      if (((fc->rf_ibuf + fc->rf_ilen) - (fc->rf_ibuf + *start)) < hl) { *start = *start + 1; continue; } // invalid message length, or more likely, don't have all of the message yet

      *end = *start + hl;
      DDCMSG(D_RF,RED, "RF validMessage (cmd %i)", hdr->cmd);
      switch (hdr->cmd) {
         // they all have a crc, check it
         case LBC_EXPOSE:
         case LBC_MOVE:
         case LBC_CONFIGURE_HIT:
         case LBC_AUDIO_CONTROL:
         case LBC_PYRO_FIRE:
         case LBC_DEVICE_REG:
         case LBC_ASSIGN_ADDR:
         case LBC_STATUS_REQ:
         case LBC_STATUS_RESP:
         case LBC_EVENT_REPORT:
         case LBC_REPORT_ACK:
         case LBC_GROUP_CONTROL:
         case LBC_POWER_CONTROL:
         case LBC_QEXPOSE:
         case LBC_RESET:
         case LBC_QCONCEAL:
         case LBC_REQUEST_NEW:
            if (crc8(hdr) == 0) {
            //   DDCMSG(D_RF,RED, "RF VALID CRC");
               return hdr->cmd;
            } else {
            //   DDCMSG(D_RF,RED, "RF INVALID CRC");
            }
            break;

         // not a valid number, not a valid header
         default:
            break;
      }

      *start = *start + 1;
   }

//   DDCMSG(D_RF,RED, "RF invalid: %08X %i %i", fc->rf_ibuf, *start, fc->rf_ilen);
//   DDCMSG(D_RF,HEXB(RED, "RF invalid:", fc->rf_ibuf+*start, fc->rf_ilen-*start);
   return -1;
}

// macro used in rf2fasit
#define HANDLE_RF(RF_NUM) case LBC_ ## RF_NUM : retval |= handle_ ## RF_NUM (fc, start, end) ; break;

// mangle an rf message into 1 or more fasit messages, and potentially respond with rf message
int rf2fasit(fasit_connection_t *fc, char *buf, int s) {
   int start, end, mnum, retval = doNothing;
   DDCMSG(D_RF,RED, "RF 2 FASIT");

   // check client
   if (!fc->rf) {
      return doNothing;
   }
  
   // read all available valid messages
   debugRF(RED, fc->rf_ibuf, fc->rf_ilen);
   while ((mnum = validMessage(fc, &start, &end)) != -1) {
      if (!packetForMe(fc, start)) {
         DDCMSG(D_RF,BLACK,"Ignored RF message %d",mnum);
      } else {
         DDCMSG(D_RF,BLACK,"Recieved RF message %d",mnum);
         debugRF(RED, fc->rf_ibuf + start, end-start);
         if (fc->sleeping && mnum != LBC_POWER_CONTROL) {
            // ignore message when sleeping
            DDCMSG(D_RF,BLACK,"Slept through RF message %d",mnum);
         } else {
            switch (mnum) {
               HANDLE_RF (STATUS_REQ);
               HANDLE_RF (REPORT_ACK);
               HANDLE_RF (EXPOSE);
               HANDLE_RF (MOVE);
               HANDLE_RF (CONFIGURE_HIT);
               HANDLE_RF (GROUP_CONTROL);
               HANDLE_RF (AUDIO_CONTROL);
               HANDLE_RF (POWER_CONTROL);
               HANDLE_RF (PYRO_FIRE);
               HANDLE_RF (QEXPOSE);
               HANDLE_RF (RESET);
               HANDLE_RF (QCONCEAL);
               HANDLE_RF (REQUEST_NEW);
               HANDLE_RF (ASSIGN_ADDR);
               default:
                  break;
            }
         }
      }
      clearBuffer(fc, end); // clear out last message
   }
   return retval;
}

int handle_STATUS_REQ(fasit_connection_t *fc, int start, int end) {
   LB_status_req_t *pkt = (LB_status_req_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_STATUS_REQ(%8p, %i, %i)", fc, start, end);
   // wait 'til we have the most up-to-date information to send
   fc->waiting_status_resp = 1;
   DDCMSG(D_RF|D_MEGA, BLACK, "#######################################\nWaiting: %i, Epoll: %i\n#######################################", fc->waiting_status_resp, added_rf_to_epoll);
   return send_2100_status_req(fc); // gather latest information
}

int handle_REPORT_ACK(fasit_connection_t *fc, int start, int end) {
   LB_report_ack_t *pkt = (LB_report_ack_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_REPORT_ACK(%8p, %i, %i)", fc, start, end);
   
   // reset the given amount of hits for the event in question
   log_ResetHits_Some(fc, pkt->event, pkt->hits, pkt->report);
   DDCMSG(D_POINTER, black, "handled REPORT ACK for %i %i %i", pkt->event, pkt->hits, pkt->report);
   return doNothing;
}

// returns 1 if a > b, 0 if a == b, -1 if a < b
int compTime(struct timespec a, struct timespec b) {
   if (a.tv_sec > b.tv_sec) {
      return 1;
   } else if (a.tv_sec == b.tv_sec) {
      if (a.tv_nsec > b.tv_nsec) {
         return 1;
      } else if (a.tv_nsec == b.tv_nsec) {
         return 0;
      } else {
         return -1;
      }
   } else {
      return -1;
   }
}

int send_EVENT_REPORTs(fasit_connection_t *fc) {
   int i, retval = doNothing;
   int report_limit = 4; // assuming 36 bytes allowed per response window (110 ms @ 9600 baud with 64 bytes overhead and 26.6 ms warm-up) and 9 bytes from the status response, that leaves almost room for 4 reports per response (1 byte too small, boo-hoo, we're going for it)
   hit_event_link_t *this = NULL; // chain of reports to send
   hit_event_link_t **head =  &this;
   hit_event_link_t *temp = NULL; // a temporary reference to possibly prevent looking through chain twice
   hit_event_link_t *last = NULL; // the last this
   struct timespec tsn;
   clock_gettime(CLOCK_MONOTONIC,&tsn);

   // don't remember the new report between report sending times
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i @ %3i.%03i", __FILE__, __LINE__, tsn.tv_sec, tsn.tv_nsec/1000000l);
   for (i=0; i < MAX_HIT_EVENTS; i++) {
      fc->hit_event_sum[i].new_report = -1;
   }
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i", __FILE__, __LINE__);

   // look at each hit and place in the report chain
   for (i=0; i < MAX_HITS; i++) {
      if (!(fc->hit_times[i].time.tv_sec == 0 && fc->hit_times[i].time.tv_nsec == 0l)) {
   DDCMSG(D_POINTER, GREEN, "Looking @ %s:%i with %i", __FILE__, __LINE__, i);
   DDCMSG(D_POINTER, GREEN, "...found hit @ time %3i.%03i event %i, report %i, event reported: %i, event new rpt: %i",
         fc->hit_times[i].time.tv_sec, fc->hit_times[i].time.tv_nsec/1000000l,
         fc->hit_times[i].event, fc->hit_times[i].report,
         fc->hit_event_sum[fc->hit_times[i].event].reported, fc->hit_event_sum[fc->hit_times[i].event].new_report);
         int found = 0; // have we found a link for this hits event/report combo
         int event = fc->hit_times[i].event; // shortcut to the current event
         this = *head;
         // look through chain to see if I already created a report to send
         while (this != NULL && (found == 0 || found == 2)) {
            if (event == this->event &&
                fc->hit_times[i].report != -1 &&
                fc->hit_times[i].report == this->report) {
   DDCMSG(D_POINTER, GREEN, "Found link @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
               // matched event and (valid) report
               found = 1; // will re-report (adding on to this reports count)
            } else {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
               if (event == this->event) {
   DDCMSG(D_POINTER, GREEN, "Partial link @ %s:%i with %i:%p as evt %i:%i rpt %i:%i", __FILE__, __LINE__, i, this, event, this->event, fc->hit_times[i].report, this->report);
                  found = 2; // we found a report that matched my event; we'll have to keep looking and might have to come back to this report
                  if (fc->hit_event_sum[event].new_report != -1 &&
                      this->report == fc->hit_event_sum[event].new_report) {
   DDCMSG(D_POINTER, GREEN, "Temp link @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
                     // we found the latest report for this event, remember it for later (should only be one match)
                     temp = this;
                  }
               }
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
               // move on to next item
               last = this;
               this = this->next;
            }
         }

         // partial match? we need to either use the newly created report for this event or create a new one
         if (found == 2) {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p %i:%i:%i", __FILE__, __LINE__, i, this, fc->hit_times[i].report, fc->hit_event_sum[event].reported, fc->hit_event_sum[event].new_report);
            // check to see if I want to use the found report or not
            if (fc->hit_times[i].report == -1 && fc->hit_event_sum[event].reported == 0) {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
               // we have created a report number for this event and the hit has not been reported
               if (temp != NULL) {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
                  // we found the correct link to use before
                  this = temp;
                  fc->hit_times[i].report = temp->report; // assign correct report number to hit
                  found = 1;
               } else {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
                  // we need to create a new report for this event
                  // TODO -- we should never get here as it implies we recently created a report, but can't find it -- we need to prove that we can't get here or do something more drastic here
                  found = 0;
               }
            } else {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
               // the hit as been reported, or both have been reported, either way, create a new link
               found = 0;
            }
         }

         // create a new link in the chain?
         if (found == 0) {
            // no match, create a new link in the chain
            this = malloc(sizeof(hit_event_link_t));
   DDCMSG(D_POINTER, GREEN, "Creating @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
            if (last != NULL) {
               last->next = this; // link together
            }

            // prepare chain
            this->event = event;
            if (fc->hit_times[i].report == -1) {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
               if (++fc->last_report > 255) {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
                  fc->last_report = 0;
               }
               fc->hit_times[i].report = fc->last_report;
            }
            this->report = fc->hit_times[i].report;
            this->next = NULL;
            // prepare packet
            memset(&this->pkt, 0, sizeof(this->pkt));
            this->pkt.cmd = LBC_EVENT_REPORT;
            this->pkt.event = event;
            this->pkt.report = this->report;
            this->pkt.addr = fc->id & 0x7ff; // source address (always to basestation)
            if (!(fc->event_starts[event].tv_sec == 0 && fc->event_starts[event].tv_nsec == 0l) &&
                fc->event_ends[event].tv_sec == 0 && fc->event_ends[event].tv_nsec == 0l) {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
               // start time, but no end time, count everything as unqualified
               this->pkt.qualified = 0;
            } else {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
               // there is an expose and conceal time, we're qualified 
               this->pkt.qualified = 1;
            }
            // remember the last report used with this event (so other non-reported hits use the same report)
            fc->hit_event_sum[event].new_report = this->report;
         }
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
         
         // "this" is now a correct match, count the hit
         if (this->pkt.qualified == 0) {
            // unqualified packets count everything
            this->pkt.hits++;
   DDCMSG(D_POINTER, GREEN, "Count unqual @ %s:%i with %i:%p as hit %i in evt %i rpt %i", __FILE__, __LINE__, i, this, this->pkt.hits, this->event, this->report);
         } else {
   DDCMSG(D_POINTER, GREEN, "Check qual @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
            // qualified packets require correct time
            if (compTime(fc->event_starts[event], fc->hit_times[i].time) <= 0 &&
                compTime(fc->event_ends[event], fc->hit_times[i].time) >= 0) {
               this->pkt.hits++;
   DDCMSG(D_POINTER, GREEN, "Count qual @ %s:%i with %i:%p as hit %i in evt %i rtp %i", __FILE__, __LINE__, i, this, this->pkt.hits, this->event, this->report);
            } else {
   DDCMSG(D_POINTER, GREEN, "Fail qual @ %s:%i with %i:%p es:%3i.%03i ee:%3i.%03i ht:%3i.%03i", __FILE__, __LINE__, i, this, fc->event_starts[event].tv_sec, fc->event_starts[event].tv_nsec/1000000l, fc->event_ends[event].tv_sec, fc->event_ends[event].tv_nsec/1000000l, fc->hit_times[i].time.tv_sec, fc->hit_times[i].time.tv_nsec/1000000l);
            }
         }
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %i:%p", __FILE__, __LINE__, i, this);
      }
   }

   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i", __FILE__, __LINE__);
   // send the reports
   this = *head;
   while (this != NULL) {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %p", __FILE__, __LINE__, this);
      if (this->pkt.hits > 0) {
   DDCMSG(D_POINTER, GREEN, "Sending @ %s:%i with %p %i %i %i", __FILE__, __LINE__, this, this->pkt.event, this->pkt.report,this->pkt.hits);
         // finish and send
         set_crc8(&this->pkt);
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %p", __FILE__, __LINE__, this);
         queueMsg(fc, &this->pkt, RF_size(LBC_EVENT_REPORT));
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %p", __FILE__, __LINE__, this);
         retval |= mark_rfWrite;
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %p, %p, %i", __FILE__, __LINE__, this, fc, this->event);
         // mark this event as reported
         fc->hit_event_sum[this->event].reported = 1;
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %p", __FILE__, __LINE__, this);

         // limit number of event reports to send each time
         if (--report_limit <= 0) {
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %p", __FILE__, __LINE__, this);
            // we've reached our maximum of sent reports, call it good
            break;
         }
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %p", __FILE__, __LINE__, this);
      }
      this = this->next; // next link in chain
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i with %p", __FILE__, __LINE__, this);
   }
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i", __FILE__, __LINE__);

   // free allocated memory
   this = *head;
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i", __FILE__, __LINE__);
   while (this != NULL) {
   DDCMSG(D_POINTER, GREEN, "Freeing @ %s:%i", __FILE__, __LINE__);
      temp = this;
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i", __FILE__, __LINE__);
      this = this->next; // next link in chain
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i", __FILE__, __LINE__);
      free(temp);
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i", __FILE__, __LINE__);
   }
   
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i", __FILE__, __LINE__);
   return retval;
#if 0
   // prepare message
   LB_event_report_t LB;
   memset(&LB, 0, sizeof(LB));
   LB.cmd = LBC_EVENT_REPORT;
   LB.event = event;
   LB.report = report;
   LB.addr = fc->id & 0x7ff; // source address (always to basestation)

   // check if qualified or unqualified
   if (fc->event_starts[event].tv_sec !=0 && fc->event_starts[event].tv_nsec != 0l &&
       fc->event_ends[event].tv_sec == 0 && fc->event_ends[event].tv_nsec == 0l) {
      // start time, but no end time, count everything as unqualified
      LB.qualified = 0;
      for (i=0; i < MAX_HITS; i++) {
         if (fc->hit_times[i].event == event) {
            LB.hits++;
            fc->hit_times[i].report = report; // this hit has been reported, it can now be cleared on ACK
         }
      }
   } else {
      // there is an expose and conceal time, we're qualified 
      LB.qualified = 1;
      for (i=0; i < MAX_HITS; i++) {
         if (fc->hit_times[i].event == event &&
             compTime(fc->event_starts[event], fc->hit_times[i].time) <= 0 &&
             compTime(fc->event_ends[event], fc->hit_times[i].time) >= 0) {
            // time between expose and conceal times, count hit
            LB.hits++;
            fc->hit_times[i].report = report; // this hit has been reported, it can now be cleared on ACK
         }
      }
   }

   // send if we found hits
   if (LB.hits > 0) {
      set_crc8(&LB);
      queueMsg(fc, &LB, RF_size(LBC_EVENT_REPORT));
      return mark_rfWrite;
   } else {
      return doNothing;
   }
#endif
   DDCMSG(D_POINTER, GREEN, "Here @ %s:%i", __FILE__, __LINE__);
}

int send_STATUS_RESP(fasit_connection_t *fc) {
   int retval = doNothing, i;
   
   // build up current response
   LB_status_resp_t s;
   DDCMSG(D_RF|D_VERY,RED, "send_STATUS_RESP(%08X)", fc);
   D_memset(&s, 0, sizeof(LB_status_resp_t));
   //s.hits = max(0,min(fc->hit_event_sum[fc->current_event], 127)); // cap upper/lower bounds
   if (fc->f2102_resp.body.exp == 45) {
      DCMSG(BLACK, "||||||||||||||||\nUSING FUTURE: %i\n||||||||||||||||", fc->future_exp);
      s.expose = fc->future_exp == 90 ? 1 : 0; // look into the future
   } else {
      s.expose = fc->f2102_resp.body.exp == 90 ? 1: 0; // transitions become "down"
   }
   DCMSG(GRAY, "RESP with expose: s.expose=%i, fc->f2102_resp.body.exp=%i @ %i", s.expose, fc->f2102_resp.body.exp, __LINE__);
   s.speed = max(0,min(htonf(fc->f2102_resp.body.speed) * 100, 2047)); // cap upper/lower bounds
   s.move = fc->f2102_resp.body.move & 0x3;
   s.location = htons(fc->f2102_resp.body.pos) & 0x7ff;
   s.hitmode = fc->hit_mode;
   s.react = fc->hit_react;
   s.sensitivity = fc->hit_react;
   s.timehits = fc->hit_burst;
   s.fault = fc->last_fault;
   s.tokill = fc->hit_tokill;
   s.did_exp_cmd = fc->did_exp_cmd == 3 ? 1 : 0; // only fully-over-finished task counts
   DDCMSG(D_RF|D_VERY,BLACK, "Fault encountered: %04X %02X", fc->last_fault, s.fault);
   if (s.fault) {
      fc->last_fault = 0; // clear out fault
   }
   // we don't know if they received the last response, so send everything every time
   // copy current status to last status and send it
   fc->last_status = s;
   // finish filling in message and send
   fc->last_status.cmd = LBC_STATUS_RESP;
   fc->last_status.addr = fc->id & 0x7FF; // source address (always to basestation)

   // set crc and send
   set_crc8(&fc->last_status);
   queueMsg(fc, &fc->last_status, RF_size(LBC_STATUS_RESP));
   retval = mark_rfWrite;

#if 0
   DDCMSG(D_MEGA, BLACK, "\n============================\nNew values: %i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i\nOld values: %i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i\n============================\nf2102 vals: %i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i\n============================\n",
      s.hits, s.expose, s.speed, s.move, s.react, s.location, s.hitmode, s.tokill, s.sensitivity, s.timehits, s.fault,
      fc->last_status.hits, fc->last_status.expose, fc->last_status.speed, fc->last_status.move, fc->last_status.react, fc->last_status.location, fc->last_status.hitmode, fc->last_status.tokill, fc->last_status.sensitivity, fc->last_status.timehits, fc->last_status.fault,
      htons(fc->f2102_resp.body.hit), fc->f2102_resp.body.exp, htonf(fc->f2102_resp.body.speed), htons(fc->f2102_resp.body.move), fc->f2102_resp.body.hit_conf.react, htons(fc->f2102_resp.body.pos), fc->f2102_resp.body.hit_conf.mode, htons(fc->f2102_resp.body.hit_conf.tokill), htons(fc->f2102_resp.body.hit_conf.sens), htons(fc->f2102_resp.body.hit_conf.burst), htons(fc->f2102_resp.body.fault));

   // determine changes send correct status response
   if (s.fault != fc->last_status.fault ||
       s.timehits != fc->last_status.timehits ||
       s.sensitivity != fc->last_status.sensitivity ||
       s.react != fc->last_status.react ||
       s.tokill != fc->last_status.tokill ||
       s.hitmode != fc->last_status.hitmode) {
      // copy current status to last status and send it
      fc->last_status = s;
      retval = send_STATUS_RESP_EXT(fc);
      goto SR_end;
#if 0
   } else if (s.location != fc->last_status.location ||
              s.move != fc->last_status.move ||
              s.speed != fc->last_status.speed ||
              s.expose != fc->last_status.expose ||
              s.hits > 0) {
#else
   } else {
#endif
      // copy current status to last status and send it
      fc->last_status = s;
      // send appropriate mover or lifter message
      if (fc->target_type == RF_Type_MIT || fc->target_type == RF_Type_MAT) {
         retval = send_STATUS_RESP_MOVER(fc);
         goto SR_end;
      } else {
         retval = send_STATUS_RESP_LIFTER(fc);
         goto SR_end;
      }
#if 0
   } else {
      // nothing changed, send that
      retval = send_STATUS_NO_RESP(fc);
      goto SR_end;
#endif
   }
   // single spot to reset hit_hit
   SR_end:
   fc->hit_hit = last_hh;
#endif
   // send an appropriate number of event reports if I have any hits
   retval |= send_EVENT_REPORTs(fc);
   return retval;
}

int handle_EXPOSE(fasit_connection_t *fc, int start, int end) {
   LB_expose_t *pkt = (LB_expose_t *)(fc->rf_ibuf + start);
   int retval = doNothing;
   static int mfsSDelay = -1;
   static int mfsRDelay = -1;
   DDCMSG(D_RF|D_VERY,RED, "handle_EXPOSE(%8p, %i, %i)", fc, start, end);

   // read, once, the eeprom values for start and repeat delays
   if (mfsSDelay == -1) {
      mfsSDelay = ReadEeprom_int(MFS_START_DELAY_LOC, MFS_START_DELAY_SIZE, MFS_START_DELAY);
      mfsRDelay = ReadEeprom_int(MFS_REPEAT_DELAY_LOC, MFS_REPEAT_DELAY_SIZE, MFS_REPEAT_DELAY);
   }

   // send mfs configuration
   if (fc->has_MFS) {
      switch (pkt->mfs) {
         case 0:
            // off
            retval |= send_2110(fc, 0, 0, 0, 0);
            break;
         case 1:
            // single
            retval |= send_2110(fc, 1, 0, mfsSDelay, mfsRDelay);
            break;
         case 2:
            // burst
            retval |= send_2110(fc, 1, 1, mfsSDelay, mfsRDelay);
            break;
         case 3:
            // simulate single or burst randomly
            if (rand() % 2) {
               retval |= send_2110(fc, 1, 0, mfsSDelay, mfsRDelay);
            } else {
               retval |= send_2110(fc, 1, 1, mfsSDelay, mfsRDelay);
            }
            break;
      }
   }

   // end previous event if we're not exposing
   if (!pkt->expose) {
      DDCMSG(D_NEW, CYAN, "EXPOSE (%i) Changing event_ends[%i] from %3i.%03i (start:%3i.%03i + %i)",
             pkt->expose, fc->current_event,
             fc->event_ends[fc->current_event].tv_sec, fc->event_ends[fc->current_event].tv_nsec/1000000l,
             fc->event_starts[fc->current_event].tv_sec, fc->event_starts[fc->current_event].tv_nsec/1000000l);
      clock_gettime(CLOCK_MONOTONIC,&fc->event_ends[fc->current_event]);
      DDCMSG(D_NEW, CYAN, "EXPOSE (%i) Changed event_ends[%i] to %3i.%03i",
             pkt->expose, fc->current_event,
             fc->event_ends[fc->current_event].tv_sec, fc->event_ends[fc->current_event].tv_nsec/1000000l);
   }


   // change event
   fc->current_event = pkt->event;
   log_ResetHits_All(fc); // clear out everything for this event
   clock_gettime(CLOCK_MONOTONIC,&fc->event_starts[fc->current_event]);
   if (pkt->expose) {
      fc->future_exp = 90;
   }
   fc->did_exp_cmd = 0; // haven't accomplished command yet (count as a command even if we're not changing exposure)
   DCMSG(BLACK, "||||||||||||||||\nSETTING FUTURE: %i\n||||||||||||||||", fc->future_exp);

   // send configure hit sensing
   retval |= send_2100_conf_hit(fc, 4, /* blank on conceal */
                                0, /* reset hit count for this new event */
                                fc->hit_react, /* remembered hit reaction */
                                pkt->tokill, /* hits to kill */
                                fc->hit_sens, /* remembered hit sensitivity */
                                pkt->hitmode ? 2 : 1, /* burst / single */
                                fc->hit_burst); /* remembered hit burst seperation */

   
   // send expose command
   if (pkt->expose) {
      retval |= send_2100_exposure(fc, 90);
   }
   
   return retval;
}

int handle_MOVE(fasit_connection_t *fc, int start, int end) {
   LB_move_t *pkt = (LB_move_t *)(fc->rf_ibuf + start);
   // convert speed
   float speed = pkt->speed / 100.0;
   DDCMSG(D_RF|D_VERY,RED, "handle_MOVE(%8p, %i, %i)", fc, start, end);
   DDCMSG(D_RF, BLUE, "handle_MOVE(%f, %i)", speed, pkt->move);
   if (pkt->speed == 2047) {
      // send e-stop message
      return send_2100_estop(fc);
   }

   // send movement message
   return send_2100_movement(fc, pkt->move,speed); /* converted speed value */ 
}

int handle_CONFIGURE_HIT(fasit_connection_t *fc, int start, int end) {
   LB_configure_t *pkt = (LB_configure_t *)(fc->rf_ibuf + start);
   int retval = doNothing;
   int hit_temp = 555; // "do-nothing" number
   DDCMSG(D_RF|D_VERY,RED, "handle_CONFIGURE_HIT(%8p, %i, %i)", fc, start, end);

   // check for thermal control only
   if (pkt->react == 7) {
      // TOOD -- add thermal control
      return doNothing;
   }

   // hit count set
   switch (pkt->hitcountset) {
      case 0:
         // no change
         break;
      case 1:
         // reset to zero
         //  hit_temp = 0; -- NO!!! don't listen to SmartRange/TRACR
         hit_temp = 555; // we'll leave it alone if it's setting it to zero and handle resets on our own
         break;
      case 2:
         // incriment by one
         hit_temp = 1;
         break;
      case 3:
         // set to hits-to-kill value
         hit_temp = pkt->tokill;
         break;
   }

   // reaction settings
   if (pkt->react == 6) {
      retval |= send_14110(fc, 1); // enable PHI
      fc->hit_phi = 1; // remember
      fc->hit_react = 1; // kill
   } else if (fc->hit_phi) {
      retval |= send_14110(fc, 1); // disable previously enabled PHI
      fc->hit_phi = 0; // forget
      fc->hit_react = pkt->react;
   } else {
      fc->hit_react = pkt->react;
   }

   // remember other hit settings
   fc->hit_tokill = pkt->tokill;
   fc->hit_sens = pkt->sensitivity;
   fc->hit_mode = pkt->hitmode ? 2 : 1; // burst / single
   fc->hit_burst = pkt->timehits * 5; // each number is 5 milliseconds
   
   // send configure hit sensing
   retval |= send_2100_conf_hit(fc, 4, /* blank on conceal */
                                hit_temp, /* remembered hit reset value */
                                fc->hit_react, /* remembered hit reaction */
                                fc->hit_tokill, /* remembered hits to kill */
                                fc->hit_sens, /* remembered hit sensitivity */
                                fc->hit_mode, /* remembered hit mode */
                                fc->hit_burst); /* remembered hit burst seperation */

   return retval;
}

int handle_GROUP_CONTROL(fasit_connection_t *fc, int start, int end) {
   LB_group_control_t *pkt = (LB_group_control_t *)(fc->rf_ibuf + start);
   // which group command?
   int i, j;
   DDCMSG(D_RF|D_VERY,RED, "handle_GROUP_CONTROL(%8p, %i, %i)", fc, start, end);
   switch (pkt->gcmd) {
      case 0: // disable command
         for (i = 0; i<MAX_GROUPS; i++) {
            if (pkt->gaddr == fc->groups[i]) {
               // found that I am part of this group
               for (j = 0; j<MAX_GROUPS; j++) {
                  // clear out any existing ones in the disabled list
                  if (fc->groups_disabled[j] == pkt->gaddr) {
                     fc->groups_disabled[j] = 0;
                  }
               }
               for (j = 0; j<MAX_GROUPS; j++) {
                  if (fc->groups_disabled[j] == 0) {
                     // found place in group disable list that's free
                     fc->groups_disabled[j] = pkt->gaddr;
                     return doNothing;
                  }
               }
               return doNothing;
            } 
         }
         return doNothing; break;
      case 1: // enable command
         for (i = 0; i<MAX_GROUPS; i++) {
            // clear out any existing ones in the disabled list
            if (fc->groups_disabled[i] == pkt->gaddr) {
               fc->groups_disabled[i] = 0;
            }
         }
         return doNothing; break;
      case 2: // join command
         for (i = 0; i<MAX_GROUPS; i++) {
            // clear out any existing ones in the group list
            if (fc->groups[i] == pkt->gaddr) {
               fc->groups[i] = 0;
            }
         }
         for (i = 0; i<MAX_GROUPS; i++) {
            // find empty spot in group list
            if (fc->groups[i] == 0) {
               fc->groups[i] = pkt->gaddr;
               return doNothing;
            }
         }
         return doNothing; break;
      case 3: // seperate command
         for (i = 0; i<MAX_GROUPS; i++) {
            // clear out any existing ones in the group list
            if (fc->groups[i] == pkt->gaddr) {
               fc->groups[i] = 0;
            }
         }
         return doNothing; break;
   }
   return doNothing;
}

int handle_AUDIO_CONTROL(fasit_connection_t *fc, int start, int end) {
   LB_packet_t *pkt = (LB_packet_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_AUDIO_CONTROL(%8p, %i, %i)", fc, start, end);
   // TODO -- fill me in
   return doNothing;
}

int handle_POWER_CONTROL(fasit_connection_t *fc, int start, int end) {
   LB_power_control_t *pkt = (LB_power_control_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_POWER_CONTROL(%8p, %i, %i)", fc, start, end);
   // send correct power command to fasit client
   switch (pkt->pcmd) {
      case 0: // ignore command
         return doNothing; break;
      case 1: // sleep command
         fc->sleeping = 1;
         return send_2100_power(fc, CID_Sleep); break;
      case 2: // wake command
         fc->sleeping = 0;
         return send_2100_power(fc, CID_Wake); break;
      case 3: // extended shutdown command
         return send_2100_power(fc, CID_Shutdown); break;
   }
   return doNothing;
}

int handle_PYRO_FIRE(fasit_connection_t *fc, int start, int end) {
   LB_pyro_fire_t *pkt = (LB_pyro_fire_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_PYRO_FIRE(%8p, %i, %i)", fc, start, end);
   // send pyro fire request (will handle "set" and "fire" commands to BES)
   send_2000(fc, pkt->zone);
   return doNothing;
}

int handle_QEXPOSE(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_RF|D_VERY,RED, "handle_QEXPOSE(%8p, %i, %i)", fc, start, end);
   // send exposure request
   fc->future_exp = 90;
   fc->did_exp_cmd = 0; // haven't accomplished command yet
   return send_2100_exposure(fc, 90); // 90^ = exposed
}

int handle_RESET(fasit_connection_t *fc, int start, int end) {
   DDCMSG(D_RF|D_VERY,RED, "handle_RESET(%8p, %i, %i)", fc, start, end);
   fc->doing_reset = 1;
   // send reset command
   return send_2100_reset(fc);
}

int handle_QCONCEAL(fasit_connection_t *fc, int start, int end) {
   struct timespec *tv;
   int uptime_sec;
   int uptime_dsec;
   LB_qconceal_t *pkt = (LB_qconceal_t *)(fc->rf_ibuf + start);
   fc->future_exp = 0;
   fc->did_exp_cmd = 0; // haven't accomplished command yet
   DDCMSG(D_RF|D_VERY,RED, "handle_QCONCEAL(%8p, %i, %i)", fc, start, end);
   // pre-calculate uptime into seconds and deciseconds
   uptime_sec = pkt->uptime / 10; // everything above 10, divided by 10
   uptime_dsec = pkt->uptime % 10; // everything below 10
   // remember end time (not now, but the actual end time based on the "up" time)
   DDCMSG(D_NEW, CYAN, "QCONCEAL Changing event_ends[%i] from %3i.%03i (start:%3i.%03i)",
          fc->current_event,
          fc->event_ends[fc->current_event].tv_sec, fc->event_ends[fc->current_event].tv_nsec/1000000l,
          fc->event_starts[fc->current_event].tv_sec, fc->event_starts[fc->current_event].tv_nsec/1000000l);
   tv = &fc->event_ends[fc->current_event]; // for convenience sake, use a pointer
   *tv = fc->event_starts[fc->current_event]; // start with a copy
   tv->tv_sec += uptime_sec;
   tv->tv_nsec += 100000000L * uptime_dsec; // convert deciseconds to nanoseconds
   while (tv->tv_nsec > 1000000000L) { // carry over any seconds
      tv->tv_nsec -= 1000000000L;
      tv->tv_sec++;
   }
   DDCMSG(D_NEW, CYAN, "QCONCEAL Changed event_ends[%i] to %3i.%03i",
          fc->current_event,
          fc->event_ends[fc->current_event].tv_sec, fc->event_ends[fc->current_event].tv_nsec/1000000l);

   // send exposure request
   return send_2100_exposure(fc, 0); // 0^ = concealed
}

int send_DEVICE_REG(fasit_connection_t *fc) {
   // create a message packet with most significant bytes of the FASIT device ID
   LB_device_reg_t bdy;
   DDCMSG(D_RF|D_VERY,RED, "send_DEVICE_REG(%08X)", fc);
   D_memset(&bdy, 0, sizeof(LB_device_reg_t));
   bdy.cmd = LBC_DEVICE_REG;
   bdy.dev_type = fc->target_type;
   bdy.devid = fc->devid;
   DDCMSG(D_RF|D_MEGA, BLACK, "Going to pass devid: %02X:%02X:%02X:%02X", (bdy.devid & 0xff000000) >> 24, (bdy.devid & 0xff0000) >> 16, (bdy.devid & 0xff00) >> 8, bdy.devid & 0xff);

   // status block
   //bdy.hits = max(0,min(fc->hit_event_sum[fc->current_event], 127)); // cap upper/lower bounds
   if (fc->f2102_resp.body.exp == 45) {
      DCMSG(BLACK, "||||||||||||||||\nUSING FUTURE: %i\n||||||||||||||||", fc->future_exp);
      bdy.expose = fc->future_exp == 90 ? 1 : 0; // look into the future
   } else {
      bdy.expose = fc->f2102_resp.body.exp == 90 ? 1: 0; // transitions become "down"
   }
   bdy.speed = max(0,min(htonf(fc->f2102_resp.body.speed) * 100, 2047)); // cap upper/lower bounds
   DCMSG(GRAY, "REG with expose: bdy.expose=%i, fc->f2102_resp.body.exp=%i @ %i", bdy.expose, fc->f2102_resp.body.exp, __LINE__);
   switch (fc->f2102_resp.body.move) {
      case 0:
      default:
         bdy.speed = 0.0;
         bdy.move = 0; // default to towards home if stopped
         break;
      case 1:
         bdy.move = 0; // convert fasit to rf
         break;
      case 2:
         bdy.move = 1; // convert fasit to rf
         break;
   }
   bdy.location = htons(fc->f2102_resp.body.pos) & 0x7ff;
   bdy.hitmode = fc->hit_mode;
   bdy.react = fc->hit_react;
   bdy.sensitivity = fc->hit_react;
   bdy.timehits = fc->hit_burst;
   bdy.fault = fc->last_fault;
   bdy.tokill = fc->hit_tokill;
   
   // copy this info to last sent status
   //fc->last_status.hits = bdy.hits;
   fc->last_status.expose = bdy.expose;
   fc->last_status.speed = bdy.speed;
   fc->last_status.move = bdy.move;
   fc->last_status.location = bdy.location;
   fc->last_status.hitmode = bdy.hitmode;
   fc->last_status.react = bdy.react;
   fc->last_status.sensitivity = bdy.sensitivity;
   fc->last_status.timehits = bdy.timehits;
   fc->last_status.fault = bdy.fault;
   fc->last_status.tokill = bdy.tokill;

   // put in the crc and send
   set_crc8(&bdy);
   queueMsg(fc, &bdy, RF_size(LBC_DEVICE_REG));
   return mark_rfWrite;
}

// a request for new messages has arrived, send back "DEVICE_REG"
int handle_REQUEST_NEW(fasit_connection_t *fc, int start, int end) {
   LB_request_new_t *pkt = (LB_request_new_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_REQUEST_NEW(%8p, %i, %i)", fc, start, end);

   // if we got here, we must forget our old address
   fc->id=2047;
   // only send a registry if I have a fully connected fasit client
   if (fc->target_type == RF_Type_Unknown) {
      DDCMSG(D_RF, RED, "haven't finished connecting yet, can't register");
      return doNothing;
   }

   // I must already have matched 'packetforme' to get here, so I should send the dev registration

   return send_DEVICE_REG(fc); // register now
}


int handle_ASSIGN_ADDR(fasit_connection_t *fc, int start, int end) {
   LB_assign_addr_t *pkt = (LB_assign_addr_t *)(fc->rf_ibuf + start);
   DDCMSG(D_RF|D_VERY,RED, "handle_ASSIGN_ADDR(%8p, %i, %i)", fc, start, end);
   // change device ID to new address
   fc->id = pkt->new_addr;
   DDCMSG(D_RF, RED, "SLAVEBOSS Registered as %i for %08X", fc->id, fc);
   // reset hit event log
   for (fc->current_event = MAX_HIT_EVENTS - 1; fc->current_event >= 0; fc->current_event--) {
      log_ResetHits_All(fc);
   } // should end on fc->current_event of -1
   fc->current_event = 0; // ... make it a 0 again
   return doNothing;
}



