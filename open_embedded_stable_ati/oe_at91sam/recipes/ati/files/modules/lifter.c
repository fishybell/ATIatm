//---------------------------------------------------------------------------
// lifter.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <mach/gpio.h>

#include "netlink_kernel.h"

#include "target.h"
#include "lifter.h"

//---------------------------------------------------------------------------
// For external functions
//---------------------------------------------------------------------------
#include "target_lifter_infantry.h"
#include "target_generic_output.h"
#include "target_hit_poll.h"
#include "target_battery.h"
#include "eeprom_settings.h"
#include "fasit/faults.h"
#include "defaults.h"

//---------------------------------------------------------------------------
#define TARGET_NAME     "lifter"

//#define DEBUG_SEND
//#define PRINT_DEBUG

#ifdef PRINT_DEBUG
#define DELAY_PRINTK  delay_printk
#else
#define DELAY_PRINTK(...)  //
#endif

#ifdef DEBUG_SEND
#define SENDUSERCONNMSG  sendUserConnMsg
#else
#define SENDUSERCONNMSG(...)  //
#endif

//---------------------------------------------------------------------------
// Message filler handler for failure messages
//---------------------------------------------------------------------------
static int error_mfh(struct sk_buff *skb, void *msg) {
    // the msg argument is a null-terminated string
    return nla_put_string(skb, GEN_STRING_A_MSG, msg);
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
// These variables are parameters given when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
static int has_miles = FALSE;          // has MILES hit sensor
module_param(has_miles, bool, S_IRUGO);
static int has_thm_pulse = 0;           // Uses thermal pulse
module_param(has_thm_pulse, int, S_IRUGO);
static int has_bes = 0;           // has battlefield effects simulator
module_param(has_bes, int, S_IRUGO);
static int has_hitX = FALSE;           // has X mechanical hit sensors
module_param(has_hitX, int, S_IRUGO);
static int has_engine = FALSE;            // has engine mechanical hit sensor
module_param(has_engine, bool, S_IRUGO);
static int has_wheelX = FALSE;            // has wheel X mechanical hit sensors
module_param(has_wheelX, int, S_IRUGO);
static int has_turret = FALSE;            // has turret hit sensor
module_param(has_turret, bool, S_IRUGO);
static int has_kill_reaction = 3;           // hits to kill
module_param(has_kill_reaction, int, S_IRUGO);
static int has_hits_to_kill = 1;           // hits to kill
module_param(has_hits_to_kill, int, S_IRUGO);
static int has_hits_to_bob = 1;           // hits to bob
module_param(has_hits_to_bob, int, S_IRUGO);

void lift_event_internal(int etype, bool upload); // forward declaration
void hit_event_internal(int line, bool upload); // forward declaration

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate which direction we toggled last
//---------------------------------------------------------------------------
atomic_t toggle_last = ATOMIC_INIT(CONCEAL); // assume conceal last to expose first

//---------------------------------------------------------------------------
// This atomic variable is use to hold our driver id from netlink provider
//---------------------------------------------------------------------------
atomic_t driver_id = ATOMIC_INIT(-1);

//---------------------------------------------------------------------------
// This atomic variable is use to hold what we told userspace last time
//---------------------------------------------------------------------------
atomic_t last_sent = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This atomic variables used to hold what we received from below last time
//   and what we were lass commanded to by userspace (to see down-up-downs)
//---------------------------------------------------------------------------
atomic_t last_lift_event = ATOMIC_INIT(EVENT_ERROR);
atomic_t last_lift_cmd = ATOMIC_INIT(EXPOSURE_REQ);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space of a position
// change or error detected by the IRQs or the timeout timer.
//---------------------------------------------------------------------------
static struct work_struct position_work;

//---------------------------------------------------------------------------
// This delayed work queue item is used to correct the hit sensor enabled value
//---------------------------------------------------------------------------
static struct work_struct hit_enable_work;

//---------------------------------------------------------------------------
// hit log variables
//---------------------------------------------------------------------------
typedef struct hit_item {
    int line; // which hit sensor detected the hit
    struct timespec time; // time hit occurred according to current_kernel_time()
    struct hit_item *next; // link to next log item
} hit_item_t;
static spinlock_t hit_lock = SPIN_LOCK_UNLOCKED;
static hit_item_t *hit_chain = NULL; // head of hit log chain
static struct timespec hit_start; // time the hit log was started

//---------------------------------------------------------------------------
// lifter state variables
//---------------------------------------------------------------------------
atomic_t hits_to_kill = ATOMIC_INIT(1); // infinite hits to kill
atomic_t kill_counter = ATOMIC_INIT(1); // invalid hit count for hits_to_kill
atomic_t hit_type = ATOMIC_INIT(1); // single-fire mechanical
atomic_t after_kill = ATOMIC_INIT(0); // stay down on kill
atomic_t bob_type = ATOMIC_INIT(-1); // invalid hit count for bob_type
atomic_t hits_to_bob = ATOMIC_INIT(1); // invalid hit count for bob
atomic_t bob_counter = ATOMIC_INIT(1); // invalid hit count for bob
static struct timer_list kill_timer; // TODO -- stay down for a little while?
atomic_t blank_time = ATOMIC_INIT(0); // no blanking time
atomic_t enable_on = ATOMIC_INIT(BLANK_ON_CONCEALED); // blank when fully concealed
atomic_t enable_doing = ATOMIC_INIT(0); // hit sensor enabling nothing
static void blank_off(unsigned long data); // forward declaration
static struct timer_list blank_timer = TIMER_INITIALIZER(blank_off, 0, 0);
atomic_t at_conceal = ATOMIC_INIT(0); // do nothing when conceald

void lift_faults(int liftfault) {
	u8 fault = 0;

   if (liftfault) {
      fault = liftfault; 
	   // send fault upstream always
	   queue_nl_multi(NL_C_FAULT, &fault, sizeof(fault));
   }

}

//---------------------------------------------------------------------------
// netlink command handler for expose commands
//---------------------------------------------------------------------------
int nl_expose_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
    DELAY_PRINTK("Lifter: handling expose command\n");

    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na);
        DELAY_PRINTK("Lifter: received value: %i, nl_expose_handler\n", value);

        switch (value) {
            case TOGGLE:
                enable_battery_check(0); // disable battery checking while motor is on
                // grab current position
                if (lifter_position_get() == LIFTER_POSITION_UP) {
                    // was up, go down
                    lifter_position_set(LIFTER_POSITION_DOWN); // conceal now
                    atomic_set(&toggle_last, CONCEAL); // remember toggle direction
                    atomic_set(&last_lift_cmd, CONCEAL);
                    atomic_set(&last_lift_event, EVENT_ERROR); // no event
                } else if (lifter_position_get() == LIFTER_POSITION_DOWN) {
                    // was down, go up
                    lifter_position_set(LIFTER_POSITION_UP); // expose now
                    atomic_set(&toggle_last, EXPOSE); // remember toggle direction
		            atomic_set(&kill_counter, atomic_read(&hits_to_kill)); // reset kill counter
                    atomic_set(&bob_counter, atomic_read(&hits_to_bob)); // reset bob counter
                    atomic_set(&last_lift_cmd, EXPOSE);
                    atomic_set(&last_lift_event, EVENT_ERROR); // no event
                } else { // moving or error
                    // otherwise go opposite of last direction
                    if (atomic_read(&toggle_last) == EXPOSE) {
                        // went up last time, go down now
                        lifter_position_set(LIFTER_POSITION_DOWN); // conceal now
                        atomic_set(&toggle_last, CONCEAL); // remember toggle direction
                        atomic_set(&last_lift_cmd, CONCEAL);
                        atomic_set(&last_lift_event, EVENT_ERROR); // no event
                    } else { // assume conceal last
                        // went down last time, go up now
                        lifter_position_set(LIFTER_POSITION_UP); // expose now
                        atomic_set(&toggle_last, EXPOSE); // remember toggle direction
                        atomic_set(&last_lift_cmd, EXPOSE);
                        atomic_set(&last_lift_event, EVENT_ERROR); // no event
                    }
                }
                rc = -1; // we'll be going later
                break;

            case EXPOSE:
                enable_battery_check(0); // disable battery checking while motor is on
                atomic_set(&last_lift_cmd, EXPOSE);
                atomic_set(&last_lift_event, EVENT_ERROR); // no event
                // do expose
                if (lifter_position_get() != LIFTER_POSITION_UP) {
                    lifter_position_set(LIFTER_POSITION_UP); // expose now
		            atomic_set(&kill_counter, atomic_read(&hits_to_kill)); // reset kill counter
                    atomic_set(&bob_counter, atomic_read(&hits_to_bob)); // reset bob counter
                    rc = -1; // we'll be going later
                } else {
                    rc = EXPOSE; // we're already there
                    atomic_set(&enable_doing, 1); // an action, not a calibration is changing the sensor
                    schedule_work(&hit_enable_work); // we "reached" a "new" position; change hit sensor enabled state
                }
                break;

            case CONCEAL:
                enable_battery_check(0); // disable battery checking while motor is on
                atomic_set(&last_lift_cmd, CONCEAL);
                atomic_set(&last_lift_event, EVENT_ERROR); // no event
                // do conceal
                if (lifter_position_get() != LIFTER_POSITION_DOWN) {
                    lifter_position_set(LIFTER_POSITION_DOWN); // conceal now
                    rc = -1; // we'll be going later
                } else {
                    rc = CONCEAL; // we're already there
                    atomic_set(&enable_doing, 1); // an action, not a calibration is changing the sensor
                    schedule_work(&hit_enable_work); // we "reached" a "new" position; change hit sensor enabled state
                }
                break;

            default:
            case EXPOSURE_REQ:
                // do expose
                rc = lifter_position_get();

                // prepare response
                switch (rc) {
                    case LIFTER_POSITION_DOWN: rc = CONCEAL; break;
                    case LIFTER_POSITION_UP: rc = EXPOSE; break;
                    case LIFTER_POSITION_MOVING: rc = LIFTING; break;
                    default: rc = EXPOSURE_REQ; break; // error
                }
                break;
        }


        // are we creating a response?
        if (rc != -1) {
            rc = nla_put_u8(skb, GEN_INT8_A_MSG, rc); // rc depends on above
        }

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else if (rc == -1) {
            rc = HANDLE_SUCCESS_NO_REPLY;
        } else {
            DELAY_PRINTK("Lifter: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        DELAY_PRINTK("Lifter: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
    DELAY_PRINTK("Lifter: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for hit count commands
//---------------------------------------------------------------------------
int nl_hits_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
    struct hit_item *this;
    DELAY_PRINTK("Lifter: handling hits command\n");

    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na);
        DELAY_PRINTK("Lifter: received value: %i, nl_hits_handler\n", value);

        // get initial data from log
        rc = 0;
        spin_lock(hit_lock);
        this = hit_chain;
        while (this != NULL && value != 0) {
            rc++; // count hit (doesn't matter which line)
            this = this->next; // next link in chain
        }
        spin_unlock(hit_lock);

        // reset hit log?
        if (value == 0) {
            // determine how many we should have at the end
            if (rc > atomic_read(&last_sent)) {
                rc = atomic_read(&last_sent); // how many we sent before is how many we need to remove now
                // if it's less or equal, just reset
                DELAY_PRINTK("RESET HITS\n");
                spin_lock(hit_lock);
                this = hit_chain;
                while (rc-- > 0 && this != NULL) {
                    DELAY_PRINTK("SHRANK ONE HIT\n");
                    hit_chain = this; // remember this
                    this = this->next; // move on to next link
                    kfree(hit_chain); // free it
                }
                rc++; // this is how many are left
                spin_unlock(hit_lock);
            } else {
                // if it's less or equal, just reset
                DELAY_PRINTK("RESET HITS\n");
                spin_lock(hit_lock);
                this = hit_chain;
                while (this != NULL) {
                    DELAY_PRINTK("SHRANK ONE HIT\n");
                    hit_chain = this; // remember this
                    this = this->next; // move on to next link
                    kfree(hit_chain); // free it
                }
                hit_chain = NULL;
                spin_unlock(hit_lock);
            }
            hit_start = current_kernel_time(); // reset hit log start time
//            atomic_set(&kill_counter, atomic_read(&hits_to_kill)); // reset kill counter
        }

        // re-get data from log
        if (value != 0) {
DELAY_PRINTK("\n***Shelly1-value: %i\n", value);
            rc = 0;
            spin_lock(hit_lock);
            this = hit_chain;
            while (this != NULL) {
                 rc++; // count hit (doesn't matter which line)
                 this = this->next; // next link in chain
            }
            spin_unlock(hit_lock);
        }

        // fake the hit log data?
        if (value != HIT_REQ && value != 0) {
            DELAY_PRINTK("FAKE HITS\n");
            if (value > rc) {
                // grow hit log
                while (value > rc) {
                    DELAY_PRINTK("GREW ONE HIT\n");
                    // create a full hit event ...
                    hit_event_internal(0, false); // ... except don't send data back upstream
                    rc++; // hit log grew one
                }
            } else if (value < rc) {
                // shrink hit log
                spin_lock(hit_lock);
                this = hit_chain; // start removing from end of chain (TODO -- remove from other side?)
                while (value < rc && this != NULL) {
                    DELAY_PRINTK("SHRANK ONE HIT\n");
                    hit_chain = this; // remember this
                    this = this->next; // move on to next link
                    kfree(hit_chain); // free it
                    rc--; // hit log shrank one
                }
                hit_chain = this;
                spin_unlock(hit_lock);
//                atomic_set(&kill_counter, atomic_read(&hits_to_kill)-rc); // fix kill counter
            }
        }

        // prepare response
        atomic_set(&last_sent, rc); // remember our last sent value
        rc = nla_put_u8(skb, GEN_INT8_A_MSG, rc); // rc is number of hits

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
            DELAY_PRINTK("Lifter: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        DELAY_PRINTK("Lifter: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
    DELAY_PRINTK("Lifter: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for hit count commands
//---------------------------------------------------------------------------
int nl_hit_log_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
    struct hit_item *this;
    char *wbuf;
    DELAY_PRINTK("Lifter: handling hits command\n");

    // get attribute from message
    na = info->attrs[GEN_STRING_A_MSG]; // generic string message
    if (na) {
        // get count from log
        rc = 0;
        spin_lock(hit_lock);
        this = hit_chain;
        while (this != NULL) {
            rc++; // count hit (doesn't matter which line)
DELAY_PRINTK("\n***Shelly2-increase rc: %i\n", rc);
            this = this->next; // next link in chain
        }
        spin_unlock(hit_lock);

        // allocate memory for log
        value = 20+rc*50;
        wbuf = kmalloc(value, GFP_KERNEL);

        // build human-readable/machine-readable log
        snprintf(wbuf, 20, "line,time_s,time_n\n");
        rc = strnlen(wbuf, value);
        spin_lock(hit_lock);
        this = hit_chain;
        while (this != NULL) {
            rc = strnlen(wbuf, value-rc);
            snprintf(wbuf+rc, value-rc, "%i,%li,%li\n", this->line, this->time.tv_sec, this->time.tv_nsec);
            this = this->next; // next link in chain
        }
        spin_unlock(hit_lock);

        // prepare response
        rc = nla_put_string(skb, GEN_STRING_A_MSG, wbuf);

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
            DELAY_PRINTK("Lifter: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        DELAY_PRINTK("Lifter: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
    DELAY_PRINTK("Lifter: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for stop commands
//---------------------------------------------------------------------------
int nl_stop_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
    DELAY_PRINTK("Lifter: handling stop command\n");

    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na); // value is ignored
        DELAY_PRINTK("Lifter: received value: %i, nl_stop_handler\n", value);

        // stop motor wherever it is
// Stop in down position        lifter_position_set(LIFTER_POSITION_ERROR_NEITHER);
//      NO NO NO, lower the lifters NOW, this is emergency stop
        lifter_position_set(LIFTER_POSITION_DOWN); // conceal now
        enable_battery_check(1); // enable battery checking while motor is off

        // TODO -- disable hit sensor (but don't clear hit log)

        lift_faults(ERR_emergency_stop);
        // Stop accessories (will disable them as well)
        generic_output_event(EVENT_ERROR);

        // prepare response
        rc = nla_put_u8(skb, GEN_INT8_A_MSG, 1); // value is ignored

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
            DELAY_PRINTK("Lifter: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        DELAY_PRINTK("Lifter: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
    DELAY_PRINTK("Lifter: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

// Accessory Command, moved here so we can call when we want to instead of
// needing netlink to do it. Needed to enable BES when lifting
int do_accessory_configure(int callMethod, struct accessory_conf *acc_c, struct sk_buff *skb){
    int rc = HANDLE_SUCCESS_NO_REPLY; // by default this is a command with no response
    // prepare mode and active mode value for later
    int a_mode = 0, mode = CONSTANT_ON; // may be overwritten depending on accessory type
    int i, num, num2, num3, num4 = 0; 
    SENDUSERCONNMSG("Q X%iX %i %i %i %i %i %i %i %i %i %i %i %i %i\n", acc_c->acc_type, acc_c->exists, acc_c->on_now, acc_c->on_exp, acc_c->on_hit, acc_c->on_kill, acc_c->on_time, acc_c->off_time, acc_c->start_delay, acc_c->repeat_delay, acc_c->repeat, acc_c->ex_data1, acc_c->ex_data2, acc_c->ex_data3);
    DELAY_PRINTK("Q X%iX %i %i %i %i %i %i %i %i %i %i %i %i %i\n", acc_c->acc_type, acc_c->exists, acc_c->on_now, acc_c->on_exp, acc_c->on_hit, acc_c->on_kill, acc_c->on_time, acc_c->off_time, acc_c->start_delay, acc_c->repeat_delay, acc_c->repeat, acc_c->ex_data1, acc_c->ex_data2, acc_c->ex_data3);
    switch (acc_c->on_exp) {
	case 1: a_mode |= ACTIVE_UP | UNACTIVE_LOWER; break; // active when fully exposed only
	case 2: a_mode |= ACTIVE_RAISE | UNACTIVE_LOWER; break; // active when partially and fully expose
	case 3: a_mode |= ACTIVE_RAISE | UNACTIVE_UP | ACTIVE_LOWER | UNACTIVE_DOWN; break; // active on transition
	case 4: a_mode |= ACTIVE_RAISE | UNACTIVE_DOWN; break; // active when partially exposed and fully concealed
	case 5:
            a_mode |= ACTIVE_RAISE | UNACTIVE_LOWER;
            mode = TEMP_ON_PULSE;
            break; // active when partially exposed and fully concealed
    }
    switch (acc_c->on_hit) {
	case 1: a_mode |= ACTIVE_HIT; mode = TEMP_ON; break; // make sure we're not on forever
	case 2: a_mode |= UNACTIVE_HIT; break;
    }
    switch (acc_c->on_kill) {
	case 1: a_mode |= ACTIVE_KILL; mode = TEMP_ON; break; // make sure we're not on forever
	case 2: a_mode |= UNACTIVE_KILL; break;
    }

    // find out which of multiple accessories to configure
    switch (acc_c->acc_type) {
	case ACC_THERMAL_PULSE :
            mode = TEMP_ON_PULSE;
	case ACC_THERMAL :
	case ACC_MILES_SDH :
	case ACC_SES :
	case ACC_NES_MFS :
	case ACC_NES_PHI :
	case ACC_NES_MGL :
	    for (i=generic_output_exists(acc_c->acc_type); i > 0; i--) { // do I have one?
		DELAY_PRINTK("Lifter: Using Single Accessory %i\n", acc_c->acc_type);
		num = i; // use this one
	    }
	    break;
	case ACC_SMOKE :
	    if (generic_output_exists(acc_c->acc_type) >= acc_c->ex_data1) { // do I have this one? (ex_data1 is thermal #)
		DELAY_PRINTK("Lifter: Using Multiple Accessory %i:%i\n", acc_c->acc_type, acc_c->ex_data1);
		num = acc_c->ex_data1; // use this one
	    }
	    break;
	default :
	    DELAY_PRINTK("Lifter: bad accessory type: %s\n", acc_c->acc_type);
	    break;
	case ACC_BES_ENABLE :
	case ACC_BES_TRIGGER_1 :
	case ACC_BES_TRIGGER_2 :
	case ACC_BES_TRIGGER_3 :
	case ACC_BES_TRIGGER_4 :
            if (callMethod) mode = TEMP_ON;
	    for (i=generic_output_exists(acc_c->acc_type); i > 0; i--) { // do I have one?
		DELAY_PRINTK("Lifter: Using Single Accessory %i\n", acc_c->acc_type);
		num = i; // use this one
	    }
	    break;
    }


    if (!acc_c->request) {
	DELAY_PRINTK("Doing accessory configure\n");
SENDUSERCONNMSG( "Doing accessory configure");
	// configure based on accessory type
	switch (acc_c->acc_type) {
	    case ACC_NES_MFS:
		if (lifter_position_get() == LIFTER_POSITION_UP) {
		    acc_c->on_now = 2;
		}
		// burst or single mode?
		if (acc_c->ex_data1) {
		    // burst mode
		    if (acc_c->ex_data2 >= 255) { // 8-bits, so shouldn't be bigger
			DELAY_PRINTK("MFS burst infinite repeat\n");
SENDUSERCONNMSG( "MFS burst infinite repeat %i %i %i %i", acc_c->acc_type, acc_c->on_now, acc_c->ex_data1, acc_c->ex_data2);
			generic_output_set_onoff_repeat(acc_c->acc_type, num, -1); // infinite repeat
		    } else {
			DELAY_PRINTK("MFS burst repeat: %i\n", acc_c->ex_data2);
SENDUSERCONNMSG( "MFS burst repeat %i %i %i %i", acc_c->acc_type, acc_c->on_now, acc_c->ex_data1, acc_c->ex_data2);
			generic_output_set_onoff_repeat(acc_c->acc_type, num, acc_c->ex_data2);
		    }
		    mode = BURST_FIRE;
		} else {
		    DELAY_PRINTK("MFS not bursting\n");
SENDUSERCONNMSG( "MFS not bursting %i %i %i %i", acc_c->acc_type, acc_c->on_now, acc_c->ex_data1, acc_c->ex_data2);
		    mode = TEMP_ON;
		}
		// mfs has randomized delays
		generic_output_set_initial_delay_random(acc_c->acc_type, num, acc_c->start_delay*250); // convert to milliseconds and halve
		generic_output_set_repeat_delay_random(acc_c->acc_type, num, acc_c->repeat_delay*250); // convert to milliseconds and halve
		break;
	    case ACC_MILES_SDH:
		// TODO -- what to do with MILES data?
		break;
	    case ACC_BES_ENABLE:
SENDUSERCONNMSG( "BES ENABLE %i %i %i", acc_c->acc_type, acc_c->on_now, acc_c->ex_data1);
DELAY_PRINTK( "BES ENABLE %i %i %i %i", acc_c->acc_type, acc_c->on_now, acc_c->on_exp, acc_c->ex_data1);
/*		    a_mode = ACTIVE_RAISE | UNACTIVE_LOWER; // active when partially and fully expose
		    mode = CONSTANT_ON; // BES Enable is always this
		    acc_c->repeat = 0;
		    acc_c->start_delay = 0;
		    acc_c->repeat_delay = 0;
		    acc_c->on_time = 0;	*/
		break;
	    case ACC_BES_TRIGGER_1:
SENDUSERCONNMSG( "TRIGGER 1 %i %i %i", acc_c->acc_type, acc_c->on_now, acc_c->ex_data1);
		    mode = TEMP_ON; // Triggers are always this value
		break;
	    case ACC_BES_TRIGGER_2:
	    case ACC_BES_TRIGGER_3:
	    case ACC_BES_TRIGGER_4:
SENDUSERCONNMSG( "TRIGGER %i %i %i", acc_c->acc_type, acc_c->on_now, acc_c->ex_data1);
		    mode = TEMP_ON; // Triggers are always this value
/*		    acc_c->repeat = 2;
		    acc_c->start_delay = 0;
		    acc_c->repeat_delay = 2;
		    acc_c->on_time = 1000;	*/
		break;
	}

	// configure generic
	if (acc_c->on_now || acc_c->on_exp || acc_c->on_hit || acc_c->on_kill) {
	    generic_output_set_enable(acc_c->acc_type, num, ENABLED);
	} else {
	    generic_output_set_enable(acc_c->acc_type, num, DISABLED);
	}
	generic_output_set_active_on(acc_c->acc_type, num, a_mode);
	generic_output_set_mode(acc_c->acc_type, num, mode);
	generic_output_set_initial_delay(acc_c->acc_type, num, acc_c->start_delay*500); // convert to milliseconds
	generic_output_set_repeat_delay(acc_c->acc_type, num, acc_c->repeat_delay*500); // convert to milliseconds 
	if (acc_c->repeat >= 63) { // 6 bits, so shouldn't be bigger
	    generic_output_set_repeat_count(acc_c->acc_type, num, -1); // infinite repeat
	} else {
	    generic_output_set_repeat_count(acc_c->acc_type, num, acc_c->repeat);
	}
	generic_output_set_on_time(acc_c->acc_type, num, acc_c->on_time);
	generic_output_set_off_time(acc_c->acc_type, num, acc_c->off_time);

	// after configuration, do we activate now?
	switch (acc_c->on_now) {
	    case 1: generic_output_set_state(acc_c->acc_type, num, ON_SOON); break;
	    case 2: generic_output_set_state(acc_c->acc_type, num, ON_IMMEDIATE); break;
	}
	acc_c->request=1;
    }


    // fill request or configure
    if (acc_c->request) {
	// replace existing accessory_conf
	int type = acc_c->acc_type;
	DELAY_PRINTK("Filling accessory request\n");
	memset(acc_c, 0, sizeof(struct accessory_conf)); // clean completely
	acc_c->acc_type = type;// but not too completely

	// fill generic request
	acc_c->exists = num > 0; // num starts as zero, and is found out above
	switch (generic_output_get_state(acc_c->acc_type, num)) {
	    case ON_IMMEDIATE:
	    case ON_SOON:
		acc_c->on_now = 1; // don't differentiate when requesting info
		break;
	    case OFF_IMMEDIATE:
	    case OFF_SOON:
	    default:
		acc_c->on_now = 0; // don't differentiate when requesting info
		break;
	}
	a_mode = generic_output_get_active_on(acc_c->acc_type, num);
	switch (a_mode & (ACTIVE_UP | UNACTIVE_LOWER | ACTIVE_RAISE | UNACTIVE_UP | ACTIVE_LOWER | UNACTIVE_DOWN)) { // match the pieces below only
	    case (ACTIVE_UP | UNACTIVE_LOWER):
		acc_c->on_exp = 1; break;
	    case (ACTIVE_RAISE | UNACTIVE_LOWER):
		acc_c->on_exp = 2; break;
	    case (ACTIVE_RAISE | UNACTIVE_UP | ACTIVE_LOWER | UNACTIVE_DOWN):
		acc_c->on_exp = 3; break;
	}
	if (a_mode & ACTIVE_HIT) {
	    acc_c->on_hit = 1;
	} else if (a_mode & UNACTIVE_HIT) {
	    acc_c->on_hit = 2;
	}
	if (a_mode & ACTIVE_KILL) {
	    acc_c->on_kill = 1;
	} else if (a_mode & UNACTIVE_KILL) {
	    acc_c->on_kill = 2;
	}
	acc_c->on_time = generic_output_get_on_time(acc_c->acc_type, num);
	acc_c->off_time = generic_output_get_off_time(acc_c->acc_type, num);
	acc_c->start_delay = generic_output_get_initial_delay(acc_c->acc_type, num)/500; // convert to half-seconds
	acc_c->repeat_delay = generic_output_get_repeat_delay(acc_c->acc_type, num)/500; // convert to half-seconds
	acc_c->repeat = generic_output_get_repeat_count(acc_c->acc_type, num);

	// fill request based on accessory type
	switch (acc_c->acc_type) {
	    case ACC_SMOKE:
	    case ACC_THERMAL:
	    case ACC_THERMAL_PULSE:
		acc_c->ex_data1 = num;
		break;
	    case ACC_NES_MFS:
		// burst or single mode?
		if (mode == BURST_FIRE) {
		    // burst mode
		    int repeat = generic_output_get_onoff_repeat(acc_c->acc_type, num);
		    DELAY_PRINTK("MFS burst\n");
		    acc_c->ex_data1 = 1;

		    // cram into 8-bits
		    if (repeat == -1) {
			acc_c->ex_data2 = 255;
		    } else if (repeat >= 255) {
			acc_c->ex_data2 = 254;
		    } else if (repeat < 0) {
			acc_c->ex_data2 = 0;
		    } else {
			acc_c->ex_data2 = repeat;
		    }
		} else {
		    DELAY_PRINTK("MFS single\n");
		    // single-fire mode
		    acc_c->ex_data1 = 0;
		}
		break;
	    case ACC_MILES_SDH:
		// TODO -- what to do with MILES data?
		DELAY_PRINTK("Lifter: Couldn't fill in s*** for MILES data %i %i %i\n", num, num2, num3);
		acc_c->ex_data1 = num;
		acc_c->ex_data2 = num2;
		acc_c->ex_data3 = num3;
		break;
	}
	acc_c->request=0; // not a request anymore

	DELAY_PRINTK("Lifter: Returning Accessory data\n");
        if (callMethod){
	    nla_put(skb, ACC_A_MSG, sizeof(struct accessory_conf), acc_c);
        }
	rc = HANDLE_SUCCESS; // we now have a response
    }
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for accessory commands
//---------------------------------------------------------------------------
int nl_accessory_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct accessory_conf thmp = {ACC_THERMAL_PULSE,0,1,1,0,2,0,0,2,9000,005,0,0, 9,0,0,0};
    int lastOnValue = 0;
    struct nlattr *na;
    int rc = HANDLE_SUCCESS_NO_REPLY; // by default this is a command with no response
    struct accessory_conf *acc_c;
    DELAY_PRINTK("Lifter: handling accessory command thermal mode\n");

    // get attribute from message
    na = info->attrs[ACC_A_MSG]; // accessory message
    if (na) {
        // grab value from attribute
        acc_c = (struct accessory_conf*)nla_data(na);
        if (acc_c != NULL) {
                lastOnValue = acc_c->on_now;
SENDUSERCONNMSG("before has_thm_pulse %i, acctype %i on_now %i\n", has_thm_pulse, acc_c->acc_type, lastOnValue);
		rc = do_accessory_configure(1, acc_c, skb);
SENDUSERCONNMSG("after has_thm_pulse %i, acctype %i on_now %i\n", has_thm_pulse, acc_c->acc_type, lastOnValue);
DELAY_PRINTK("has_thm_pulse %i, acctype %i on_now %i\n", has_thm_pulse, acc_c->acc_type, lastOnValue);
		if (has_thm_pulse) {
                    if (acc_c->acc_type == ACC_THERMAL) {
                        if (lastOnValue == 1) {
                            do_accessory_configure(0, &thmp, 0);
                        } else {
                            thmp.on_now = 0;
                            do_accessory_configure(0, &thmp, 0);
                        }
                    }
                }
        }
    }

    // return status
    return rc;
}

/* Kill if we need to */
void do_kill_internal(void) {
	int stay_up = 1;
    int bob_reached = 1;
	u8 kdata;
	if (atomic_read(&hits_to_kill) > 0) {
		stay_up = !atomic_dec_and_test(&kill_counter);
	if (atomic_read(&hits_to_kill) >= 255) { // 255 is never die
		atomic_set(&kill_counter, atomic_read(&hits_to_kill)); // reset kill counter
        }
        bob_reached = !atomic_dec_and_test(&bob_counter);
SENDUSERCONNMSG( "do_kill kill %i bob %i ", kill_counter, bob_counter);
        // Bob after bob counter reached
        if (!bob_reached && stay_up && atomic_read(&after_kill) == 4) {
           enable_battery_check(0); // disable battery checking while motor is on
           set_target_conceal();
           lifter_position_set(LIFTER_POSITION_DOWN); // conceal now 
DELAY_PRINTK("\n***hits_to_bob: %i, bob counter: %i***\n\n", atomic_read(&hits_to_bob), bob_counter);
           atomic_set(&bob_counter, atomic_read(&hits_to_bob)); // reset bob counter
        }
	}
	if (!stay_up) {
		atomic_set(&kill_counter, atomic_read(&hits_to_kill)); // reset kill counter

		// create events for outputs
		generic_output_event(EVENT_KILL);
        //lift_faults(ERR_target_killed);

		// send kill upstream (always, no matter what the upload value is)
		kdata = EVENT_KILL; // cast to 8-bits
		queue_nl_multi(NL_C_EVENT, &kdata, sizeof(kdata));

		switch (atomic_read(&after_kill)) {
			case 0: /* fall */
			case 1: /* kill -- TODO -- find out difference between fall and kill */
                enable_battery_check(0); // disable battery checking while motor is on
				lifter_position_set(LIFTER_POSITION_DOWN); // conceal now
				break;
			case 2: /* stop */
				// TODO -- send stop movement message to mover
				break;
			case 3: /* fall/stop */
				// put down
                enable_battery_check(0); // disable battery checking while motor is on
				lifter_position_set(LIFTER_POSITION_DOWN); // conceal now
				// TODO -- send stop movement message to mover
				break;
			case 4: 
                enable_battery_check(0); // disable battery checking while motor is on
				lifter_position_set(LIFTER_POSITION_DOWN); // conceal now
                break;
		}
	}
}

//---------------------------------------------------------------------------
// netlink command handler for accessory commands
//---------------------------------------------------------------------------
int nl_hit_cal_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int htk = -1;
    int rc = HANDLE_SUCCESS_NO_REPLY; // by default this is a command with no response
    struct hit_calibration *hit_c;
    DELAY_PRINTK("Lifter: handling hit-calibration command\n");

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
                    get_hit_calibration(&hit_c->seperation, &hit_c->sensitivity); // use existing hit_c structure
                    hit_c->blank_time = atomic_read(&blank_time)/100; // convert from milliseconds
                    hit_c->enable_on = atomic_read(&enable_on);
                    hit_c->hits_to_kill = atomic_read(&hits_to_kill);
                    hit_c->hits_to_bob = atomic_read(&hits_to_bob);
                    hit_c->after_kill = atomic_read(&after_kill);
                    hit_c->type = atomic_read(&hit_type);
                    hit_c->invert = get_hit_invert();
                    nla_put(skb, HIT_A_MSG, sizeof(struct hit_calibration), hit_c);
                    rc = HANDLE_SUCCESS; // with return message
                    break;
                case HIT_OVERWRITE_ALL:   /* overwrites every value */
SENDUSERCONNMSG( "HIT_OVERWRITE_ALL  Bobs %i", hit_c->hits_to_bob);
                    set_hit_calibration(hit_c->seperation, hit_c->sensitivity);
                    htk = atomic_read(&hits_to_kill);
                    atomic_set(&hits_to_kill, hit_c->hits_to_kill);
                    atomic_set(&after_kill, hit_c->after_kill);
                    atomic_set(&hits_to_bob, hit_c->hits_to_bob);
                    atomic_set(&bob_type, hit_c->bob_type);
                    atomic_set(&hit_type, hit_c->type);
                    set_hit_invert(hit_c->invert);
                    atomic_set(&blank_time, hit_c->blank_time*100); // convert to milliseconds
                    atomic_set(&enable_on, hit_c->enable_on);
                    atomic_set(&enable_doing, 2); // a calibration, not an action is changing the sensor
                    schedule_work(&hit_enable_work); // fix the hit sensor enabling soon
                    break;
                case HIT_OVERWRITE_CAL:   /* overwrites calibration values (sensitivity, seperation) */
                    set_hit_calibration(hit_c->seperation, hit_c->sensitivity);
                    atomic_set(&blank_time, hit_c->blank_time*100); // convert to milliseconds
                    atomic_set(&enable_on, hit_c->enable_on);
                    atomic_set(&enable_doing, 2); // a calibration, not an action is changing the sensor
                    schedule_work(&hit_enable_work); // fix the hit sensor enabling soon
                    break;
                case HIT_OVERWRITE_OTHER: /* overwrites non-calibration values (type, etc.) */
                    htk = atomic_read(&hits_to_kill);
                    atomic_set(&hits_to_kill, hit_c->hits_to_kill);
                    atomic_set(&hits_to_bob, hit_c->hits_to_bob);
                    atomic_set(&after_kill, hit_c->after_kill);
                    atomic_set(&hit_type, hit_c->type);
                    set_hit_invert(hit_c->invert);
                    break;
                case HIT_OVERWRITE_TYPE: /* overwrites type value only */
                    atomic_set(&hit_type, hit_c->type);
                    set_hit_invert(hit_c->invert);
                    break;
                case HIT_OVERWRITE_KILL:  /* overwrites hits_to_kill value only */
                    htk = atomic_read(&hits_to_kill);
                    atomic_set(&hits_to_kill, hit_c->hits_to_kill);
                    atomic_set(&hits_to_bob, hit_c->hits_to_bob);
                    atomic_set(&after_kill, hit_c->after_kill);
                    break;
            }
            /* if we changed hits to kill see if we need to do kill */
            if (htk > 0 && htk != atomic_read(&hits_to_kill)) {
               int rc = 0;
               static hit_item_t *this;
               spin_lock(hit_lock);
               this = hit_chain;
               while (this != NULL) {
                   rc++; // count hit (doesn't matter which line)
                   this = this->next; // next link in chain
               }
               spin_unlock(hit_lock);

               // only kill if we actually received the correct number of hits
               if (rc >= atomic_read(&hits_to_kill)) {
                  do_kill_internal();
               }
            }
        }
    }

    // return status
    return rc;
}

//---------------------------------------------------------------------------
// Turn blank timer off
//---------------------------------------------------------------------------
static void blank_off(unsigned long data) {
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }

    // turn off hit blanking
SENDUSERCONNMSG( "lifter blank_off blanking off ");
    hit_blanking_off();
}

//---------------------------------------------------------------------------
// Message filler handler for expose functions
//---------------------------------------------------------------------------
int pos_mfh(struct sk_buff *skb, void *pos_data) {
    // the pos_data argument is a pre-made u8 structure
    return nla_put_u8(skb, GEN_INT8_A_MSG, *((int*)pos_data));
}

//---------------------------------------------------------------------------
// Work item to adjust hit sensor enabledness
//---------------------------------------------------------------------------
static void hit_enable_change(struct work_struct * work) {
    int enable_at;
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }

    enable_at = atomic_read(&enable_on);
SENDUSERCONNMSG( "lifter hit_enable_change 0 enable_at=%d=", enable_at);
    switch (atomic_read(&enable_doing)) {
        case 1:
            // new action
            switch (enable_at) {
                case BLANK_ON_CONCEALED:
                case ENABLE_ALWAYS:
                case BLANK_ALWAYS:
                    // nothing
                    break;
                case ENABLE_AT_POSITION:
                    // we're at position, blanking off == sensor enabled
SENDUSERCONNMSG( "lifter hit_enable_change 1 blanking off ");
                    hit_blanking_off();
                    break;
                case DISABLE_AT_POSITION:
                    // we're at position, blanking on == sensor disabled
SENDUSERCONNMSG( "lifter hit_enable_change 2 blanking on ");
                    hit_blanking_on();
                    break;
            }
            break;
        case 2:
            // new calibration
            switch (enable_at) {
                case BLANK_ON_CONCEALED:
                    if (lifter_position_get() == LIFTER_POSITION_DOWN || lifter_position_get() == LIFTER_POSITION_MOVING) {
                        // down, so blank
SENDUSERCONNMSG( "lifter hit_enable_change 3 blanking off ");
                        hit_blanking_on();
                    } else {
                        // not down, don't blank
SENDUSERCONNMSG( "lifter hit_enable_change 4 blanking on ");
                        hit_blanking_off();
                    }
                    break;
                case ENABLE_ALWAYS:
                    // blanking off == sensor enabled




SENDUSERCONNMSG( "lifter hit_enable_change 5 blanking off ");
                    hit_blanking_off();
                    break;
                case BLANK_ALWAYS:
                    // blank always == blank now
SENDUSERCONNMSG( "lifter hit_enable_change 6 blanking on ");
                    hit_blanking_on();
                    break;
                case ENABLE_AT_POSITION:
                case DISABLE_AT_POSITION:
                    // nothing
                    break;
            }
            break;
    }
    atomic_set(&enable_doing, 0);
}

//---------------------------------------------------------------------------
// Work item to notify the user-space about a position change or error
//---------------------------------------------------------------------------
static void position_change(struct work_struct * work) {
    int lifterPosition = LIFTER_POSITION_ERROR_NEITHER;
    u8 pos_data;
    int last_event = atomic_read(&last_lift_event);
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    lifterPosition = lifter_position_get();
    if (lifterPosition >= LIFTER_POSITION_ERROR_NEITHER){
      if (lifterPosition == LIFTER_POSITION_ERROR_BOTH)
         pos_data = ERR_lifter_stuck_at_limit; 
      else
         pos_data = ERR_lifter_stuck_at_limit; 
      
	      // send fault upstream always
	      queue_nl_multi(NL_C_FAULT, &pos_data, sizeof(pos_data));
      return;
    }

    // check for fast up-down-up and down-up-down so we can send an extra netlink message to userspace
    switch (atomic_read(&last_lift_cmd)) {
        case CONCEAL:
            // tried to go down, but came back up?
            if (last_event == EVENT_RAISE || last_event == EVENT_UP) {
                // we will skip sending the "concealed" step below, so do it now
                pos_data = CONCEAL;
                send_nl_message_multi(&pos_data, pos_mfh, NL_C_EXPOSE);
            }
            break;
        case EXPOSE:
            // tried to go up, but went back down?
            if (last_event == EVENT_LOWER || last_event == EVENT_DOWN) {
                // we will skip sending the "exposed" step below, so do it now
                pos_data = EXPOSE;
                send_nl_message_multi(&pos_data, pos_mfh, NL_C_EXPOSE);
            }
            break;
    }

    // notify netlink userspace
    switch (lifterPosition) { // map internal to external values
        case LIFTER_POSITION_DOWN: pos_data = CONCEAL; break;
        case LIFTER_POSITION_UP: pos_data = EXPOSE; break;
        case LIFTER_POSITION_MOVING: pos_data = LIFTING; break;
        default: pos_data = EXPOSURE_REQ; break; //error
    }
    send_nl_message_multi(&pos_data, pos_mfh, NL_C_EXPOSE);
}


//---------------------------------------------------------------------------
// event handler for lifts
//---------------------------------------------------------------------------
void lift_event(int etype) {
    lift_event_internal(etype, true); // send event upstream
}

void lift_event_internal(int etype, bool upload) {
    int enable_at = atomic_read(&enable_on);
SENDUSERCONNMSG( "lifter lift_event_internal enable_at=%d=", enable_at);
    DELAY_PRINTK("lift_event(%i)\n", etype);

	// send event upstream?
	if (upload) {
		u8 data = etype; // cast to 8-bits
		queue_nl_multi(NL_C_EVENT, &data, sizeof(data));
	}

	// create event for outputs
	generic_output_event(etype);

	// notify user-space
	switch (etype) {
        case EVENT_ERROR:
            enable_battery_check(1); // enable battery checking while motor is off
            // fall through
		case EVENT_RAISE:
		case EVENT_UP:
            if (atomic_read(&last_lift_cmd) == CONCEAL) {
                // remember going the wrong way
                atomic_set(&last_lift_event, etype);
            }
			schedule_work(&position_work);
            break;
		case EVENT_LOWER:
		case EVENT_DOWN:
            if (atomic_read(&last_lift_cmd) == EXPOSE) {
                // remember going the wrong way
                atomic_set(&last_lift_event, etype);
            }
			schedule_work(&position_work);
			break;
	}

	// disable or enable hit sensor on raise and lower events
	switch (etype) {
		case EVENT_UP:
		case EVENT_DOWN:
            enable_battery_check(1); // enable battery checking while motor is off
			switch (enable_at) {
				case ENABLE_ALWAYS:
					// we never blank
SENDUSERCONNMSG( "lifter lift_event_internal 1 blanking off ");
					hit_blanking_off();
					break;
				case ENABLE_AT_POSITION:
				case DISABLE_AT_POSITION:
					// we reached a new position; change hit sensor enabled state
					atomic_set(&enable_doing, 1); // an action, not a calibration is changing the sensor
					schedule_work(&hit_enable_work);
					break;
				case BLANK_ALWAYS:
					// we always blank
SENDUSERCONNMSG( "lifter lift_event_internal 2 blanking on ");
					hit_blanking_on();
					break;
				case BLANK_ON_CONCEALED:
					if (etype == EVENT_DOWN) {
						// we're down; blank
SENDUSERCONNMSG( "lifter lift_event_internal 3 blanking on ");
						hit_blanking_on();
					}
					break;
			}
		   	break;
        case EVENT_LOWER:
            if (enable_at == BLANK_ON_CONCEALED) {
			   // blank when lowering
SENDUSERCONNMSG( "lifter lift_event_internal 4 blanking on ");
               hit_blanking_on();
            }
            break;
		case EVENT_RAISE:
			if (enable_at == BLANK_ON_CONCEALED) {
				// we're not concealed, blank a little longer, or stop blanking now
				if (atomic_read(&blank_time) == 0) { // no blanking time
SENDUSERCONNMSG( "lifter lift_event_internal 5 blanking off ");
					hit_blanking_off();
				} else {
					mod_timer(&blank_timer, jiffies+((atomic_read(&blank_time)*HZ)/1000)); // blank for X milliseconds
				}
			}
			break;
	}

	// reset kill counter on start of raise
	if (etype == EVENT_RAISE) {
        //if (atomic_read(&after_kill) != 5) {  // not set to fasit bob
        if (atomic_read(&after_kill) != 4) {
		   atomic_set(&kill_counter, atomic_read(&hits_to_kill)); // reset kill counter
           atomic_set(&bob_counter, atomic_read(&hits_to_bob)); // reset bob counter
        }
	}

	// do bob?
#if 0
	if (atomic_read(&at_conceal) == 1) {
		switch (etype) {
			case EVENT_HIT:
			case EVENT_KILL:
			case EVENT_LOWER:
				// ignore
				break;
			case EVENT_DOWN:
				// bob after we went down
				atomic_set(&at_conceal, 0); // reset to do nothing
            enable_battery_check(0); // disable battery checking while motor is on
				lifter_position_set(LIFTER_POSITION_UP); // expose now
				break;
			default:
				// everything else implies something went wrong
				atomic_set(&at_conceal, 0); // reset to do nothing
				break;
		}
	}
#endif

}

//---------------------------------------------------------------------------
// Message filler handler for magnitude
//---------------------------------------------------------------------------
static int mag_mfh(struct sk_buff *skb, void *mag_data) {
    // the mag_data argument is a pre-made u16 structure
    return nla_put_u16(skb, GEN_INT16_A_MSG, *((u16*)mag_data));
}

//---------------------------------------------------------------------------
// handler for magnitude
//---------------------------------------------------------------------------
void magnitude_event(int mag) {
    u16 magnitude;
    magnitude = mag; // message is unsigned, fix it be "signed"
SENDUSERCONNMSG( "randy magnitude event %i", magnitude );
    send_nl_message_multi(&magnitude, mag_mfh, NL_C_MAGNITUDE);
}

//---------------------------------------------------------------------------
// event handler for hits
//---------------------------------------------------------------------------
void hit_event(int line) {
    hit_event_internal(line, true); // send hit upstream
}

void hit_event_internal(int line, bool upload) {
	struct hit_item *new_hit;
	u8 hits = 0;
	u8 data = EVENT_HIT; // cast to 8-bits
	DELAY_PRINTK("hit_event_internal(line=%i, upload=%d)\n", line,upload);

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
		new_hit->next = hit_chain;
		hit_chain = new_hit;
		spin_unlock(hit_lock);

		// set values in new hit item
		new_hit->time = timespec_sub(current_kernel_time(), hit_start); // time since start
		new_hit->line = line;
	}

	// create netlink message for userspace (always, no matter what the upload value is)
	// get data from log
	spin_lock(hit_lock);
	new_hit = hit_chain;
	while (new_hit != NULL) {
		hits++; // count hit (doesn't matter which line)
		new_hit = new_hit->next; // next link in chain
	}
	spin_unlock(hit_lock);

	// send hits upstream
   if (upload) {
      queue_nl_multi(NL_C_HITS, &hits, sizeof(hits));
   }

	// go down if we need to go down
   do_kill_internal();
}

void disconnected_hit_sensor_event(int disconnected) {
	u8 fault = 0;

   if (disconnected) {
      fault = ERR_hit_sensor_failure; 
	   // send fault upstream always
	   // Only send disconnects
	   queue_nl_multi(NL_C_FAULT, &fault, sizeof(fault));
   }
}

//---------------------------------------------------------------------------
// netlink command handler for event commands
//---------------------------------------------------------------------------
int nl_event_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
    u8 data = BATTERY_SHUTDOWN; // in case we need to shutdown
    DELAY_PRINTK("Lifter: handling event command\n");

    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na); // value is ignored
        DELAY_PRINTK("Lifter: received value: %i, nl_event_handler\n", value);

        // handle event
        switch (value) {
            case EVENT_WAKE:
                // lifters handle as nl command and nl event
                lifter_sleep_set(0);
                break;
            case EVENT_SLEEP:
                // lifters handle as nl command and nl event
                lifter_sleep_set(1);
                break;
            case EVENT_HIT:
                hit_event_internal(0, false); // don't repropogate
                break;
            case EVENT_SHUTDOWN:
                // needs to be converted to NL_C_SHUTDOWN from userspace, so send it back up (and send onwards to other attached devices)
                queue_nl_multi(NL_C_BATTERY, &data, sizeof(data));
                break;
            default:
                lift_event_internal(value, false); // don't repropogate
                break;
        }

        rc = HANDLE_SUCCESS_NO_REPLY;
    } else {
        DELAY_PRINTK("Lifter: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
    DELAY_PRINTK("Lifter: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for sleep commands
//---------------------------------------------------------------------------
int nl_sleep_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc;
    u8 data = 0;
    DELAY_PRINTK("Mover: handling sleep command\n");

    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        data = nla_get_u8(na); // value is ignored
        DELAY_PRINTK("Mover: received value: %i\n", data);

        if (data != SLEEP_REQUEST) {
            // handle sleep/wake in hw driver
            lifter_sleep_set(data==SLEEP_COMMAND?1:0);
            // lifters don't propogate message

            rc = HANDLE_SUCCESS_NO_REPLY;
        } else {
            // retrieve sleep status
            rc = nla_put_u8(skb, GEN_INT8_A_MSG, lifter_sleep_get()?SLEEP_COMMAND:WAKE_COMMAND);

            // message creation success?
            if (rc == 0) {
                rc = HANDLE_SUCCESS;
            } else {
                DELAY_PRINTK("Mover: could not create return message\n");
                rc = HANDLE_FAILURE;
            }
        }
    } else {
        DELAY_PRINTK("Mover: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
    DELAY_PRINTK("Mover: returning rc: %i\n", rc);

    return rc;
}

static void init_bes_triggers(int besLevel){
    int mode;
    int shots;

                                              //    r,e,o,o,p,o,o,p,    o, o,s,r,r,e,e,e;
                                              //    e,x,n,n,a,n,n,a,    n, f,t,e,e,x,x,x;
                                              //    q,i, , ,d, , ,d,     , f,a,p,p, , , ;
                                              //    u,s,n,e,1,h,k,2,    t,  ,r,e,e,d,d,d;
                                              //    e,t,o,x, ,i,i, ,    i, t,t,a,a,a,a,a;
                                              //    s,s,w,p, ,t,l, ,    m, i, ,t,t,t,t,t;
                                              //    t, , , , , ,l, ,    e, m,d, , ,a,a,a;
                                              //     , , , , , , , ,     , e,e,d, ,1,2,3;
                                              //     , , , , , , , ,     ,  ,l,e, , , , ;
                                              //     , , , , , , , ,     ,  ,a,l, , , , ;
                                              //     , , , , , , , ,     ,  ,y,a, , , , ;
                                              //     , , , , , , , ,     ,  , ,y, , , , ;
    struct accessory_conf bt1 = {ACC_BES_TRIGGER_1 ,0,1,0,0,2,1,0,2,  250, 0,0,0,0,0,0,0};
    struct accessory_conf bt2 = {ACC_BES_TRIGGER_2 ,0,1,0,2,2,0,0,2,  250, 0,0,0,0,0,0,0};
    struct accessory_conf bt3 = {ACC_BES_TRIGGER_3 ,0,1,0,2,2,0,0,2,  250, 0,0,0,0,0,0,0};
    struct accessory_conf bt4 = {ACC_BES_TRIGGER_4 ,0,1,0,2,2,0,0,2,  250, 0,0,0,0,0,0,0};

    mode = get_eeprom_int_value(BES_MODE, BES_MODE_LOC, BES_MODE_SIZE);
    bt1.on_exp = get_eeprom_int_value(BT1_ACTIVATE_EXPOSE, BT1_ACTIVATE_EXPOSE_LOC, BT1_ACTIVATE_EXPOSE_SIZE);
    if (bt1.on_exp > 1) bt1.on_exp = 1;
    bt1.on_hit = get_eeprom_int_value(BT1_ACTIVATE_ON_HIT, BT1_ACTIVATE_ON_HIT_LOC, BT1_ACTIVATE_ON_HIT_SIZE);
    bt1.on_kill = get_eeprom_int_value(BT1_ACTIVATE_ON_KILL, BT1_ACTIVATE_ON_KILL_LOC, BT1_ACTIVATE_ON_KILL_SIZE);
    bt1.repeat = get_eeprom_int_value(BT1_EX3, BT1_EX3_LOC, BT1_EX3_SIZE);
// We only repeat if number of shots greater than 1.
// No repeat will give 1 shot, 1 repeat will be 2
    if (bt1.repeat > 1){
        bt1.repeat -= 1;
        bt1.repeat_delay = 3;
    } else {
        bt1.repeat = 0;
    }
    do_accessory_configure(0, &bt1, 0);
    if (besLevel > 1){
            bt2.on_exp = get_eeprom_int_value(BT2_ACTIVATE_EXPOSE, BT2_ACTIVATE_EXPOSE_LOC, BT2_ACTIVATE_EXPOSE_SIZE);
            if (bt2.on_exp > 1) bt2.on_exp = 1;
            bt2.on_hit = get_eeprom_int_value(BT2_ACTIVATE_ON_HIT, BT2_ACTIVATE_ON_HIT_LOC, BT2_ACTIVATE_ON_HIT_SIZE);
            bt2.on_kill = get_eeprom_int_value(BT2_ACTIVATE_ON_KILL, BT2_ACTIVATE_ON_KILL_LOC, BT2_ACTIVATE_ON_KILL_SIZE);
            bt2.repeat = get_eeprom_int_value(BT2_EX3, BT2_EX3_LOC, BT2_EX3_SIZE);
            if (bt2.repeat > 1){
        	bt2.repeat -= 1;
        	bt2.repeat_delay = 3;
            } else {
        	bt2.repeat = 0;
            }
        if (besLevel == 3){
            if (bt2.repeat) {
// In this BES mode we need 2 triggers per fire event so if we need
// more than one shot we need to repeat more times.
                bt2.repeat = (2 * (bt2.repeat + 1)) - 1;
            } else {
                bt2.repeat = 1;
            }
            bt2.repeat_delay = 3;
        }
	do_accessory_configure(0, &bt2, 0);
        if (besLevel == 2 || besLevel == 5){
            bt3.on_exp = get_eeprom_int_value(BT3_ACTIVATE_EXPOSE, BT3_ACTIVATE_EXPOSE_LOC, BT3_ACTIVATE_EXPOSE_SIZE);
            if (bt3.on_exp > 1) bt3.on_exp = 1;
            bt3.on_hit = get_eeprom_int_value(BT3_ACTIVATE_ON_HIT, BT3_ACTIVATE_ON_HIT_LOC, BT3_ACTIVATE_ON_HIT_SIZE);
            bt3.on_kill = get_eeprom_int_value(BT3_ACTIVATE_ON_KILL, BT3_ACTIVATE_ON_KILL_LOC, BT3_ACTIVATE_ON_KILL_SIZE);
            bt3.repeat = get_eeprom_int_value(BT3_EX3, BT3_EX3_LOC, BT3_EX3_SIZE);
            if (bt3.repeat > 1){
        	bt3.repeat -= 1;
        	bt3.repeat_delay = 3;
            } else {
        	bt3.repeat = 0;
            }
	    do_accessory_configure(0, &bt3, 0);
        }
        if (besLevel == 5){
            bt4.on_exp = get_eeprom_int_value(BT4_ACTIVATE_EXPOSE, BT4_ACTIVATE_EXPOSE_LOC, BT4_ACTIVATE_EXPOSE_SIZE);
            if (bt4.on_exp > 1) bt4.on_exp = 1;
            bt4.on_hit = get_eeprom_int_value(BT4_ACTIVATE_ON_HIT, BT4_ACTIVATE_ON_HIT_LOC, BT4_ACTIVATE_ON_HIT_SIZE);
            bt4.on_kill = get_eeprom_int_value(BT4_ACTIVATE_ON_KILL, BT4_ACTIVATE_ON_KILL_LOC, BT4_ACTIVATE_ON_KILL_SIZE);
            bt4.repeat = get_eeprom_int_value(BT4_EX3, BT4_EX3_LOC, BT4_EX3_SIZE);
            if (bt4.repeat > 1){
        	bt4.repeat -= 1;
        	bt4.repeat_delay = 3;
            } else {
        	bt4.repeat = 0;
            }
	    do_accessory_configure(0, &bt4, 0);
        }
    }
}

static void init_thermal_pulse(){
    int mode;
    int shots;

    struct accessory_conf thmp = {ACC_THERMAL_PULSE,0,1,0,5,2.0,0,2, 9000,100,0,0, 9,0,0,0};
    do_accessory_configure(0, &thmp, 0);
}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init Lifter_init(void) {
    int retval = 0, d_id;
    struct driver_command commands[] = {
        {NL_C_EXPOSE,    nl_expose_handler},
        {NL_C_STOP,      nl_stop_handler},
        {NL_C_HITS,      nl_hits_handler},
        {NL_C_HIT_LOG,   nl_hit_log_handler},
        {NL_C_ACCESSORY, nl_accessory_handler},
        {NL_C_HIT_CAL,   nl_hit_cal_handler},
        {NL_C_EVENT,     nl_event_handler},
        {NL_C_SLEEP,     nl_sleep_handler},
    };
    struct nl_driver driver = {NULL, commands, sizeof(commands)/sizeof(struct driver_command), NULL}; // no heartbeat object, X command in list, no identifying data structure

    // install driver w/ netlink provider
    d_id = install_nl_driver(&driver);
    DELAY_PRINTK("%s(): %s - %s : %i\n",__func__,  __DATE__, __TIME__, d_id);
    atomic_set(&driver_id, d_id);

    if (has_bes > 0 && has_bes < 6){
        init_bes_triggers(has_bes);
    }
    if (has_thm_pulse){
        init_thermal_pulse();
    }
    // reset hit log start time
    hit_start = current_kernel_time();

    // set callback handlers
    set_hit_callback(hit_event, disconnected_hit_sensor_event, magnitude_event);
    set_lift_callback(lift_event, lift_faults);

    INIT_WORK(&position_work, position_change);
    INIT_WORK(&hit_enable_work, hit_enable_change);

    atomic_set(&hits_to_kill, has_hits_to_kill);
    atomic_set(&hits_to_bob, has_hits_to_bob);
    atomic_set(&kill_counter, atomic_read(&hits_to_kill)); // reset kill counter
    atomic_set(&bob_counter, atomic_read(&hits_to_bob)); // reset bob counter
    atomic_set(&after_kill, has_kill_reaction);

    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
    return retval;
}

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit Lifter_exit(void) {
    atomic_set(&full_init, FALSE);
    uninstall_nl_driver(atomic_read(&driver_id));
    ati_flush_work(&position_work); // close any open work queue items
    ati_flush_work(&hit_enable_work); // close any open work queue items
}


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(Lifter_init);
module_exit(Lifter_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI Lifter module");
MODULE_AUTHOR("ndb");

