#include <net/genetlink.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "netlink_user.h"

/* attribute policy: defines which attribute has which type (e.g int, char * etc)
 * possible values defined in net/netlink.h 
 */
static struct nla_policy provider_genl_policy[NL_A_MAX + 1] = {
    [NL_A_MSG] = { .type = NLA_NUL_STRING },
};

#define VERSION_NR 1
/* family definition */
static struct genl_family provider_gnl_family = {
    .id = GENL_ID_GENERATE,         //genetlink should generate an id
    .hdrsize = 0,
    .name = "ATI",        //the name of this family, used by userspace application
    .version = VERSION_NR,                   //version number  
    .maxattr = NL_A_MAX,
};

/* an echo command, receives a message, prints it and sends another message back */
int provider_echo(struct sk_buff *skb_2, struct genl_info *info) {
    struct nlattr *na;
    struct sk_buff *skb;
    int rc;
    void *msg_head;
    char * mydata;

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
       u8 command index (why do we need this?)
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
    genlmsg_end(skb, msg_head);

    /* send the message back */
    rc = genlmsg_unicast(skb,info->snd_pid );
    if (rc != 0)
        goto out;
    return 0;

 out:
    printk("an error occured in provider_echo:\n");

      return 0;
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
    int rc;
    void *msg_head;
    char * mydata;

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
        else
            printk("registered: %s\n", mydata);
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
       u8 command index (why do we need this?)
    */
    msg_head = genlmsg_put(skb, 0, info->snd_seq+1, &provider_gnl_family, 0, NL_C_REG);
    if (msg_head == NULL) {
        rc = -ENOMEM;
        goto out;
    }
    /* add a NL_A_MSG attribute (actual value to be sent) */
    rc = nla_put_string(skb, NL_A_MSG, "hello world from kernel space (register)");
    if (rc != 0)
        goto out;

    /* finalize the message */
    genlmsg_end(skb, msg_head);

    /* send the message back */
    rc = genlmsg_unicast(skb,info->snd_pid );
    if (rc != 0)
        goto out;
    return 0;

 out:
    printk("an error occured in provider_reg:\n");

      return 0;
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
    int rc;
    void *msg_head;
    char * mydata;

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
        else
            printk("unregistered: %s\n", mydata);
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
       u8 command index (why do we need this?)
    */
    msg_head = genlmsg_put(skb, 0, info->snd_seq+1, &provider_gnl_family, 0, NL_C_UNREG);
    if (msg_head == NULL) {
        rc = -ENOMEM;
        goto out;
    }
    /* add a NL_A_MSG attribute (actual value to be sent) */
    rc = nla_put_string(skb, NL_A_MSG, "hello world from kernel space (unregister)");
    if (rc != 0)
        goto out;

    /* finalize the message */
    genlmsg_end(skb, msg_head);

    /* send the message back */
    rc = genlmsg_unicast(skb,info->snd_pid );
    if (rc != 0)
        goto out;
    return 0;

 out:
    printk("an error occured in provider_unreg:\n");

      return 0;
}

/* commands: mapping between the command enumeration and the actual function */
struct genl_ops provider_gnl_ops_unreg = {
    .cmd = NL_C_UNREG,
    .flags = 0,
    .policy = provider_genl_policy,
    .doit = provider_unreg,
    .dumpit = NULL,
};

static int __init gnKernel_init(void) {
    int rc;
    printk("INIT GENERIC NETLINK PROVIDER MODULE\n");

    /* register new family */
    rc = genl_register_family(&provider_gnl_family);
    if (rc != 0)
        goto failure;
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

    return 0;

  failure:
    printk("an error occured while inserting the generic netlink example module\n");
    return -1;


}

static void __exit gnKernel_exit(void) {
    int ret;
    printk("EXIT GENERIC NETLINK PROVIDER MODULE\n");
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


