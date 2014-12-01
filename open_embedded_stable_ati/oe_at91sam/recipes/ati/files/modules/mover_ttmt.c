//---------------------------------------------------------------------------
// mover.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <mach/gpio.h>

#include "netlink_kernel.h"

#include "target.h"
#include "mover.h"
#include "scenario.h"

//---------------------------------------------------------------------------
// For external functions
//---------------------------------------------------------------------------
#include "target_mover_ttmt.h"
#include "target_mover_generic.h"
#include "target_mover_hit_poll.h"
#include "target_generic_output.h"
#include "target_battery.h"
#include "fasit/faults.h"
#define LIFTER_POSITION_DOWN 0
#define LIFTER_POSITION_UP 1

#define DEBUG_USERCONN

#ifdef DEBUG_USERCONN
#define SENDUSERCONNMSG  sendUserConnMsg
#else
#define SENDUSERCONNMSG(...)  //
#endif

//---------------------------------------------------------------------------
// These variables are parameters given when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------

#define MOVED_DELAY 250

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This atomic variable is use to hold what we told userspace last time
//---------------------------------------------------------------------------
atomic_t last_sent = ATOMIC_INIT(0);
//---------------------------------------------------------------------------
// This atomic variable is use to hold our driver id from netlink provider
//---------------------------------------------------------------------------
atomic_t driver_id = ATOMIC_INIT(-1);

atomic_t mover_reaction = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space of a position
// change or error detected by the IRQs or the timeout timer.
//---------------------------------------------------------------------------
static struct work_struct position_work;

//---------------------------------------------------------------------------
// mover state variables
//---------------------------------------------------------------------------
static void move_change(unsigned long data); // forward declaration
static struct timer_list moved_timer = TIMER_INITIALIZER(move_change, 0, 0);

typedef struct hit_item {
    int line; // which hit sensor detected the hit
    struct timespec time; // time hit occurred according to current_kernel_time()
    struct hit_item *next; // link to next log item
} hit_item_t;
static spinlock_t hit_lock = SPIN_LOCK_UNLOCKED;
static hit_item_t *hit_chain[3] = {NULL, NULL, NULL}; // head of hit log chain
static struct timespec hit_start; // time the hit log was started

//---------------------------------------------------------------------------
// Message filler handler for failure messages
//---------------------------------------------------------------------------
int error_mfh(struct sk_buff *skb, void *msg) {
    // the msg argument is a null-terminated string
    return nla_put_string(skb, GEN_STRING_A_MSG, msg);
}

//---------------------------------------------------------------------------
// Message filler handler for expose functions
//---------------------------------------------------------------------------
static int pos_mfh(struct sk_buff *skb, void *pos_data) {
    // the pos_data argument is a pre-made u16 structure
    return nla_put_u16(skb, GEN_INT16_A_MSG, *((u16*)pos_data));
}

//---------------------------------------------------------------------------
// Work item to notify the user-space about a position change or error
//---------------------------------------------------------------------------
static void position_change(struct work_struct * work) {
    u16 pos_data;
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }

    // notify netlink userspace
    pos_data = mover_position_get() + 0x8000; // message is unsigned, fix it be "signed"
    send_nl_message_multi(&pos_data, pos_mfh, NL_C_POSITION);
}

//---------------------------------------------------------------------------
// Message filler handler for expose functions
//---------------------------------------------------------------------------
int move_mfh(struct sk_buff *skb, void *move_data) {
    // the move_data argument is a pre-made u8 structure
    return nla_put_u8(skb, GEN_INT8_A_MSG, *((u8*)move_data));
}

static void sendUserConnMsg( char *fmt, ...){
    va_list ap;
    char *msg;
    va_start(ap, fmt);
   msg = kmalloc(256, GFP_KERNEL);
   if (msg){
      vsnprintf(msg, 256, fmt, ap);
      send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
      kfree(msg);
   }
   va_end(ap);
}

//---------------------------------------------------------------------------
// Timer timeout function for finishing a move change
//---------------------------------------------------------------------------
static void move_change(unsigned long data) {
    u16 move_data;
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    // notify netlink userspace
    move_data = 32768+mover_speed_get(); // signed speed turned to unsigned byte
    send_nl_message_multi(&move_data, pos_mfh, NL_C_MOVE);
}

static void move_faults(int movefault) {
	u8 fault = 0;

   if (movefault) {
      fault = movefault; 
	   // send fault upstream always
	   queue_nl_multi(NL_C_FAULT, &fault, sizeof(fault));
   }
}

//---------------------------------------------------------------------------
// event handler for moves
//---------------------------------------------------------------------------
static void move_event_internal(int etype, bool upload); // forward declaration
void hit_event_internal(int line, bool upload); // forward declaration

static void move_event(int etype) {
   move_event_internal(etype, true); // send event upstream
}

static void move_event_internal(int etype, bool upload) {
    delay_printk("move_event(%i)\n", etype);

    // send event upstream?
    if (upload) {
        u8 data = etype; // cast to 8-bits
        queue_nl_multi(NL_C_EVENT, &data, sizeof(data));
    }

    // event causes change in motion?
    if (etype == EVENT_KILL) {
        // TODO -- programmable coast vs. stop vs. ignore
        mover_set_speed_stop();
        // mover_speed_set(0);
    }

    // create event for outputs
    generic_output_event(etype);

    // notify user-space
    switch (etype) {
        case EVENT_MOVE:
        case EVENT_MOVING:
        case EVENT_IS_MOVING:
            mod_timer(&moved_timer, jiffies+(((MOVED_DELAY/2)*HZ)/1000)); // wait for X milliseconds for sensor to settle
            break;
        case EVENT_STOPPED:
//            enable_battery_check(1); // enable battery checking while motor is off
            mod_timer(&moved_timer, jiffies+(((MOVED_DELAY*4)*HZ)/1000)); // wait for X milliseconds for sensor to settle
            schedule_work(&position_work);
            break;
        case EVENT_POSITION:
            schedule_work(&position_work);
            break;
    }
}

//---------------------------------------------------------------------------
// event handler for hits
//---------------------------------------------------------------------------
void hit_event(int line, int line1) {
    hit_event_internal(line, true); // send hit upstream
}

void hit_event_internal(int line, bool upload) {
	struct hit_item *new_hit;
	struct hit_on_line hol;
        u8 hits = 0;
	u8 data = EVENT_HIT; // cast to 8-bits
	delay_printk("hit_event_internal(line=%i, upload=%d)\n", line,upload);
	SENDUSERCONNMSG("hit_event_internal(line=%i, upload=%d)\n", line,upload);

	// create event
   if (upload){
	   queue_nl_multi(NL_C_EVENT, &data, sizeof(data));
   }
	generic_output_event(EVENT_HIT);
//	lift_event_internal(EVENT_HIT, upload);

	// log event
	new_hit = kmalloc(sizeof(struct hit_item), GFP_KERNEL);
	memset(new_hit, 0, sizeof(struct hit_item));
	if (new_hit != NULL) {
		// change change around
		spin_lock(hit_lock);
		new_hit->next = hit_chain[line];
		hit_chain[line] = new_hit;
		spin_unlock(hit_lock);

		// set values in new hit item
		new_hit->time = timespec_sub(current_kernel_time(), hit_start); // time since start
		new_hit->line = line;
	}

	// create netlink message for userspace (always, no matter what the upload value is)
	// get data from log
	spin_lock(hit_lock);
	new_hit = hit_chain[line];
	while (new_hit != NULL) {
		hits++; // count hit (doesn't matter which line)
		new_hit = new_hit->next; // next link in chain
	}
	spin_unlock(hit_lock);

	// send hits upstream
   if (upload) {
      hol.hits = hits;
      hol.line = line + 1;
SENDUSERCONNMSG( "randy hit_event_internal hits %i line %i\n", hol.hits, hol.line );
      queue_nl_multi(NL_C_HITS_MOVER, &hol, sizeof(struct hit_on_line));
   }
}

void disconnected_hit_sensor_event(int line, int disconnected) {
	u8 fault = 0;

   if (disconnected) {
      fault = ERR_hit_sensor_failure; 
	   // send fault upstream always
	   // Only send disconnects
	   queue_nl_multi(NL_C_FAULT, &fault, sizeof(fault));
   }
}

//---------------------------------------------------------------------------
// netlink command handler for stop commands
//---------------------------------------------------------------------------
int nl_continuous_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
delay_printk("Mover: handling continuous movement command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT16_A_MSG]; // generic 16-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u16(na);
delay_printk("Mover: received value: %i\n", value);

        rc = mover_set_continuous_move(value-32768); // unsigned value to signed speed (0 will coast)

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
            delay_printk("Mover: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        delay_printk("Mover: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Mover: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for stop commands
//---------------------------------------------------------------------------
int nl_moveaway_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
delay_printk("Mover: handling moveaway movement command\n");
SENDUSERCONNMSG( "randy moveaway_handler" );
    
    // get attribute from message
    na = info->attrs[GEN_INT16_A_MSG]; // generic 16-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u16(na);
delay_printk("Mover: received value: %i\n", value);
SENDUSERCONNMSG( "randy moveaway value %i", value );

        rc = mover_set_moveaway_move(value-32768); // unsigned value to signed speed (0 will coast)

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
            delay_printk("Mover: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        delay_printk("Mover: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Mover: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for stop commands
//---------------------------------------------------------------------------
int nl_stop_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
delay_printk("Mover: handling stop command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT16_A_MSG]; // generic 16-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u16(na); // value is ignored
delay_printk("Mover: received value: %i\n", value);

        // stop mover
        mover_set_speed_stop();
// report as emergency stop
        move_faults(ERR_emergency_stop);

        // Stop accessories (will disable them as well)
        generic_output_event(EVENT_ERROR);

        // prepare response
        rc = nla_put_u16(skb, GEN_INT16_A_MSG, 1); // value is ignored

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
            delay_printk("Mover: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        delay_printk("Mover: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Mover: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for move commands
//---------------------------------------------------------------------------
int nl_move_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc;
    u16 value = 0;
delay_printk("Mover: handling move command\n");
SENDUSERCONNMSG( "randy move_handler" );
    
    // get attribute from message
    na = info->attrs[GEN_INT16_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u16(na); // value is ignored
	delay_printk("Mover: received value: %i\n", value-32768);
        delay_printk("Current speed: %i\n", mover_speed_get());

        // default to message handling success (further feedback will come from move_event)
        rc = HANDLE_SUCCESS_NO_REPLY;

        // do something to the mover
        if (value == VELOCITY_STOP) {
SENDUSERCONNMSG( "randy move_handler STOP" );
            // stop
            mover_set_speed_stop(); // -- this is emergency stop
// report as requested stop
            move_faults(ERR_stop);
            // mover_speed_set(0); -- this is "coast" stop
        } else if (value == VELOCITY_REQ) {
SENDUSERCONNMSG( "randy move_handler REQ" );
            // retrieve speed
            value = 32768+mover_speed_get(); // signed speed turned to unsigned byte
            rc = nla_put_u16(skb, GEN_INT16_A_MSG, value);

            // message creation success?
            if (rc == 0) {
                rc = HANDLE_SUCCESS;
            } else {
                delay_printk("Mover: could not create return message\n");
                rc = HANDLE_FAILURE;
            }
        } else {
            // move
//            enable_battery_check(0); // disable battery checking while motor is on
            delay_printk("NL_MOVE_HANDLER: value1: %d value2: %d\n", value, value-32768);
SENDUSERCONNMSG( "randy move_handler value" );
            mover_set_move_speed(value-32768); // unsigned value to signed speed (0 will coast)
        }

    } else {
        delay_printk("Mover: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Mover: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for position requests
//---------------------------------------------------------------------------
int nl_position_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc;
    u16 value = 0;
delay_printk("Mover: handling position request\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT16_A_MSG]; // generic 16-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u16(na); // value is ignored
delay_printk("Mover: received value: %i\n", value);

        // stop mover
        rc = mover_position_get();

        // prepare response
        value = rc + 0x8000; // message is unsigned, fix it be "signed"
        rc = nla_put_u16(skb, GEN_INT16_A_MSG, value);

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
            delay_printk("Mover: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        delay_printk("Mover: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Mover: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}


//---------------------------------------------------------------------------
// netlink command handler for commands
//---------------------------------------------------------------------------
int nl_gohome_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
delay_printk("Mover: handling event command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na); // value is ignored
delay_printk("Mover: received value: %i\n", value);

        mover_go_home();

        rc = HANDLE_SUCCESS_NO_REPLY;
    } else {
        delay_printk("Mover: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Mover: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for coast command
//---------------------------------------------------------------------------
int nl_coast_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
    SENDUSERCONNMSG("nl_coast_handler");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na); // value is ignored
delay_printk("Mover: received value: %i\n", value);

        mover_coast_to_stop();

        rc = HANDLE_SUCCESS_NO_REPLY;
    } else {
        delay_printk("Mover: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Mover: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for accessory commands
//---------------------------------------------------------------------------
int nl_hit_cal_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    struct hit_calibration *hit_c;
    int rc = HANDLE_SUCCESS_NO_REPLY; // by default this is a command with no response
    delay_printk("Mover_ttmt: handling hit-calibration command\n");

    // get attribute from message
    na = info->attrs[HIT_A_MSG]; // accessory message
    if (na) {
        // grab value from attribute
        hit_c = (struct hit_calibration*)nla_data(na);
        if (hit_c != NULL) {
            switch (hit_c->set) {
                case HIT_GET_CAL:        /* overwrites nothing (gets calibration values) */
                case HIT_GET_TYPE:       /* overwrites nothing (gets type value) */
                case HIT_GET_KILL:       /* overwrites nothing (gets hi ts_to_kill value) */
                case HIT_OVERWRITE_NONE: /* overwrite nothing (gets reply with current values) */
                    // fill in all data, the unused portions will be ignored
                    get_mover_hit_calibration(hit_c->line - 1, &hit_c->seperation, &hit_c->sensitivity); // use existing hit_c structure
	SENDUSERCONNMSG("get_mover_hit_calibration(line=%i, seperation=%d, sensitivity=%d)", hit_c->line, hit_c->seperation, hit_c->sensitivity);
                    hit_c->blank_time = 7;
                    hit_c->enable_on = 1;
                    hit_c->hits_to_kill = 0;
                    hit_c->after_kill = 0;
                    hit_c->type = 0;
                    hit_c->invert = 0;
                    nla_put(skb, HIT_A_MSG, sizeof(struct hit_calibration), hit_c);
                    rc = HANDLE_SUCCESS; // with return message
                    break;
                case HIT_OVERWRITE_CAL:   /* overwrites calibration values (sensitivity, seperation) */
                    if (hit_c->line > 0){
	SENDUSERCONNMSG("set_mover_hit_calibration(line=%i, seperation=%d, sensitivity=%d)", hit_c->line, hit_c->seperation, hit_c->sensitivity);
                        // line 0 is the SAT, 1 is front, 2 is rear, 3 is engine
                        set_mover_hit_calibration(hit_c->line - 1, hit_c->seperation, hit_c->sensitivity);
                    }
                    break;
                case HIT_OVERWRITE_TYPE: /* overwrites type value only */
                     // Movers do not provide this, so ignore these types
                     // As of now 4/25/12 movers only care about kill reaction (after_kill)
                    break;
                case HIT_OVERWRITE_ALL:   /* overwrites every value */
                    if (hit_c->line > 0){
                        // line 0 is the SAT
                        set_mover_hit_calibration(hit_c->line - 1, hit_c->seperation, hit_c->sensitivity);
                    }
                    break;
                case HIT_OVERWRITE_OTHER: /* overwrites non-calibration values (type, etc.) */
                case HIT_OVERWRITE_KILL:  /* overwrites hits_to_kill value only */
                    atomic_set(&mover_reaction, hit_c->after_kill);
                    break;
            }
        }
    }

    // return status
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for hit count commands
//---------------------------------------------------------------------------
int nl_hits_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc;
    struct hit_item *this;
    hit_on_line_t *hol;
    delay_printk("Mover: handling hits command\n");

    // get attribute from message
    na = info->attrs[HIT_M_MSG]; // accessory message
    if (na) {
        // grab value from attribute
        hol = (hit_on_line_t*)nla_data(na);
        delay_printk("Mover: received line: %i, nl_hits_handler\n", hol->line - 1);

        // get initial data from log
        rc = 0;
        spin_lock(hit_lock);
        this = hit_chain[hol->line - 1];
        while (this != NULL && hol->hits != 0) {
            rc++; // count hit
            this = this->next; // next link in chain
        }
        spin_unlock(hit_lock);

        // reset hit log?
        if (hol->hits == 0) {
            // determine how many we should have at the end
            if (rc > atomic_read(&last_sent)) {
                rc = atomic_read(&last_sent); // how many we sent before is how many we need to remove now
                // if it's less or equal, just reset
                delay_printk("RESET HITS\n");
                spin_lock(hit_lock);
                this = hit_chain[hol->line - 1];
                while (rc-- > 0 && this != NULL) {
                    delay_printk("SHRANK ONE HIT\n");
                    hit_chain[hol->line - 1] = this; // remember this
                    this = this->next; // move on to next link
                    kfree(hit_chain[hol->line - 1]); // free it
                }
                rc++; // this is how many are left
                spin_unlock(hit_lock);
            } else {
                // if it's less or equal, just reset
                delay_printk("RESET HITS\n");
                spin_lock(hit_lock);
                this = hit_chain[hol->line - 1];
                while (this != NULL) {
                    delay_printk("SHRANK ONE HIT\n");
                    hit_chain[hol->line - 1] = this; // remember this
                    this = this->next; // move on to next link
                    kfree(hit_chain[hol->line - 1]); // free it
                }
                hit_chain[hol->line - 1] = NULL;
                spin_unlock(hit_lock);
            }
            hit_start = current_kernel_time(); // reset hit log start time
        }

        // re-get data from log
        if (hol->hits != 0) {
delay_printk("\n***randy-value: %i\n", hol->hits);
            rc = 0;
            spin_lock(hit_lock);
            this = hit_chain[hol->line - 1];
            while (this != NULL) {
                 rc++; // count hit (doesn't matter which line)
                 this = this->next; // next link in chain
            }
            spin_unlock(hit_lock);
        }

        // fake the hit log data?
        if (hol->hits != HIT_REQ && hol->hits != 0) {
            delay_printk("FAKE HITS\n");
            if (hol->hits > rc) {
                // grow hit log
                while (hol->hits > rc) {
                    delay_printk("GREW ONE HIT\n");
                    // create a full hit event ...
                    hit_event_internal(0, false); // ... except don't send data back upstream
                    rc++; // hit log grew one
                }
            } else if (hol->hits < rc) {
                // shrink hit log
                spin_lock(hit_lock);
                this = hit_chain[hol->line - 1]; // start removing from end of chain (TODO -- remove from other side?)
                while (hol->hits < rc && this != NULL) {
                    delay_printk("SHRANK ONE HIT\n");
                    hit_chain[hol->line - 1] = this; // remember this
                    this = this->next; // move on to next link
                    kfree(hit_chain[hol->line - 1]); // free it
                    rc--; // hit log shrank one
                }
                hit_chain[hol->line - 1] = this;
                spin_unlock(hit_lock);
//                atomic_set(&kill_counter, atomic_read(&hits_to_kill)-rc); // fix kill counter
            }
        }

        // prepare response
        atomic_set(&last_sent, rc); // remember our last sent hol->hits
        hol->hits = rc;
        nla_put(skb, HIT_M_MSG, sizeof(hit_on_line_t), hol);

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
            delay_printk("Mover_ttmt: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        delay_printk("Mover_ttmt: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
    delay_printk("Mover_ttmt: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for event commands
//---------------------------------------------------------------------------
int nl_event_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0, reaction;
    u8 data = BATTERY_SHUTDOWN; // in case we need to shutdown
delay_printk("Mover: handling event command\n");

    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // handle event
        value = nla_get_u8(na); // value is ignored
        switch (value) {
            case EVENT_SHUTDOWN:
                // needs to be converted to NL_C_SHUTDOWN from userspace, so send it back up (and send onwards to other attached devices)
                queue_nl_multi(NL_C_BATTERY, &data, sizeof(data));
                break;
            case EVENT_DOWN:
               set_lifter_position(LIFTER_POSITION_DOWN);
               break;
            case EVENT_UP:
               set_lifter_position(LIFTER_POSITION_UP);
               break;
            case EVENT_KILL:
               set_lifter_position(LIFTER_POSITION_DOWN);
               reaction = atomic_read(&mover_reaction);
// 0-fall, 1-kill, 2-stop, 3-fall and stop, 4-bob - see definitions for lifter kills
               if (reaction == 2 || reaction == 3){
                  if (mover_continuous_speed_get() > 0){
                     mover_set_continuous_move(0);
                  } else {
                     mover_set_speed_stop();
                  }
               } else { // 0, 1, 4 and any undefined reactions
                  if (mover_continuous_speed_get() > 0){
                     mover_set_continuous_move(0);
                     mover_go_home();
                  }
               }
               break;
            default:
                move_event_internal(value, false); // don't repropogate
                break;
        }

        rc = HANDLE_SUCCESS_NO_REPLY;
    } else {
        delay_printk("Mover: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Mover: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for event commands
//---------------------------------------------------------------------------
int nl_fault_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
delay_printk("Mover: handling event command\n");

    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        value = nla_get_u8(na);
        set_lifter_fault(value);

        rc = HANDLE_SUCCESS_NO_REPLY;
    } else {
        delay_printk("Mover: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Mover: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for sleep commands
//---------------------------------------------------------------------------
int nl_sleep_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value;
    u8 data = 0;
delay_printk("Mover: handling sleep command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na); // value is ignored
delay_printk("Mover: received value: %i\n", value);

        if (value != SLEEP_REQUEST) {
           // handle sleep/wake in hw driver
//           mover_sleep_set(value==SLEEP_COMMAND?1:0);
           // handle sleep/wake/dock in hw driver
           if(value == DOCK_COMMAND){
              mover_sleep_set(value);
              data = value==EVENT_DOCK; // convert to event
           } else { 
              mover_sleep_set(value==SLEEP_COMMAND?1:0);
              data = value==SLEEP_COMMAND?EVENT_SLEEP:EVENT_WAKE; // convert to event
           }
//           data = value==SLEEP_COMMAND?EVENT_SLEEP:EVENT_WAKE; // convert to event
           queue_nl_multi(NL_C_EVENT, &data, sizeof(data)); // movers propogate message
           rc = HANDLE_SUCCESS_NO_REPLY;
        } else {
           // retrieve sleep status
           rc = nla_put_u8(skb, GEN_INT8_A_MSG, mover_sleep_get()?SLEEP_COMMAND:WAKE_COMMAND);

           // message creation success?
           if (rc == 0) {
              rc = HANDLE_SUCCESS;
           } else {
              delay_printk("Mover: could not create return message\n");
              rc = HANDLE_FAILURE;
           }
        }
    } else {
        delay_printk("Mover: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Mover: returning rc: %i\n", rc);

    return rc;
}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init Mover_init(void) {
    int retval = 0, d_id;
    struct driver_command commands[] = {
        {NL_C_STOP,      nl_stop_handler},
        {NL_C_MOVE,      nl_move_handler},
        {NL_C_POSITION,  nl_position_handler},
        {NL_C_EVENT,     nl_event_handler},
        {NL_C_FAULT,     nl_fault_handler},
        {NL_C_CONTINUOUS, nl_continuous_handler},
        {NL_C_MOVEAWAY, nl_moveaway_handler},
        {NL_C_GOHOME, nl_gohome_handler},
        {NL_C_COAST, nl_coast_handler},
        {NL_C_HIT_CAL_MOVER,   nl_hit_cal_handler},
        {NL_C_HITS_MOVER,   nl_hits_handler},
        {NL_C_SLEEP,     nl_sleep_handler},
    };
    struct nl_driver driver = {NULL, commands, sizeof(commands)/sizeof(struct driver_command), NULL}; // no heartbeat object, X command in list, no identifying data structure

    // install driver w/ netlink provider
    d_id = install_nl_driver(&driver);
delay_printk("%s(): %s - %s : %i\n",__func__,  __DATE__, __TIME__, d_id);
    atomic_set(&driver_id, d_id);

    // set callback handlers
    set_move_callback(move_event, move_faults);
// Need this for ttmt
    set_mover_hit_callback(0, hit_event, disconnected_hit_sensor_event); // Front tire
    set_mover_hit_callback(1, hit_event, disconnected_hit_sensor_event); // Back tire
    set_mover_hit_callback(2, hit_event, disconnected_hit_sensor_event); // Engine

    INIT_WORK(&position_work, position_change);

    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
            mod_timer(&moved_timer, jiffies+(((MOVED_DELAY/2)*HZ)/1000)); // wait for X milliseconds for sensor to settle
    return retval;
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit Mover_exit(void) {
    atomic_set(&full_init, FALSE);
    del_timer(&moved_timer);
    uninstall_nl_driver(atomic_read(&driver_id));
    ati_flush_work(&position_work); // close any open work queue items
}


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(Mover_init);
module_exit(Mover_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI Mover module");
MODULE_AUTHOR("ndb");

