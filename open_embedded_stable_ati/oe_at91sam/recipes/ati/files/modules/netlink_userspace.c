#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/genetlink.h>

#include "netlink_user.h"

/*
 * Generic macros for dealing with netlink sockets.
 */
#define GENLMSG_DATA(glh) ((void *)(NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh) (NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)
#define NLA_DATA(na) ((void *)((char*)(na) + NLA_HDRLEN))
//#define NLA_PAYLOAD(len) (len - NLA_HDRLEN)

typedef struct nl_msg {
    struct nlmsghdr n;
    struct genlmsghdr g;
    char buf[256];
} nl_msg_t;

/*
 * Create a raw netlink socket and bind
 */
int create_nl_socket(int groups) {
    socklen_t addr_len;
    int fd;
    struct sockaddr_nl local;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&local, 0, sizeof(local));
    local.nl_family = AF_NETLINK;
    local.nl_pid = getpid();
    local.nl_groups = groups;
    if (bind(fd, (struct sockaddr *) &local, sizeof(local)) < 0)
        goto error;

    return fd;
 error:
    close(fd);
    return -1;
}

/*
 * Send netlink message to kernel
 */
int sendto_fd(int s, const char *buf, int bufLen) {
    struct sockaddr_nl nladdr;
    int r;

    memset(&nladdr, 0, sizeof(nladdr));
    nladdr.nl_family = AF_NETLINK;

    while ((r = sendto(s, buf, bufLen, 0, (struct sockaddr *) &nladdr,
        sizeof(nladdr))) < bufLen) {
        if (r > 0) {
            buf += r;
            bufLen -= r;
        } else if (errno != EAGAIN)
            return -1;
    }
    return 0;
}


/*
 * Probe the controller in genetlink to find the family id
 * for the ATI family
 */
int get_family_id(int sd) {
    struct nl_msg family_req;

    struct nl_msg ans;

    int id;
    struct nlattr *na;
    int rep_len;

    /* Get family name */
    family_req.n.nlmsg_type = GENL_ID_CTRL;
    family_req.n.nlmsg_flags = NLM_F_REQUEST;
    family_req.n.nlmsg_seq = 0;
    family_req.n.nlmsg_pid = getpid();
    family_req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    family_req.g.cmd = CTRL_CMD_GETFAMILY;
    family_req.g.version = 0x1;

    na = (struct nlattr *) GENLMSG_DATA(&family_req);
    na->nla_type = CTRL_ATTR_FAMILY_NAME;
    na->nla_len = strlen("ATI") + 1 + NLA_HDRLEN;
    strcpy(NLA_DATA(na), "ATI");

    family_req.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

    if (sendto_fd(sd, (char *) &family_req, family_req.n.nlmsg_len) < 0)
        return -1;

    rep_len = recv(sd, &ans, sizeof(ans), 0);
    if (rep_len < 0) {
        perror("recv");
        return -1;
    }

    /* Validate response message */
    if (!NLMSG_OK((&ans.n), rep_len)) {
        fprintf(stderr, "invalid reply message\n");
        return -1;
    }

    if (ans.n.nlmsg_type == NLMSG_ERROR) { /* error */
        fprintf(stderr, "received error\n");
        return -1;
    }

    na = (struct nlattr *) GENLMSG_DATA(&ans);
    na = (struct nlattr *) ((char *) na + NLA_ALIGN(na->nla_len));
    if (na->nla_type == CTRL_ATTR_FAMILY_ID) {
        id = *(__u16 *) NLA_DATA(na);
    }
    return id;
}

int send_command(int nl_sd, int id, int command, char *message, size_t mlength) {
    /* verify message */
    if (mlength < 1 || mlength > 254) {
        fprintf(stderr, "invalid message length: %i\n", mlength);
        return -1;
    }

    /* Send command needed */
    struct nl_msg req;
    req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    req.n.nlmsg_type = id;
    req.n.nlmsg_flags = NLM_F_REQUEST;
    req.n.nlmsg_seq = 60;
    req.n.nlmsg_pid = getpid();
    req.g.cmd = command; // command to use

    /* compose message */
    struct nlattr *na;
    na = (struct nlattr *) GENLMSG_DATA(&req);
    na->nla_type = NL_A_MSG; // attribute to use
    na->nla_len = mlength+NLA_HDRLEN; //message length
    memcpy(NLA_DATA(na), message, mlength);
    req.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

    /* send message */
    struct sockaddr_nl nladdr;
    int r;

    memset(&nladdr, 0, sizeof(nladdr));
    nladdr.nl_family = AF_NETLINK;

    r = sendto(nl_sd, (char *)&req, req.n.nlmsg_len, 0,  
      (struct sockaddr *) &nladdr, sizeof(nladdr));

    return r;
}

int rec_response(int nl_sd, char *result, size_t rlength) {
    /* receive response */
    struct nl_msg ans;
    int rep_len = recv(nl_sd, &ans, sizeof(ans), 0);

    /* Validate response message */
    if (ans.n.nlmsg_type == NLMSG_ERROR) { /* error */
        fprintf(stderr, "error received NACK - leaving \n");
        return -1;
    }
    if (rep_len < 0) {
        fprintf(stderr, "error receiving reply message via Netlink \n");
        return -1;
    }
    if (!NLMSG_OK((&ans.n), rep_len)) {
        fprintf(stderr, "invalid reply message received via Netlink\n");
        return -1;
    }

    rep_len = GENLMSG_PAYLOAD(&ans.n);
    if (rep_len > rlength) {
        fprintf(stderr, "message too big\n");
        return -1;
    }

    /* parse reply message */
    struct nlattr *na;
    na = (struct nlattr *) GENLMSG_DATA(&ans);
    char * res = (char *)NLA_DATA(na);
    memcpy(result, res, rep_len);

    return rep_len;
}


