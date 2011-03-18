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

/* commands: enumeration of all commands (functions), 
 * used by userspace application to identify command to be ececuted
 */
enum {
    NL_C_UNSPEC,
    NL_C_ECHO,
    NL_C_REG,
    NL_C_UNREG,
    __NL_C_MAX,
};
#define NL_C_MAX (__NL_C_MAX - 1)

int create_nl_socket(int groups);
int sendto_fd(int s, const char *buf, int bufLen);
int get_family_id(int sd);
int send_command(int nl_sd, int id, int command, char *message, size_t mlength);
int rec_response(int nl_sd, char *result, size_t rlength);

#endif
