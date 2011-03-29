#include <net/genetlink.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>

#include <mach/gpio.h>
#include <mach/heartbeat.h>
#include "target_hardware.h"

#include "netlink_user.h"

#define TIMEOUT_IN_MSECONDS      10000
static void timeout_fire(unsigned long data);
static struct timer_list timeout_timer_list = TIMER_INITIALIZER(timeout_fire, 0, 0);

/* periodic heartbeat callback for the netlink provider @ 500 hz */
static void nl_heartbeat_cb(unsigned int);
static struct heartbeat_object nl_hb = {nl_heartbeat_cb, NULL, 0, HZ_TO_MOD(1)}; // callback with ticks, no offset, 1 hz

/* attribute policy: defines which attribute has which type (e.g int, char * etc)
 * possible values defined in net/netlink.h 
 */
static struct nla_policy provider_genl_policy[NL_A_MAX + 1] = {
    [NL_A_MSG] = { .type = NLA_NUL_STRING },
};

/* structure to map name to timing interval and id */
typedef struct nl_name_time {
    char *name; // name of client
    int id; // id of client
    int seq; // last message sequence number used
    int trigger_every; // trigger every X heartbeat ticks
} nl_name_time_t;

/* mapping of name to timing to timing interval */
#define MAX_NL_CLIENTS 1024
static struct nl_name_time nl_map[MAX_NL_CLIENTS];

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

/* callback for heartbeat */
void nl_heartbeat_cb(unsigned int ticks) {
    // for testing
    if (at91_get_gpio_value(AT91_PIN_PB8) == ACTIVE_LOW) {
        at91_set_gpio_value(AT91_PIN_PB8, !ACTIVE_LOW);
    } else {
        at91_set_gpio_value(AT91_PIN_PB8, ACTIVE_LOW);
        printk("ticks: %u\n", ticks);
    }
}

/* sends a multicast reregister message */
int provider_reregister(void) {
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
    msg_head = genlmsg_put(skb, 0, 0, &provider_gnl_family, 0, NL_C_REREG);
    if (msg_head == NULL) {
        rc = -ENOMEM;
        goto out;
    }
    /* add a NL_A_MSG attribute (actual value to be sent) */
    rc = nla_put_string(skb, NL_A_MSG, "reregister");
    if (rc != 0)
        goto out;

    /* finalize the message */
    rc = genlmsg_end(skb, msg_head);
    if (rc < 0)
        goto out;

    /* send the message back */
    rc = genlmsg_multicast(skb, 0, nl_event_mcgrp.id, GFP_KERNEL);
    if (rc != 0) {
        if (rc == -ESRCH) {
            printk("reregister sent, but no one listening\n");
        } else {
            skb = NULL; // already free at this point
            goto out;
        }
    }

    return 0;

 out:
    printk("an error occured in provider_rereg: %i\n", rc);

    if (skb != NULL) nlmsg_free(skb); // if we didn't use actually send, free it here
    return rc;
}

/* an echo command, receives a message, prints it and sends another message back */
int provider_echo(struct sk_buff *skb_2, struct genl_info *info) {
    struct nlattr *na;
    struct sk_buff *skb;
    int rc;
    void *msg_head;
    char * mydata;
    skb = NULL;
    rc = -1;

    if (info == NULL)
        goto out;

    /* for each attribute there is an index in info->attrs which points to a nlattr structure
     * in this structure the data is given
     */
    na = info->attrs[NL_A_MSG];
    if (na) {
        mydata = (char *)nla_data(na);
        if (mydata == NULL)
            printk("error while receiving data\n");
        //else
        //    printk("received: %s\n", mydata);
    }
    else
        printk("no info->attrs %i\n", NL_A_MSG);

    /* send a message back */
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
    msg_head = genlmsg_put(skb, 0, info->snd_seq+1, &provider_gnl_family, 0, NL_C_ECHO);
    if (msg_head == NULL) {
        rc = -ENOMEM;
        goto out;
    }
    /* add a NL_A_MSG attribute (actual value to be sent) */
    rc = nla_put_string(skb, NL_A_MSG, "hello world from kernel space");
    if (rc != 0)
        goto out;

    /* finalize the message */
    rc = genlmsg_end(skb, msg_head);
    if (rc < 0)
        goto out;

    /* send the message back */
    rc = genlmsg_unicast(skb,info->snd_pid );
    if (rc != 0) {
        skb = NULL; // already free at this point
        goto out;
    }

    return 0;

 out:
    printk("an error occured in provider_echo:\n");

    if (skb != NULL) nlmsg_free(skb); // if we didn't use actually send, free it here
    return rc;
}

/* commands: mapping between the command enumeration and the actual function */
struct genl_ops provider_gnl_ops_echo = {
    .cmd = NL_C_ECHO,
    .flags = 0,
    .policy = provider_genl_policy,
    .doit = provider_echo,
    .dumpit = NULL,
};

/* a register command, receives a message, prints it and sends another message back */
int provider_reg(struct sk_buff *skb_2, struct genl_info *info) {
    struct nlattr *na;
    struct sk_buff *skb;
    int rc, i;
    void *msg_head;
    char * mydata;
    int did_reg;
    struct nl_name_time nt;
    skb = NULL;
    rc = -1;

    if (info == NULL)
        goto out;

    /* for each attribute there is an index in info->attrs which points to a nlattr structure
     * in this structure the data is given
     */
    na = info->attrs[NL_A_MSG];
    did_reg = 0;
    if (na) {
        mydata = (char *)nla_data(na);
        if (mydata == NULL) {
            printk("error while receiving data\n");
        } else {
            nt.name = kmalloc(strlen(mydata), GFP_KERNEL); // can be preempted
            if (sscanf(mydata, "%s %i", nt.name, &nt.trigger_every) == 2) {
                nt.id = info->snd_pid;
                nt.seq = info->snd_seq+1; // last sequence to be used is the next one which we'll send down below
                for (i=0; i<MAX_NL_CLIENTS; i++) {
                    if (nl_map[i].name == NULL) {
                        nl_map[i] = nt; // copy structure to map
                        did_reg = 1; // fully registered now
                        break; // don't register more than once
                    }
                }
            }

            if (did_reg) {
                printk("registered: %s:%i:%i\n", nt.name, nt.id, nt.trigger_every);
            } else {
                kfree(nt.name);
                printk("could not register with: %s\n", mydata);
            }
        }
    }
    else
        printk("no info->attrs %i\n", NL_A_MSG);

    /* send a message back */
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
    msg_head = genlmsg_put(skb, 0, info->snd_seq+1, &provider_gnl_family, 0, NL_C_REG);
    if (msg_head == NULL) {
        rc = -ENOMEM;
        goto out;
    }

    /* add a NL_A_MSG attribute (actual value to be sent) */
    rc = nla_put_string(skb, NL_A_MSG, did_reg ? "registered" : "failed");
    if (rc != 0)
        goto out;

    /* finalize the message */
    rc = genlmsg_end(skb, msg_head);
    if (rc < 0)
        goto out;

    /* send the message back */
    rc = genlmsg_unicast(skb,info->snd_pid );
    if (rc != 0) {
        skb = NULL; // already free at this point
        goto out;
    }

    return 0;

 out:
    printk("an error occured in provider_reg:\n");

    if (skb != NULL) nlmsg_free(skb); // if we didn't use actually send, free it here
    return rc;
}

/* commands: mapping between the command enumeration and the actual function */
struct genl_ops provider_gnl_ops_reg = {
    .cmd = NL_C_REG,
    .flags = 0,
    .policy = provider_genl_policy,
    .doit = provider_reg,
    .dumpit = NULL,
};

/* a register command, receives a message, prints it and sends another message back */
int provider_unreg(struct sk_buff *skb_2, struct genl_info *info) {
    struct nlattr *na;
    struct sk_buff *skb;
    int rc, i;
    void *msg_head;
    char * mydata;
    int did_unreg;
    skb = NULL;
    rc = -1;

    if (info == NULL)
        goto out;

    /* for each attribute there is an index in info->attrs which points to a nlattr structure
     * in this structure the data is given
     */
    na = info->attrs[NL_A_MSG];
    did_unreg = 0;
    if (na) {
        mydata = (char *)nla_data(na);
        if (mydata == NULL) {
            printk("error while receiving data\n");
        } else {
            for (i=0; i<MAX_NL_CLIENTS; i++) {
                /* match by id, then check name */
                if (nl_map[i].id == info->snd_pid && nl_map[i].name != NULL && strncmp(nl_map[i].name, mydata, strlen(mydata)) == 0) {
                    /* free up the memory allocated for name and set it null */
                    printk("found %s at %i\n", nl_map[i].name, i);
                    did_unreg = 1;
                    kfree(nl_map[i].name);
                    nl_map[i].name = NULL;
                }
            }

            if (did_unreg) {
                printk("unregistered: %s\n", mydata);
            } else {
                printk("did not unregister: %s\n", mydata);
            }
        }
    }
    else
        printk("no info->attrs %i\n", NL_A_MSG);

    /* send a message back */
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
    msg_head = genlmsg_put(skb, 0, info->snd_seq+1, &provider_gnl_family, 0, NL_C_UNREG);
    if (msg_head == NULL) {
        rc = -ENOMEM;
        goto out;
    }
    /* add a NL_A_MSG attribute (actual value to be sent) */
    rc = nla_put_string(skb, NL_A_MSG, "unregistered"); // we always say we unregistered (if we didn't, they weren't registered to begin with)
    if (rc != 0)
        goto out;

    /* finalize the message */
    rc = genlmsg_end(skb, msg_head);
    if (rc < 0)
        goto out;

    /* send the message back */
    rc = genlmsg_unicast(skb,info->snd_pid );
    if (rc != 0) {
        skb = NULL; // already free at this point
        goto out;
    }

    return 0;

 out:
    printk("an error occured in provider_unreg:\n");

    if (skb != NULL) nlmsg_free(skb); // if we didn't use actually send, free it here
    return rc;
}

/* commands: mapping between the command enumeration and the actual function */
struct genl_ops provider_gnl_ops_unreg = {
    .cmd = NL_C_UNREG,
    .flags = 0,
    .policy = provider_genl_policy,
    .doit = provider_unreg,
    .dumpit = NULL,
};

//---------------------------------------------------------------------------
// Periodic timeout function
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data) {
    printk("hello there\n");
    provider_reregister();
    mod_timer(&timeout_timer_list, jiffies+((TIMEOUT_IN_MSECONDS*HZ)/1000));
}

//---------------------------------------------------------------------------
// Work item to run through final initialization step
//---------------------------------------------------------------------------
static void init_final(struct work_struct * work) {
    printk("doing final initialization\n");
    provider_reregister(); // tell all connected clients to reregister
    mod_timer(&timeout_timer_list, jiffies+((TIMEOUT_IN_MSECONDS*HZ)/1000));

    // initialize heartbeat callback function
    switch (heartbeat_add(nl_hb)) {
        case HB_SUCCESS: printk("Heartbeat Callback Initialized\n"); break;
        case HB_DEAD: printk("Heartbeat Callback Error: DEAD\n"); break;
        case HB_INVALID: printk("Heartbeat Callback Error: INVALID\n"); break;
    }
}

static int __init gnKernel_init(void) {
    int rc, i;
    printk("INIT GENERIC NETLINK PROVIDER MODULE\n");

// for testing
at91_set_gpio_output(AT91_PIN_PB8, ACTIVE_LOW);

    /* start with a blank mapping */
    for (i=0; i<MAX_NL_CLIENTS; i++) {
        nl_map[i].name = NULL;
    }

    /* register new family */
    rc = genl_register_family(&provider_gnl_family);
    if (rc != 0)
        goto failure;
    /* register multicast group */
    rc = genl_register_mc_group(&provider_gnl_family, &nl_event_mcgrp);
    if (rc != 0) {
        printk("register mc group: %i\n",rc);
        genl_unregister_family(&provider_gnl_family);
        goto failure;
    }
    /* register functions (commands) of the new family */
    rc = genl_register_ops(&provider_gnl_family, &provider_gnl_ops_echo);
    if (rc != 0) {
        printk("register echo ops: %i\n",rc);
        genl_unregister_family(&provider_gnl_family);
        goto failure;
    }
    rc = genl_register_ops(&provider_gnl_family, &provider_gnl_ops_reg);
    if (rc != 0) {
        printk("register reg ops: %i\n",rc);
        genl_unregister_family(&provider_gnl_family);
        goto failure;
    }

    rc = genl_register_ops(&provider_gnl_family, &provider_gnl_ops_unreg);
    if (rc != 0) {
        printk("register unreg ops: %i\n",rc);
        genl_unregister_family(&provider_gnl_family);
        goto failure;
    }

    // do some of the initialization after init has been called
    INIT_WORK(&init_work, init_final);
    schedule_work(&init_work);

    return 0;

  failure:
    printk("an error occured while inserting the generic netlink example module\n");
    return -1;


}

static void __exit gnKernel_exit(void) {
    int ret, i;
    printk("EXIT GENERIC NETLINK PROVIDER MODULE\n");

    /* clear heartbeat callback */
    heartbeat_clear(nl_hb);

    /* delete timers */
    del_timer(&timeout_timer_list);

    /* free up map */
    for (i=0; i<MAX_NL_CLIENTS; i++) {
        if (nl_map[i].name != NULL) {
            kfree(nl_map[i].name);
        }
    }
    /* unregister multicast group */
    genl_unregister_mc_group(&provider_gnl_family, &nl_event_mcgrp);
    /* unregister the functions */
    ret = genl_unregister_ops(&provider_gnl_family, &provider_gnl_ops_echo);
    if (ret != 0) {
        printk("unregister echo ops: %i\n",ret);
        return;
    }
    ret = genl_unregister_ops(&provider_gnl_family, &provider_gnl_ops_reg);
    if (ret != 0) {
        printk("unregister reg ops: %i\n",ret);
        return;
    }
    ret = genl_unregister_ops(&provider_gnl_family, &provider_gnl_ops_unreg);
    if (ret != 0) {
        printk("unregister unreg ops: %i\n",ret);
        return;
    }
    /* unregister the family */
    ret = genl_unregister_family(&provider_gnl_family);
    if (ret !=0) {
        printk("unregister family %i\n",ret);
    }
}


module_init(gnKernel_init);
module_exit(gnKernel_exit);
MODULE_LICENSE("GPL");


