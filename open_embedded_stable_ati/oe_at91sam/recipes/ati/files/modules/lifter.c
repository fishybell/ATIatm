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

//---------------------------------------------------------------------------
#define TARGET_NAME		"lifter"

//---------------------------------------------------------------------------
// These variables are parameters given when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
static int has_miles = FALSE;				// has MILES hit sensor
module_param(has_miles, bool, S_IRUGO);
static int has_hitX = FALSE;				// has X mechanical hit sensors
module_param(has_hitX, int, S_IRUGO);
static int has_engine = FALSE;				// has engine mechanical hit sensor
module_param(has_engine, bool, S_IRUGO);
static int has_wheelX = FALSE;				// has wheel X mechanical hit sensors
module_param(has_wheelX, int, S_IRUGO);
static int has_turret = FALSE;				// has turret hit sensor
module_param(has_turret, bool, S_IRUGO);

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
// This delayed work queue item is used to notify user-space of a position
// change or error detected by the IRQs or the timeout timer.
//---------------------------------------------------------------------------
static struct work_struct position_work;

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
atomic_t hits_to_fall = ATOMIC_INIT(0); // infinite hits to fall
atomic_t fall_counter = ATOMIC_INIT(-1); // invalid hit count for hits_to_fall
atomic_t hit_type = ATOMIC_INIT(0); // mechanical
atomic_t after_fall = ATOMIC_INIT(0); // stay down on fall
static struct timer_list fall_timer; // TODO -- stay down for a little while?
atomic_t blank_time = ATOMIC_INIT(0); // no blanking time
static void blank_off(unsigned long data); // forward declaration
static struct timer_list blank_timer = TIMER_INITIALIZER(blank_off, 0, 0);
atomic_t at_conceal = ATOMIC_INIT(0); // do nothing when conceald

//---------------------------------------------------------------------------
// netlink command handler for expose commands
//---------------------------------------------------------------------------
int nl_expose_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
delay_printk("Lifter: handling expose command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na);
delay_printk("Lifter: received value: %i\n", value);

        switch (value) {
            case TOGGLE:
                // grab current position
                if (lifter_position_get() == LIFTER_POSITION_UP) {
                    // was up, go down
                    lifter_position_set(LIFTER_POSITION_DOWN); // conceal now
                    atomic_set(&toggle_last, CONCEAL); // remember toggle direction
                } else if (lifter_position_get() == LIFTER_POSITION_DOWN) {
                    // was down, go up
                    lifter_position_set(LIFTER_POSITION_UP); // expose now
                    atomic_set(&toggle_last, EXPOSE); // remember toggle direction
                } else { // moving or error
                    // otherwise go opposite of last direction
                    if (atomic_read(&toggle_last) == EXPOSE) {
                        // went up last time, go down now
                        lifter_position_set(LIFTER_POSITION_DOWN); // conceal now
                        atomic_set(&toggle_last, CONCEAL); // remember toggle direction
                    } else { // assume conceal last
                        // went down last time, go up now
                        lifter_position_set(LIFTER_POSITION_UP); // expose now
                        atomic_set(&toggle_last, EXPOSE); // remember toggle direction
                    }
                }
                rc = -1; // we'll be going later
                break;

            case EXPOSE:
                // do expose
                if (lifter_position_get() != LIFTER_POSITION_UP) {
                    lifter_position_set(LIFTER_POSITION_UP); // expose now
                    rc = -1; // we'll be going later
                } else {
                    rc = EXPOSE; // we're already there
                }
                break;

            case CONCEAL:
                // do conceal
                if (lifter_position_get() != LIFTER_POSITION_DOWN) {
                    lifter_position_set(LIFTER_POSITION_DOWN); // conceal now
                    rc = -1; // we'll be going later
                } else {
                    rc = CONCEAL; // we're already there
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
            delay_printk("Lifter: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        delay_printk("Lifter: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Lifter: returning rc: %i\n", rc);

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
delay_printk("Lifter: handling hits command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na);
delay_printk("Lifter: received value: %i\n", value);

        // reset hit log?
        if (value != HIT_REQ) {
            spin_lock(hit_lock);
            this = hit_chain;
            while (this != NULL) {
                hit_chain = this; // remember this
                kfree(hit_chain); // free it
                this = this->next; // move on to next link
            }
            hit_chain = NULL;
            spin_unlock(hit_lock);
            hit_start = current_kernel_time(); // reset hit log start time
        }

        // get data from log
        rc = 0;
        spin_lock(hit_lock);
        this = hit_chain;
        while (this != NULL) {
            rc++; // count hit (doesn't matter which line)
            this = this->next; // next link in chain
        }
        spin_unlock(hit_lock);
        
        // prepare response
        rc = nla_put_u8(skb, GEN_INT8_A_MSG, rc); // rc is number of hits

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
            delay_printk("Lifter: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        delay_printk("Lifter: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Lifter: returning rc: %i\n", rc);

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
delay_printk("Lifter: handling hits command\n");
    
    // get attribute from message
    na = info->attrs[GEN_STRING_A_MSG]; // generic string message
    if (na) {
        // get count from log
        rc = 0;
        spin_lock(hit_lock);
        this = hit_chain;
        while (this != NULL) {
            rc++; // count hit (doesn't matter which line)
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
            delay_printk("Lifter: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        delay_printk("Lifter: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Lifter: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for stop commands
//---------------------------------------------------------------------------
int nl_stop_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
delay_printk("Lifter: handling stop command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na); // value is ignored
delay_printk("Lifter: received value: %i\n", value);

        // stop motor wherever it is
        lifter_position_set(LIFTER_POSITION_ERROR_NEITHER);

        // TODO -- disable hit sensor (but don't clear hit log)

        // Stop accessories (will disable them as well)
        generic_output_event(EVENT_ERROR);

        // prepare response
        rc = nla_put_u8(skb, GEN_INT8_A_MSG, 1); // value is ignored

        // message creation success?
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
            delay_printk("Lifter: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
        delay_printk("Lifter: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Lifter: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for accessory commands
//---------------------------------------------------------------------------
int nl_accessory_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc = HANDLE_SUCCESS_NO_REPLY; // by default this is a command with no response
    struct accessory_conf *acc_c;
delay_printk("Lifter: handling accessory command\n");
    
    // get attribute from message
    na = info->attrs[ACC_A_MSG]; // accessory message
    if (na) {
        // grab value from attribute
        acc_c = (struct accessory_conf*)nla_data(na);
        if (acc_c != NULL) {

            // prepare mode and active mode value for later
            int a_mode = 0, mode = CONSTANT_ON; // may be overwritten depending on accessory type
            int i, num = 0;
//delay_printk("Q X%iX %i %i %i %i %i %i %i %i %i %i %i %i %i\n", acc_c->acc_type, acc_c->exists, acc_c->on_now, acc_c->on_exp, acc_c->on_hit, acc_c->on_kill, acc_c->on_time, acc_c->off_time, acc_c->start_delay, acc_c->repeat_delay, acc_c->repeat, acc_c->ex_data1, acc_c->ex_data2, acc_c->ex_data3);
            switch (acc_c->on_exp) {
                case 1: a_mode |= ACTIVE_UP | UNACTIVE_LOWER; break; // active when fully exposed only
                case 2: a_mode |= ACTIVE_RAISE | UNACTIVE_LOWER; break; // active when partially and fully expose
                case 3: a_mode |= ACTIVE_RAISE | UNACTIVE_UP | ACTIVE_LOWER | UNACTIVE_DOWN; break; // active on transition
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
                case ACC_MILES_SDH :
                case ACC_SES :
                case ACC_NES_MFS :
                case ACC_NES_PHI :
                case ACC_NES_MOON_GLOW :
                    for (i=generic_output_exists(acc_c->acc_type); i > 0; i--) { // do I have one?
                        delay_printk("Lifter: Using Single Accessory %i\n", acc_c->acc_type);
                        num = i; // use this one
                    }
                    break;
                case ACC_THERMAL :
                case ACC_SMOKE :
                    if (generic_output_exists(acc_c->acc_type) >= acc_c->ex_data1) { // do I have this one? (ex_data1 is thermal #)
                        delay_printk("Lifter: Using Multiple Accessory %i:%i\n", acc_c->acc_type, acc_c->ex_data1);
                        num = acc_c->ex_data1; // use this one
                    }
                    break;
                default :
                    delay_printk("Lifter: bad accessory type: %s\n", acc_c->acc_type);
                    break;
            }

            // fill request or configure
            if (acc_c->request) {
                // replace existing accessory_conf
                int type = acc_c->acc_type;
delay_printk("Filling accessory request\n");
                memset(acc_c, 0, sizeof(struct accessory_conf)); // clean completely
                acc_c->acc_type = type; // but not too completely

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
                        acc_c->ex_data1 = num;
                        break;
                    case ACC_NES_MFS:
                        // burst or single mode?
                        if (mode == BURST_FIRE) {
                            // burst mode
                            int repeat = generic_output_get_onoff_repeat(acc_c->acc_type, num);
delay_printk("MFS burst\n");
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
delay_printk("MFS single\n");
                            // single-fire mode
                            acc_c->ex_data1 = 0;
                        }
                        break;
                    case ACC_MILES_SDH:
                        // TODO -- what to do with MILES data?
                        delay_printk("Lifter: Couldn't fill in s*** for MILES data\n");
                        break;
                }

                delay_printk("Lifter: Returning Accessory data\n");
                nla_put(skb, ACC_A_MSG, sizeof(struct accessory_conf), acc_c);
                rc = HANDLE_SUCCESS; // we now have a response
            } else {
delay_printk("Doing accessory configure\n");
                // configure based on accessory type
                switch (acc_c->acc_type) {
                    case ACC_NES_MFS:
                        // burst or single mode?
                        if (acc_c->ex_data1) {
                            // burst mode
                            if (acc_c->ex_data2 >= 255) { // 8-bits, so shouldn't be bigger
delay_printk("MFS burst infinite repeat\n");
                                generic_output_set_onoff_repeat(acc_c->acc_type, num, -1); // infinite repeat
                            } else {
delay_printk("MFS burst repeat: %i\n", acc_c->ex_data2);
                                generic_output_set_onoff_repeat(acc_c->acc_type, num, acc_c->ex_data2);
                            }
                            mode = BURST_FIRE;
                        } else {
delay_printk("MFS not bursting\n");
                            mode = TEMP_ON;
                        }
                        break;
                    case ACC_MILES_SDH:
                        // TODO -- what to do with MILES data?
                        delay_printk("Lifter: Couldn't do s*** with MILES data: %i %i %i\n", acc_c->ex_data1, acc_c->ex_data2, acc_c->ex_data3);
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
            }
        }
    }

    // return status
    return rc;
}

//---------------------------------------------------------------------------
// netlink command handler for accessory commands
//---------------------------------------------------------------------------
int nl_hit_cal_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc = HANDLE_SUCCESS_NO_REPLY; // by default this is a command with no response
    struct hit_calibration *hit_c;
delay_printk("Lifter: handling hit-calibration command\n");
    
    // get attribute from message
    na = info->attrs[HIT_A_MSG]; // accessory message
    if (na) {
        // grab value from attribute
        hit_c = (struct hit_calibration*)nla_data(na);
        if (hit_c != NULL) {
            switch (hit_c->set) {
                case HIT_GET_CAL:        /* overwrites nothing (gets calibration values) */
                case HIT_GET_TYPE:       /* overwrites nothing (gets type value) */
                case HIT_GET_FALL:       /* overwrites nothing (gets hits_to_fall value) */
                case HIT_OVERWRITE_NONE: /* overwrite nothing (gets reply with current values) */
                    // fill in all data, the unused portions will be ignored
                    get_hit_calibration(&hit_c->seperation, &hit_c->sensitivity); // use existing hit_c structure
                    hit_c->blank_time = atomic_read(&blank_time);
                    hit_c->hits_to_fall = atomic_read(&hits_to_fall);
                    hit_c->after_fall = atomic_read(&after_fall);
                    hit_c->type = atomic_read(&hit_type);
                    hit_c->invert = get_hit_invert();
                    nla_put(skb, HIT_A_MSG, sizeof(struct hit_calibration), hit_c);
                    rc = HANDLE_SUCCESS; // with return message
                    break;
                case HIT_OVERWRITE_ALL:   /* overwrites every value */
                    set_hit_calibration(hit_c->seperation, hit_c->sensitivity);
                    atomic_set(&hits_to_fall, hit_c->hits_to_fall);
                    atomic_set(&after_fall, hit_c->after_fall);
                    atomic_set(&hit_type, hit_c->type);
                    set_hit_invert(hit_c->invert);
                    atomic_set(&blank_time, hit_c->blank_time);
                    atomic_set(&fall_counter, hit_c->hits_to_fall); // reset fall counter
                    break;
                case HIT_OVERWRITE_CAL:   /* overwrites calibration values (sensitivity, seperation) */
                    set_hit_calibration(hit_c->seperation, hit_c->sensitivity);
                    atomic_set(&blank_time, hit_c->blank_time);
                    break;
                case HIT_OVERWRITE_OTHER: /* overwrites non-calibration values (type, etc.) */
                    atomic_set(&hits_to_fall, hit_c->hits_to_fall);
                    atomic_set(&after_fall, hit_c->after_fall);
                    atomic_set(&hit_type, hit_c->type);
                    set_hit_invert(hit_c->invert);
                    atomic_set(&fall_counter, hit_c->hits_to_fall); // reset fall counter
                    break;
                case HIT_OVERWRITE_TYPE: /* overwrites type value only */
                    atomic_set(&hit_type, hit_c->type);
                    set_hit_invert(hit_c->invert);
                    break;
                case HIT_OVERWRITE_FALL:  /* overwrites hits_to_fall value only */
                    atomic_set(&hits_to_fall, hit_c->hits_to_fall);
                    atomic_set(&after_fall, hit_c->after_fall);
                    atomic_set(&fall_counter, hit_c->hits_to_fall); // reset fall counter
                    break;
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
// Work item to notify the user-space about a position change or error
//---------------------------------------------------------------------------
static void position_change(struct work_struct * work) {
    u8 pos_data;
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    // notify netlink userspace
    switch (lifter_position_get()) { // map internal to external values
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
void lift_event_internal(int etype, bool upload); // forward declaration
void lift_event(int etype) {
    lift_event_internal(etype, true); // send event upstream
}

void lift_event_internal(int etype, bool upload) {
    delay_printk("lift_event(%i)\n", etype);

    // send event upstream?
    if (upload) {
        u8 data = etype; // cast to 8-bits
        queue_nl_multi(NL_C_EVENT, &data, sizeof(data));
    }

    // create event for outputs
    generic_output_event(etype);

    // notify user-space
    switch (etype) {
        case EVENT_RAISE:
        case EVENT_LOWER:
        case EVENT_UP:
        case EVENT_DOWN:
        case EVENT_ERROR:
            schedule_work(&position_work);
            break;
    }

    // disable or enable hit sensor on raise and lower events
    switch (etype) {
        case EVENT_LOWER:
            // TODO -- make this optional?
            hit_blanking_on();
            break;
        case EVENT_RAISE:
            if (atomic_read(&blank_time) == 0) { // no blanking time
                hit_blanking_off();
            } else {
                mod_timer(&blank_timer, jiffies+((atomic_read(&blank_time)*HZ)/1000)); // blank for X milliseconds
            }
    }

    // reset fall counter on start of raise
    if (etype == EVENT_RAISE) {
        atomic_set(&fall_counter, atomic_read(&hits_to_fall)); // reset fall counter
    }

    // do bob?
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
                lifter_position_set(LIFTER_POSITION_UP); // expose now
                break;
            default:
                // everything else implies something went wrong
                atomic_set(&at_conceal, 0); // reset to do nothing
                break;
        }
    }
}

//---------------------------------------------------------------------------
// event handler for hits
//---------------------------------------------------------------------------
void hit_event_internal(int line, bool upload); // forward declaration
void hit_event(int line) {
   hit_event_internal(line, true); // send hit upstream
}

void hit_event_internal(int line, bool upload) {
    struct hit_item *new_hit;
    int stay_up = 1;
    u8 hits = 0, kdata;
    delay_printk("hit_event(%i)\n", line);

    // create event
    lift_event_internal(EVENT_HIT, upload);

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
    queue_nl_multi(NL_C_HITS, &hits, sizeof(hits));

    // go down if we need to go down
    if (atomic_read(&hits_to_fall) > 0) {
        stay_up = !atomic_dec_and_test(&fall_counter);
    }
    if (!stay_up) {
        // put down
        atomic_set(&fall_counter, atomic_read(&hits_to_fall)); // reset fall counter
        lifter_position_set(LIFTER_POSITION_DOWN); // conceal now

        // create events for outputs
        generic_output_event(EVENT_KILL);

        // send kill upstream (always, no matter what the upload value is)
        kdata = EVENT_KILL; // cast to 8-bits
        queue_nl_multi(NL_C_EVENT, &kdata, sizeof(kdata));
        
        // bob if we need to bob
        switch (atomic_read(&after_fall)) {
            case 0: /* stay down */
                // do nothing
                break;
            case 1: /* bob */
                atomic_set(&at_conceal, 1); // when we get a CONCEAL event, go back up
                break;
            case 2: /* bob/stop */
                // TODO -- send stop movement message to mover
                break;
            case 3: /* stop */
                // TODO -- send stop movement message to mover
                break;
        }
    }
}

//---------------------------------------------------------------------------
// netlink command handler for event commands
//---------------------------------------------------------------------------
int nl_event_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
delay_printk("Lifter: handling event command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na); // value is ignored
delay_printk("Lifter: received value: %i\n", value);

        // handle event
        if (value == EVENT_HIT) {
            hit_event_internal(0, false); // don't repropogate
        } else {
            lift_event_internal(value, false); // don't repropogate
        }

        rc = HANDLE_SUCCESS_NO_REPLY;
    } else {
        delay_printk("Lifter: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
delay_printk("Lifter: returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
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
    };
    struct nl_driver driver = {NULL, commands, sizeof(commands)/sizeof(struct driver_command), NULL}; // no heartbeat object, X command in list, no identifying data structure

    // install driver w/ netlink provider
    d_id = install_nl_driver(&driver);
delay_printk("%s(): %s - %s : %i\n",__func__,  __DATE__, __TIME__, d_id);
    atomic_set(&driver_id, d_id);

    // reset hit log start time
    hit_start = current_kernel_time();

    // set callback handlers
    set_hit_callback(hit_event);
    set_lift_callback(lift_event);

    INIT_WORK(&position_work, position_change);

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
}


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(Lifter_init);
module_exit(Lifter_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI Lifter module");
MODULE_AUTHOR("ndb");

