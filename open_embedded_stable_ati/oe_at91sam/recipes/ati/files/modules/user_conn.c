#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>


#include "netlink_user.h"

// kill switch to program
static int close_nicely = 0;
static int close_angrily = 0;
static void quitproc() {
    close_nicely = 1;
}

static int ignore_cb(struct nl_msg *msg, void *arg) {
    return NL_OK;
}

static int parse_cb(struct nl_msg *msg, void *arg) {
    struct nlattr *attrs[GEN_INT8_A_MAX+1];
    struct nlmsghdr *nlh = nlmsg_hdr(msg);
    struct genlmsghdr *ghdr = nlmsg_data(nlh);

    // Validate message and parse attributes
//printf("Parsing: %i:%s\n", ghdr->cmd, (char *)arg);
    switch (ghdr->cmd) {
        case NL_C_BATTERY:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                 int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
//printf("battery attribute: %i\n", value);
            }

            break;
        case NL_C_FAILURE:
            genlmsg_parse(nlh, 0, attrs, GEN_STRING_A_MAX, generic_int8_policy);

            if (attrs[GEN_STRING_A_MSG]) {
                char *data = nla_get_string(attrs[GEN_STRING_A_MSG]);
//printf("failure attribute: %s\n", data);
            }

            break;
        default:
            fprintf(stderr, "failure to parse unkown command\n"); fflush(stderr);
            break;
    }

    close_nicely = 1; // exit loop
    return NL_OK;
}

// rough idea of how many connections we'll deal with and the max we'll deal with in a single loop
#define MAX_CONNECTIONS 128
#define MAX_EVENTS 16

int main(int argc, char **argv) {
    struct nl_handle *handle;
    struct nl_msg *msg;
    int family, retval;

    // install signal handlers
    signal(SIGINT, quitproc);
    signal(SIGQUIT, quitproc);

    // Allocate a new netlink handle
    handle = nl_handle_alloc();

    // join ATI group
    nl_join_groups(handle, 1);

    // Connect to generic netlink handle on kernel side
    genl_connect(handle);

    // set up epoll
    struct epoll_event ev, events[MAX_EVENTS];
    int efd; // epoll file descriptor
    struct timeval tv;
    int nfds; // number of file descriptors (returned in a single epoll_wait call)
    efd = epoll_create(MAX_CONNECTIONS);

    // add netlink socket to epoll
    int nl_fd = nl_socket_get_fd(handle); // netlink socket file descriptor
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = nl_fd;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, nl_fd, &ev) < 0) {
        fprintf(stderr, "epoll insertion error\n");
        return -1;
    }

    // Ask kernel to resolve family name to family id
    family = genl_ctrl_resolve(handle, "ATI");

// for testing
int j;
for (j=0;j<1000000 && !close_angrily;j++) {
close_nicely = 0;
    // Construct a generic netlink by allocating a new message, fill in
    // the header and append a simple string attribute.
    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, NL_C_BATTERY, 1);
    nla_put_u8(msg, GEN_INT8_A_MSG, 1);

    // Send message over netlink handle
    nl_send_auto_complete(handle, msg);

    // Free message
    nlmsg_free(msg);

    // Prepare handle to receive the answer by specifying the callback
    // function to be called for valid messages.
    retval = nl_socket_modify_cb(handle, NL_CB_VALID, NL_CB_CUSTOM, parse_cb, (void*)"VALID");
    retval |= nl_socket_modify_cb(handle, NL_CB_FINISH, NL_CB_CUSTOM, ignore_cb, (void*)"FINISH");
    retval |= nl_socket_modify_cb(handle, NL_CB_OVERRUN, NL_CB_CUSTOM, ignore_cb, (void*)"OVERRUN");
    retval |= nl_socket_modify_cb(handle, NL_CB_SKIPPED, NL_CB_CUSTOM, ignore_cb, (void*)"SKIPPED");
    retval |= nl_socket_modify_cb(handle, NL_CB_ACK, NL_CB_CUSTOM, ignore_cb, (void*)"ACK");
    retval |= nl_socket_modify_cb(handle, NL_CB_MSG_IN, NL_CB_CUSTOM, ignore_cb, (void*)"MSG_IN");
    retval |= nl_socket_modify_cb(handle, NL_CB_MSG_OUT, NL_CB_CUSTOM, ignore_cb, (void*)"MSG_OUT");
    retval |= nl_socket_modify_cb(handle, NL_CB_INVALID, NL_CB_CUSTOM, ignore_cb, (void*)"INVALID");
    retval |= nl_socket_modify_cb(handle, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, ignore_cb, (void*)"SEQ_CHECK");
    retval |= nl_socket_modify_cb(handle, NL_CB_SEND_ACK, NL_CB_CUSTOM, ignore_cb, (void*)"SEND_ACK");

    // wait for netlink messages
    while(!close_nicely && !close_angrily) {
        // wait for response, timeout, or cancel
        nfds = epoll_wait(efd, events, MAX_EVENTS, 30000); // timeout at 30 seconds

        if (nfds == -1) {
            /* select cancelled or other error */
            fprintf(stderr, "select cancelled: "); fflush(stderr);
            switch (errno) {
                case EBADF: fprintf(stderr, "EBADF\n"); break;
                case EFAULT: fprintf(stderr, "EFAULT\n"); break;
                case EINTR: fprintf(stderr, "EINTR\n"); break;
                case EINVAL: fprintf(stderr, "EINVAL\n"); break;
                default: fprintf(stderr, "say what? %i\n", errno); break;
            }
            close_angrily = 1; // exit loop
        } else if (nfds == 0) {
            // timeout occurred
        } else {
            int i;
            for (i=0; i<nfds; i++) {
                if (events[i].data.fd == nl_fd) {
                    // Wait for the answer and receive it
                    retval = nl_recvmsgs_default(handle);
                } else {
                    fprintf(stderr, "no file in fd-set\n"); fflush(stderr);
                }
            }
        }
    }
// for testing
}

    // destroy netlink handle
    nl_handle_destroy(handle);

    return 0;
}


