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

#include "target_lifter_infantry.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"lifter"

//---------------------------------------------------------------------------
// These variables are parameters giving when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
/*static int charge = FALSE;
module_param(charge, bool, S_IRUGO);
static int minvoltval = 12;
module_param(minvoltval, int, S_IRUGO);*/

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

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
            case EXPOSE:
                // do expose
                lifter_position_set(LIFTER_POSITION_UP);
                rc = LIFTING;
                break;

            case CONCEAL:
                // do conceal
                lifter_position_set(LIFTER_POSITION_DOWN);
                rc = LIFTING;
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
// init handler for the module
//---------------------------------------------------------------------------
static int __init Lifter_init(void)
    {
	int retval = 0, d_id;
    struct driver_command commands[] = {
        {NL_C_EXPOSE,    nl_expose_handler},
        {NL_C_STOP,      nl_stop_handler},
        {NL_C_HITS,      nl_hits_handler},
        /*{NL_C_HIT_CAL,   nl_hit_cal_handler},
        {NL_C_ACCESSORY, nl_accessory_handler},*/
    };
    struct nl_driver driver = {NULL, commands, 1, NULL}; // no heartbeat object, 1 command in list, no identifying data structure

delay_printk("%s(): %s - %s\n",__func__,  __DATE__, __TIME__);

    // install driver w/ netlink provider
    d_id = install_nl_driver(&driver);
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

