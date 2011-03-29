#ifndef NETLINK_USER_H
#define NETLINK_USER_H

/* attributes (variables): the index in this enum is used as a reference for the type,
 *             userspace application has to indicate the corresponding type
 *             the policy is used for security considerations 
 */
enum {
    NL_A_UNSPEC,
    NL_A_MSG,
    __NL_A_MAX,
};
#define NL_A_MAX (__NL_A_MAX - 1)

/* attribute policy: defines which attribute has which type (e.g int, char * etc)
 * possible values defined in net/netlink.h 
 */
static struct nla_policy provider_genl_policy[NL_A_MAX + 1] = {
    [NL_A_MSG] = { .type = NLA_STRING },
};

/* commands: enumeration of all commands (functions), 
 * used by userspace application to identify command to be ececuted
 */
enum {
    NL_C_UNSPEC,
    NL_C_ECHO,
    NL_C_REG,
    NL_C_UNREG,
    NL_C_REREG,
    __NL_C_MAX,
};
#define NL_C_MAX (__NL_C_MAX - 1)

#define TRIGGER_MIN 1
#define TRIGGER_MAX 1000

#endif
