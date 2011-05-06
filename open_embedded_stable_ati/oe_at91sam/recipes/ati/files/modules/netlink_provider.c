#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>

#include <mach/gpio.h>
#include "target.h"
#include "target_hardware.h"

#include "netlink_kernel.h"

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

/* delayed work queues */
static struct work_struct init_work; // work to perform just after init

#define VERSION_NR 1
/* family definition */
static struct genl_family provider_gnl_family = {
    .id = GENL_ID_GENERATE,         //genetlink should generate an id
    .hdrsize = 0,
    .name = "ATI",        //the name of this family, used by userspace application
    .version = VERSION_NR,                   //version number  
    .maxattr = NL_A_MAX,
};

/* multicast group definition */
static struct genl_multicast_group nl_event_mcgrp = {
    .id = 1,
    .name = "ATI",
};

// generated number grouping multiple handlers to a single driver
atomic_t last_driver_id = ATOMIC_INIT(0);

// largest sequence number seen (in case we need to auto-generate one)
atomic_t largest_seq = ATOMIC_INIT(0);

// command handler linked list
typedef struct command_handler {
    int driver_id; // generated id for this driver
    struct driver_command drv; // passed driver handler info
    void *ident; // optional identifying data
    struct command_handler *next; // link to next handler
} command_handler_t;

// global map for command handler linked lists
static struct command_handler *command_map[NL_C_MAX+1];

/* global map lock (using a read/write spinlock)
   reads are concurrent
   writes are exclusive */
rwlock_t map_rwlock = RW_LOCK_UNLOCKED;


/* sends an unsolicited unicast message */
int send_nl_message_uni(void *package, message_filler_handler mfh, int cmd, int pid) {
    struct sk_buff *skb;
    int rc;
    void *msg_head;
    skb = NULL;
    rc = -1;

    /* allocate some memory, since the size is not yet known use NLMSG_GOODSIZE */    
    skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (skb == NULL)
        goto out;

    /* create the message headers */
    /* arguments of genlmsg_put: 
       struct sk_buff *, 
       int (sending) pid, 
       int sequence number, 
       struct genl_family *, 
       int flags, 
       u8 command index
    */
    msg_head = genlmsg_put(skb, 0, atomic_add_return(1,&largest_seq), &provider_gnl_family, 0, cmd);
    if (msg_head == NULL) {
        rc = -ENOMEM;
        goto out;
    }

    /* assemble message via message filler handler */
    rc = mfh(skb, package);
    if (rc != 0)
        goto out;

    /* finalize the message */
    rc = genlmsg_end(skb, msg_head);
    if (rc < 0)
        goto out;

    /* send the message */
    rc = genlmsg_unicast(skb, pid);
    if (rc != 0) {
        if (rc == -ESRCH) {
//delay_printk("multicast message %i sent, but no one listening\n", cmd);
        } else {
            skb = NULL; // already free at this point
            goto out;
        }
    }

    return 0;

 out:
    delay_printk("NL ERROR: an error occured in send_nl_message_uni: %i\n", rc);

    if (skb != NULL) nlmsg_free(skb); // if we didn't use actually send, free it here
    return rc;
}
EXPORT_SYMBOL(send_nl_message_uni);

/* sends an unsolicited multicast message */
int send_nl_message_multi(void *package, message_filler_handler mfh, int cmd) {
    struct sk_buff *skb;
    int rc;
    void *msg_head;
    skb = NULL;
    rc = -1;

    /* allocate some memory, since the size is not yet known use NLMSG_GOODSIZE */    
    skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (skb == NULL) {
delay_printk("NL ERROR: no memory for genlmsg_new\n");
        goto out;
    }

    /* create the message headers */
    /* arguments of genlmsg_put: 
       struct sk_buff *, 
       int (sending) pid, 
       int sequence number, 
       struct genl_family *, 
       int flags, 
       u8 command index
    */
    msg_head = genlmsg_put(skb, 0, atomic_add_return(1,&largest_seq), &provider_gnl_family, 0, cmd);
    if (msg_head == NULL) {
        rc = -ENOMEM;
delay_printk("NL ERROR: no memory for genlmsg_put\n");
        goto out;
    }

    /* assemble message via message filler handler */
    rc = mfh(skb, package);
    if (rc != 0) {
delay_printk("NL ERROR: mfh error: %i\n", rc);
        goto out;
    }

    /* finalize the message */
    rc = genlmsg_end(skb, msg_head);
    if (rc < 0) {
delay_printk("NL ERROR: no memory for genlmsg_end\n");
        goto out;
    }

    /* send the message */
    rc = genlmsg_multicast(skb, 0, nl_event_mcgrp.id, GFP_KERNEL);
    if (rc != 0) {
        if (rc == -ESRCH) {
//delay_printk("multicast message %i sent, but no one listening\n", cmd);
        } else {
delay_printk("NL ERROR: genlmsg_new\n");
            skb = NULL; // already free at this point
            goto out;
        }
    }

    return 0;

 out:
    delay_printk("NL ERROR: an error occured in send_nl_message_multi: %i\n", rc);

    if (skb != NULL) nlmsg_free(skb); // if we didn't use actually send, free it here
    return rc;
}
EXPORT_SYMBOL(send_nl_message_multi);

/* top-level command handler, receives message, calls child handlers, and responds */
int provider_command_handler(struct sk_buff *skb_2, struct genl_info *info) {
    struct sk_buff *skb;
    int rc, seq;
    int did_lock = 0;
    void *msg_head;
    struct command_handler *this;
    skb = NULL;
    rc = -1;

    /* verify message integrity */
    if (info == NULL || info->genlhdr == NULL) {
delay_printk("NL ERROR: bad info or info->genlhdr\n");
        goto out;
    }

//delay_printk("received nl message w/ cmd id: %i\n", info->genlhdr->cmd);
    if (info->genlhdr->cmd <= NL_C_UNSPEC || info->genlhdr->cmd > NL_C_MAX) {
delay_printk("NL ERROR: bad command: %i\n", info->genlhdr->cmd);
        goto out;
    }

    /* grab the initial sequence number */
    seq = info->snd_seq;

    /* move up the max sequence number */
    if (atomic_add_unless(&largest_seq,1,0) == 0) { // will increment by one unless it was 0
    	atomic_set(&largest_seq,seq); // start out at least as large as our first sequence
    }

    /* call command handlers */
    read_lock(&map_rwlock); /* lock (for reading) */
    did_lock = 1; /* mark for unlocking later */
    this = command_map[info->genlhdr->cmd];
    while (this != NULL) { /* if no handlers, no loop */
        /* prepare response as much as possible before the actual handling of the message */
        /* allocate the reply, since the size is not yet known use NLMSG_GOODSIZE */    
        skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
//delay_printk("found nl handler\n");
        if (skb == NULL) {
delay_printk("NL ERROR: no memory for genlmsg_new\n");
            goto out;
        }
        /* arguments of genlmsg_put: 
           struct sk_buff *, 
           int (sending) pid, 
           int sequence number, 
           struct genl_family *, 
           int flags, 
           u8 command index
        */
        msg_head = genlmsg_put(skb, 0, ++seq, &provider_gnl_family, 0, info->genlhdr->cmd);
        if (msg_head == NULL) {
            rc = -ENOMEM;
delay_printk("NL ERROR: no memory for genlmsg_put\n");
            goto out;
        }

        /* handle message */
        rc = this->drv.handler(info, skb, info->genlhdr->cmd, this->ident);

        /* send message back if necessary */
        switch (rc) {
            case HANDLE_FAILURE:
                /* generate fault message */
                /* we're starting the message over, free it */
                nlmsg_free(skb);

                /* prepare new message */
                skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
                if (skb == NULL)
                    break; /* don't give up on all message handlers */
                msg_head = genlmsg_put(skb, 0, seq, &provider_gnl_family, 0, NL_C_FAILURE);
                if (msg_head == NULL) {
                    /* don't give up on all message handlers */
                    nlmsg_free(skb);
                    skb = NULL;
                    break;
                }
                nla_put_string(skb, GEN_STRING_A_MSG, "uknown error");

                /* fall through */
            case HANDLE_SUCCESS:
            case HANDLE_FAILURE_MESSAGE:
                /* all of the above responses will send a message */
                /* finalize the message */
                rc = genlmsg_end(skb, msg_head);
                if (rc < 0) {
delay_printk("NL ERROR: genlmsg_end in HANDLE_SUCCESS or HANDLE_FAILURE_MESSAGE\n");
                    goto out; /* give up on other message handlers */
                }

                /* send the message back */
                rc = genlmsg_unicast(skb,info->snd_pid );
                if (rc != 0) {
                    /* so what if they didn't work? move on */
                    ///* give up on other message handlers */
                    //skb = NULL; // already free at this point
                    //goto out;
                }

                /* successful send, increment largest_seq */
                atomic_inc(&largest_seq);

                /* move on to next handler */
                break;
            case HANDLE_SUCCESS_NO_REPLY:
            case HANDLE_FAILURE_NO_REPLY:
                /* none of the above responses will send a message */
                /* we didn't consume the message, free it */
                nlmsg_free(skb);
                skb = NULL; /* don't try to free again */
                seq--; /* we didn't use this sequence, reuse */

                /* move on to next handler */
                break;
        }

        /* next handler */
        rc = 0;
        this = this->next;
    }
    read_unlock(&map_rwlock); /* unlock */

//delay_printk("finished with received message\n");
    return 0;

 out:
    if (did_lock != 0) read_unlock(&map_rwlock); // unlock if necessary
    if (skb != NULL) nlmsg_free(skb); // if we didn't use actually send, free it here

    delay_printk("NL ERROR: an error occured in provider_command_handler:\n");

    return rc;
}

/* commands: mapping between the command enumeration and the actual function */
struct genl_ops provider_gnl_ops_failure = {
    .cmd = NL_C_FAILURE,
    .flags = 0,
    .policy = generic_string_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};
struct genl_ops provider_gnl_ops_battery = {
    .cmd = NL_C_BATTERY,
    .flags = 0,
    .policy = generic_int8_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};
struct genl_ops provider_gnl_ops_expose = {
    .cmd = NL_C_EXPOSE,
    .flags = 0,
    .policy = generic_int8_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};
struct genl_ops provider_gnl_ops_move = {
    .cmd = NL_C_MOVE,
    .flags = 0,
    .policy = generic_int8_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};
struct genl_ops provider_gnl_ops_position = {
    .cmd = NL_C_POSITION,
    .flags = 0,
    .policy = generic_int16_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};
struct genl_ops provider_gnl_ops_stop = {
    .cmd = NL_C_STOP,
    .flags = 0,
    .policy = generic_int8_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};
struct genl_ops provider_gnl_ops_hits = {
    .cmd = NL_C_HITS,
    .flags = 0,
    .policy = generic_int8_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};
struct genl_ops provider_gnl_ops_hit_log = {
    .cmd = NL_C_HIT_LOG,
    .flags = 0,
    .policy = generic_string_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};
struct genl_ops provider_gnl_ops_hits_cal = {
    .cmd = NL_C_HIT_CAL,
    .flags = 0,
    .policy = hit_calibration_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};
struct genl_ops provider_gnl_ops_bit = {
    .cmd = NL_C_BIT,
    .flags = 0,
    .policy = bit_event_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};
struct genl_ops provider_gnl_ops_accessory = {
    .cmd = NL_C_ACCESSORY,
    .flags = 0,
    .policy = accessory_conf_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};
struct genl_ops provider_gnl_ops_gps = {
    .cmd = NL_C_GPS,
    .flags = 0,
    .policy = gps_conf_policy,
    .doit = provider_command_handler,
    .dumpit = NULL,
};

static struct genl_ops *command_op_map[] = {
    /* NL_C_UNSPEC */		NULL,
    /* NL_C_FAILURE */		&provider_gnl_ops_failure,
    /* NL_C_BATTERY */		&provider_gnl_ops_battery,
    /* NL_C_EXPOSE */		&provider_gnl_ops_expose,
    /* NL_C_MOVE */			&provider_gnl_ops_move,
    /* NL_C_POSITION */		&provider_gnl_ops_position,
    /* NL_C_STOP */			&provider_gnl_ops_stop,
    /* NL_C_HITS */			&provider_gnl_ops_hits,
    /* NL_C_HIT_LOG */		&provider_gnl_ops_hit_log,
    /* NL_C_HIT_CAL */		&provider_gnl_ops_hits_cal,
    /* NL_C_BIT */			&provider_gnl_ops_bit,
    /* NL_C_ACCESSORY */	&provider_gnl_ops_accessory,
    /* NL_C_GPS */			&provider_gnl_ops_gps,
};

typedef struct hb_obj_list {
    struct heartbeat_object obj;
    int id;
    struct hb_obj_list *next;
} hb_obj_map_t;
static struct hb_obj_list *hb_list;

/* install_nl_driver: installs driver for multiple handlers
   returns: a new driver id
   arg1: the driver data structure */
int install_nl_driver(const struct nl_driver *driver) {
    int i, id;
    struct command_handler *ch_new;
    struct hb_obj_list *this;

    /* generate driver id number and return it */
    id = atomic_add_return(1,&last_driver_id);

    /* install heartbeat object if found */
    if (driver->hb_obj) {
        if (heartbeat_add(*driver->hb_obj) != HB_SUCCESS) {
            return -1; // failure to add heartbeat = failure overall
        }

        /* remember as being owned by id */
        this = kmalloc(sizeof(struct hb_obj_list), GFP_KERNEL);
        if (this == NULL) {
            heartbeat_clear(*driver->hb_obj); // uninstall
            return -4; // failure to remember heartbeat = failure overall
        }
        this->obj = *driver->hb_obj;
        this->id = id;

        /* install at head of list */
        this->next = hb_list;
        hb_list = this;
delay_printk("NL Installed heartbeat object\n");
    }

    /* install driver commands */
    write_lock(&map_rwlock); /* lock (for writing) */
    for (i=0; i<driver->num_commands; i++) {
        /* allocate memory for new handler */
        ch_new = kmalloc(sizeof(struct command_handler), GFP_KERNEL);
        if (ch_new == NULL) {
            write_unlock(&map_rwlock); /* unlock */
            uninstall_nl_driver(id); /* this will free up any installed handlers */
            return -2; // no memory = failure overall
        }

        /* build handler */
        ch_new->driver_id = id;
        ch_new->drv = driver->commands[i];
        ch_new->ident = driver->ident;

        /* install in map */
        if (ch_new->drv.command <= NL_C_UNSPEC || ch_new->drv.command > NL_C_MAX) {
            write_unlock(&map_rwlock); /* unlock */
            kfree(ch_new); /* free this new one */
            uninstall_nl_driver(id); /* this will free up any installed handlers */
            return -3; // bad command = failure overall
        }
        ch_new->next = command_map[ch_new->drv.command]; // find existing head
        command_map[ch_new->drv.command] = ch_new; // install as new head of list
delay_printk("NL Installed netlink command handler for %i\n", ch_new->drv.command);
    }
    write_unlock(&map_rwlock); /* unlock */

    /* return id number on success */
    return id;
}
EXPORT_SYMBOL(install_nl_driver);

void uninstall_nl_driver(int driver_id) {
    int i;
    struct command_handler *this;
    struct command_handler *last;
    struct hb_obj_list *hb_this;
    struct hb_obj_list *hb_last;

    /* clear heartbeat and remove from list */
    hb_this = hb_list;
    hb_last = NULL;
    while (hb_this != NULL) {
        if (hb_this->id == driver_id) {
            /* found a matching handler, remove it */
            if (hb_last == NULL) {
                hb_list = hb_this->next; // was head
                hb_last = hb_list;
            } else {
                hb_last->next = hb_this->next; // remove from within line
            }
            heartbeat_clear(hb_this->obj); // clear out heartbeat object
            kfree(hb_this); // deallocate memory
            hb_this = hb_last; // keep looking where we left off
        } else {
            hb_last = hb_this; // remember last item
            hb_this = hb_this->next; // move on to next item
        }
    }

    /* uninstall from each command map list */
    write_lock(&map_rwlock); /* lock (for writing) */
    for (i=NL_C_UNSPEC+1; i<=NL_C_MAX; i++) {
        this = command_map[i];
        last = NULL;
        while (this != NULL) {
            if (this->driver_id == driver_id) {
                /* found a matching handler, remove it */
                if (last == NULL) {
                    command_map[i] = this->next; // was head
                    last = command_map[i];
                } else {
                    last->next = this->next; // remove from within line
                }
                kfree(this); // deallocate memory
                this = last; // keep looking where we left off
            } else {
                last = this; // remember last item
                this = this->next; // move on to next item
            }
        }
    }
    write_unlock(&map_rwlock); /* unlock */
}
EXPORT_SYMBOL(uninstall_nl_driver);

//---------------------------------------------------------------------------
// Work item to run through final initialization step
//---------------------------------------------------------------------------
static void init_final(struct work_struct * work) {
//delay_printk("doing final initialization\n");

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
/*    // initialize heartbeat callback function
    switch (heartbeat_add(nl_hb)) {
        case HB_SUCCESS: delay_printk("Heartbeat Callback Initialized\n"); break;
        case HB_DEAD: delay_printk("Heartbeat Callback Error: DEAD\n"); break;
        case HB_INVALID: delay_printk("Heartbeat Callback Error: INVALID\n"); break;
    }*/
}

static int __init gnKernel_init(void) {
    int rc, i;
    delay_printk("INIT GENERIC NETLINK PROVIDER MODULE\n");

    /* initialize global map */
    write_lock(&map_rwlock); /* lock (for writing) */
    for (i=0; i<=NL_C_MAX; i++) {
        command_map[i] = NULL;
    }
    write_unlock(&map_rwlock); /* unlock */

    /* register new family */
    rc = genl_register_family(&provider_gnl_family);
    if (rc != 0)
        goto failure;
    /* register multicast group */
    rc = genl_register_mc_group(&provider_gnl_family, &nl_event_mcgrp);
    if (rc != 0) {
       delay_printk("NL ERROR: register mc group: %i\n",rc);
        genl_unregister_family(&provider_gnl_family);
        goto failure;
    }
    /* register functions (commands) of the new family */
    for (i=NL_C_UNSPEC+1; i<=NL_C_MAX; i++) {
        rc = genl_register_ops(&provider_gnl_family, command_op_map[i]);
        if (rc != 0) {
           delay_printk("NL ERROR: register %i: %i\n",command_op_map[i]->cmd,rc);
            /* unregister the already registerd ops */
            for (i=i-1; i>0; i--) {
                genl_unregister_ops(&provider_gnl_family, command_op_map[i]);
            }
            genl_unregister_family(&provider_gnl_family);
            goto failure;
        }
    }

    // do some of the initialization after init has been called
    INIT_WORK(&init_work, init_final);
    schedule_work(&init_work);

    return 0;

  failure:
   delay_printk("NL ERROR: an error occured while inserting the generic netlink example module\n");
    return -1;


}

static void __exit gnKernel_exit(void) {
    int ret, i;
    struct command_handler *this;
    struct command_handler *last;
    atomic_set(&full_init, FALSE);
    ati_flush_work(&init_work); // close any open work queue items
   delay_printk("EXIT GENERIC NETLINK PROVIDER MODULE\n");

    /* clear heartbeat callback */
    //heartbeat_clear(nl_hb);

    /* clear global map */
    write_lock(&map_rwlock); /* lock (for writing) */
    for (i=0; i<=NL_C_MAX; i++) {
        this = command_map[i];
        while (this != NULL) {
            last = this; // remember ...
            this = this->next; // move on to next item
            kfree(last); // ... and free
        }
        command_map[i] = NULL;
    }
    write_unlock(&map_rwlock); /* unlock */

    /* unregister multicast group */
    genl_unregister_mc_group(&provider_gnl_family, &nl_event_mcgrp);
    /* unregister the functions */
    for (i=NL_C_UNSPEC+1; i<=NL_C_MAX; i++) {
        ret = genl_unregister_ops(&provider_gnl_family, command_op_map[i]);
        if (ret != 0) {
           delay_printk("NL ERROR: unregister %i ops: %i\n",command_op_map[i]->cmd,ret);
        }
    }
    /* unregister the family */
    ret = genl_unregister_family(&provider_gnl_family);
    if (ret !=0) {
       delay_printk("NL ERROR: unregister family %i\n",ret);
    }
}


module_init(gnKernel_init);
module_exit(gnKernel_exit);
MODULE_LICENSE("proprietary");


