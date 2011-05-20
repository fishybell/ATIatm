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

//---------------------------------------------------------------------------
// For external functions
//---------------------------------------------------------------------------
#include "target_mover_generic.h"
#include "target_generic_output.h"

//---------------------------------------------------------------------------
// These variables are parameters given when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------

#define MOVED_DELAY 750

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

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
// mover state variables
//---------------------------------------------------------------------------
static void move_change(unsigned long data); // forward declaration
static struct timer_list moved_timer = TIMER_INITIALIZER(move_change, 0, 0);

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

//---------------------------------------------------------------------------
// Timer timeout function for finishing a move change
//---------------------------------------------------------------------------
static void move_change(unsigned long data) {
    u8 move_data;
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }

    // notify netlink userspace
    move_data = 128+mover_speed_get(); // signed speed turned to unsigned byte
    send_nl_message_multi(&move_data, pos_mfh, NL_C_MOVE);
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
        mover_speed_stop();
    }

    // create event for outputs
    generic_output_event(etype);

    // notify user-space
    switch (etype) {
        case EVENT_MOVE:
        case EVENT_MOVING:
            mod_timer(&moved_timer, jiffies+(((MOVED_DELAY/2)*HZ)/1000)); // wait for X milliseconds for sensor to settle
            break;
        case EVENT_STOPPED:
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
int nl_stop_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
delay_printk("Mover: handling stop command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na); // value is ignored
delay_printk("Mover: received value: %i\n", value);

        // stop mover
        mover_speed_stop();

        // Stop accessories (will disable them as well)
        generic_output_event(EVENT_ERROR);

        // prepare response
        rc = nla_put_u8(skb, GEN_INT8_A_MSG, 1); // value is ignored

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
    u8 value = 0;
delay_printk("Mover: handling move command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na); // value is ignored
delay_printk("Mover: received value: %i\n", value);

        // default to message handling success (further feedback will come from move_event)
        rc = HANDLE_SUCCESS_NO_REPLY;

        // do something to the mover
        if (value == VELOCITY_STOP) {
            // stop
            mover_speed_stop();
        } else if (value == VELOCITY_REQ) {
            // retrieve speed
            value = 128+mover_speed_get(); // signed speed turned to unsigned byte
            rc = nla_put_u8(skb, GEN_INT8_A_MSG, value);

            // message creation success?
            if (rc == 0) {
                rc = HANDLE_SUCCESS;
            } else {
                delay_printk("Mover: could not create return message\n");
                rc = HANDLE_FAILURE;
            }
        } else {
            // move
            mover_speed_set(value-128); // unsigned value to signed speed (0 will coast)
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
// netlink command handler for event commands
//---------------------------------------------------------------------------
int nl_event_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
delay_printk("Mover: handling event command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na); // value is ignored
delay_printk("Mover: received value: %i\n", value);

        // handle event
        move_event_internal(value, false); // don't repropogate

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
// init handler for the module
//---------------------------------------------------------------------------
static int __init Mover_init(void) {
    int retval = 0, d_id;
    struct driver_command commands[] = {
        {NL_C_STOP,      nl_stop_handler},
        {NL_C_MOVE,      nl_move_handler},
        {NL_C_POSITION,  nl_position_handler},
        {NL_C_EVENT,     nl_event_handler},
    };
    struct nl_driver driver = {NULL, commands, sizeof(commands)/sizeof(struct driver_command), NULL}; // no heartbeat object, X command in list, no identifying data structure

    // install driver w/ netlink provider
    d_id = install_nl_driver(&driver);
delay_printk("%s(): %s - %s : %i\n",__func__,  __DATE__, __TIME__, d_id);
    atomic_set(&driver_id, d_id);

    // set callback handlers
    set_move_callback(move_event);

    INIT_WORK(&position_work, position_change);

    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
    return retval;
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit Mover_exit(void) {
    atomic_set(&full_init, FALSE);
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

