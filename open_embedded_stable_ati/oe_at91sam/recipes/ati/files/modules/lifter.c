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
                rc = LIFTING; // we're always going
                break;

            case EXPOSE:
                // do expose
                if (lifter_position_get() != LIFTER_POSITION_UP) {
                    lifter_position_set(LIFTER_POSITION_UP); // expose now
                    rc = LIFTING; // we're going
                } else {
                    rc = EXPOSE; // we're already there
                }
                break;

            case CONCEAL:
                // do conceal
                if (lifter_position_get() != LIFTER_POSITION_DOWN) {
                    lifter_position_set(LIFTER_POSITION_DOWN); // conceal now
                    rc = LIFTING; // we're going
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
        rc = nla_put_u8(skb, GEN_INT8_A_MSG, rc); // rc depends on above

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
// netlink command handler for hit log commands
//---------------------------------------------------------------------------
int nl_hits_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
delay_printk("Lifter: handling hits command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na);
delay_printk("Lifter: received value: %i\n", value);

        // prepare response
        rc = 0; // TODO -- redesign the hit sensor .ko file to accomodate sending data here
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

        // TODO -- stop hardware motor wherever it is
        // TODO -- disable hit sensor (but don't clear hit log)

        // Stop accessories
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
                // configure based on accessory type
                switch (acc_c->acc_type) {
                    case ACC_NES_MFS:
                        // burst or single mode?
                        if (acc_c->ex_data1) {
                            // burst mode
                            if (acc_c->ex_data2 >= 255) { // 8-bits, so shouldn't be bigger
                                generic_output_set_onoff_repeat(acc_c->acc_type, num, -1); // infinite repeat
                            } else {
                                generic_output_set_onoff_repeat(acc_c->acc_type, num, acc_c->ex_data2);
                            }
                            mode = BURST_FIRE;
                        } else {
                            mode = TEMP_ON;
                        }
                        break;
                    case ACC_MILES_SDH:
                        // TODO -- what to do with MILES data?
                        delay_printk("Lifter: Couldn't do s*** with MILES data: %i %i %i\n", acc_c->ex_data1, acc_c->ex_data2, acc_c->ex_data3);
                        break;
                }

                // configure generic
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
// init handler for the module
//---------------------------------------------------------------------------
static int __init Lifter_init(void)
    {
	int retval = 0, d_id;
    struct driver_command commands[] = {
        {NL_C_EXPOSE,    nl_expose_handler},
        {NL_C_STOP,      nl_stop_handler},
        {NL_C_HITS,      nl_hits_handler},
        {NL_C_ACCESSORY, nl_accessory_handler},
        /*{NL_C_HIT_CAL,   nl_hit_cal_handler},*/
    };
    struct nl_driver driver = {NULL, commands, 4, NULL}; // no heartbeat object, 4 command in list, no identifying data structure

    // install driver w/ netlink provider
    d_id = install_nl_driver(&driver);
delay_printk("%s(): %s - %s : %i\n",__func__,  __DATE__, __TIME__, d_id);
    atomic_set(&driver_id, d_id);

	// signal that we are fully initialized
	atomic_set(&full_init, TRUE);
	return retval;
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit Lifter_exit(void)
    {
    uninstall_nl_driver(atomic_read(&driver_id));
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(Lifter_init);
module_exit(Lifter_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI Lifter module");
MODULE_AUTHOR("ndb");

