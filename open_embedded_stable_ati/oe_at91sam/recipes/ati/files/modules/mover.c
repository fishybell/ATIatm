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
#include "target_mover_generic.h"
#include "target_generic_output.h"
#include "target_battery.h"
#include "fasit/faults.h"
#define LIFTER_POSITION_DOWN 0
#define LIFTER_POSITION_UP 1

//#define DEBUG_USERCONN

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
    va_start(ap, fmt);
   char *msg = kmalloc(256, GFP_KERNEL);
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

        mover_set_continuous_move(value-32768); // unsigned value to signed speed (0 will coast)

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

        mover_set_moveaway_move(value-32768); // unsigned value to signed speed (0 will coast)

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
        }// else if ((mover_speed_get() > 0 && value-32768 < 0) || (mover_speed_get() < 0 && value-32768 > 0)) { // moving from positive to negative, or negative to positive
//            char *msg = kmalloc(512, GFP_KERNEL);
//            char *nothing_buf = msg + 256; // share same buffer
//            int speed = value-32768; // unsigned value to signed value
//            escape_scen_call("{Nothing;;;;}", 13, nothing_buf);
//            //delay_printk("mover is reversing direction\n");
//            send_nl_message_multi("mover is scenario reversing direction", error_mfh, NL_C_FAILURE);
//            enable_battery_check(0); // disable battery checking while motor is on
//            
//            snprintf(msg, 256, " \
//              {Send;R_MOVER;NL_C_MOVE;1;0080} -- Send Stop to mover \
//              {DoWait;%s;EVENT_STOPPED;15000;%s} -- Wait 15 seconds for stop (cmd installed below)\
//              {Send;R_MOVER;NL_C_MOVE;1;%04X} -- Send Stop to mover",
//              nothing_buf, nothing_buf, value & 0xffff
//                ); // use only bottom half of buffer, install "nothing" cmd, verify 16 bits on move value
//            send_nl_message_multi(msg, error_mfh, NL_C_FAILURE);
//
//            // send reverse scenario
//            if (target_scenario(msg) == -1) {
//               // failed to do in scenario, just slam it on backwards
//	            mover_speed_set(value-32768); // unsigned value to signed speed (0 will coast)
//               send_nl_message_multi("aborted scenario reversing direction", error_mfh, NL_C_FAILURE);
//            }
//            kfree(msg);
        else {
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
    u8 data = BATTERY_SHUTDOWN; // in case we need to shutdown
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
// netlink command handler for accessory commands
//---------------------------------------------------------------------------
int nl_hit_cal_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    struct hit_calibration *hit_c;
    int rc = HANDLE_SUCCESS_NO_REPLY; // by default this is a command with no response
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
                case HIT_GET_KILL:       /* overwrites nothing (gets hi ts_to_kill value) */
                case HIT_OVERWRITE_NONE: /* overwrite nothing (gets reply with current values) */
                case HIT_OVERWRITE_CAL:   /* overwrites calibration values (sensitivity, seperation) */
                case HIT_OVERWRITE_TYPE: /* overwrites type value only */
                     // Movers do not provide this, so ignore these types
                     // As of now 4/25/12 movers only care about kill reaction (after_kill)
                    break;
                case HIT_OVERWRITE_ALL:   /* overwrites every value */
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
    u8 data = BATTERY_SHUTDOWN; // in case we need to shutdown
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
        {NL_C_HIT_CAL,   nl_hit_cal_handler},
        {NL_C_SLEEP,     nl_sleep_handler},
    };
    struct nl_driver driver = {NULL, commands, sizeof(commands)/sizeof(struct driver_command), NULL}; // no heartbeat object, X command in list, no identifying data structure

    // install driver w/ netlink provider
    d_id = install_nl_driver(&driver);
delay_printk("%s(): %s - %s : %i\n",__func__,  __DATE__, __TIME__, d_id);
    atomic_set(&driver_id, d_id);

    // set callback handlers
    set_move_callback(move_event, move_faults);

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

