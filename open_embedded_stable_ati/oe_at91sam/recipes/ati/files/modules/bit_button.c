#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>

#include "netlink_user.h"
#include "scenario.h"

// kill switch to program
static int close_nicely = 0;
static void quitproc() {
    close_nicely = 1;
}

// global family id for ATI netlink family
int family;

// bit button has been pressed (long-press)
void handle_bit_test_long(struct nl_handle *handle, int is_on) {
    // build scenario
    // Step 1 - Conceal
    // Step 2 - Wait for Concealed
    // Step 3 - Enable MFS in burst mode
    // Step 4 - Expose
    // Step 5 - Wait 3 seconds
    // Step 6 - Conceal
    const char *scen = "{Send;R_LIFTER;NL_C_EXPOSE;1;00} -- Send Conceal to lifter \
                        {DoWait;Nothing;EVENT_DOWN;15000;Nothing} -- Wait 15 seconds for conceal \
                        {Send;R_LIFTER;NL_C_ACCESSORY;1;%s} -- Enable MFS (data installed below) \
                        {Send;R_LIFTER;NL_C_EXPOSE;1;01} -- Send Expose to lifter \
                        {Delay;3000;;;} -- Wait 3 seconds \
                        {Send;R_LIFTER;NL_C_EXPOSE;1;00} -- Send Conceal to lifter";
    char scen_buf[1024];
    char hex_buf[1024];
    // build accessory configuration message
    struct accessory_conf acc_c;
    acc_c.acc_type  = ACC_NES_MFS;
    acc_c.request = 0;     // not a request
    acc_c.on_exp = 1;      // on
    acc_c.on_kill = 2;     // 2 = deactivate on kill
    acc_c.ex_data1 = 1;    // do burst
    acc_c.ex_data2 = 5;    // burst 5 times
    acc_c.on_time = 50;    // on 50 milliseconds
    acc_c.off_time = 100;  // off 100 milliseconds
    acc_c.repeat_delay = 2 * 3;  // when burst, burst every 6 half-seconds
    acc_c.repeat = 63;     // infinite repeat
    acc_c.start_delay = 2 * 1;   // start after 2 half-seconds
    hex_encode_attr((void*)&acc_c, sizeof(acc_c), hex_buf); // use helper function to build scenario
    snprintf(scen_buf, 1024, scen, hex_buf); // format scenario

//printf("BIT: sending NL_C_SCENARIO: %s\n", scen_buf);
    // send scenario
    struct nl_msg *msg;
    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, NL_C_SCENARIO, 1);
    nla_put_string(msg, GEN_STRING_A_MSG, scen_buf);

    // Send message over netlink handle
    nl_send_auto_complete(handle, msg);

    // Free message
    nlmsg_free(msg);
}

// bit button might have been pressed, move the lifter up or down
void handle_bit_test(struct nl_handle *handle, int is_on) {
    // ignore button not pressed events
    if (is_on != 1) { return; }

    // toggle lifter position
    struct nl_msg *msg;
    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, NL_C_EXPOSE, 1);
    nla_put_u8(msg, GEN_INT8_A_MSG, TOGGLE);

    // Send message over netlink handle
    nl_send_auto_complete(handle, msg);

    // Free message
    nlmsg_free(msg);
}

void handle_bit_move(struct nl_handle *handle, int type) {
    // move button pressed? send the correct command back to the kernel
    struct nl_msg *msg;
    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, NL_C_MOVE, 1);

    // fill with the correct movement data
    switch (type) {
        case BIT_MOVE_FWD:
//printf("BIT: sending FWD\n");
            nla_put_u16(msg, GEN_INT16_A_MSG, 32768 + 15); // fwd at 1.5 mph
            break;
        case BIT_MOVE_REV:
//printf("BIT: sending REV\n");
            nla_put_u16(msg, GEN_INT16_A_MSG, 32768 - 15); // rev at 1.5 mph
            break;
        case BIT_MOVE_STOP:
//printf("BIT: sending STOP\n");
            nla_put_u16(msg, GEN_INT16_A_MSG, VELOCITY_STOP); // stop
            break;
    }

    // Send message over netlink handle
    nl_send_auto_complete(handle, msg);

    // Free message
    nlmsg_free(msg);
}

static int ignore_cb(struct nl_msg *msg, void *arg) {
    return NL_OK;
}

static int parse_cb(struct nl_msg *msg, void *arg) {
    struct nlattr *attrs[BIT_A_MAX+1];
    struct nlmsghdr *nlh = nlmsg_hdr(msg);
    struct genlmsghdr *ghdr = nlmsg_data(nlh);
    struct nl_handle *handle = (struct nl_handle*)arg;

    // Validate message and parse attributes
//printf("BIT: Parsing: %i\n", ghdr->cmd);
    switch (ghdr->cmd) {
#if 0
        case NL_C_EXPOSE:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                // received response for exposure status request
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                toggle_lifter_exposure(handle, value); // do the opposite of the current exposure
            }
            break;
#endif // if 0
        case NL_C_BIT:
            genlmsg_parse(nlh, 0, attrs, BIT_A_MAX, bit_event_policy);

            if (attrs[BIT_A_MSG]) {
                // bit event data
                struct bit_event *bit = (struct bit_event*)nla_data(attrs[HIT_A_MSG]);
                if (bit != NULL) {
                    switch (bit->bit_type) {
                        case BIT_TEST:
                            handle_bit_test(handle, bit->is_on);
                            break;
                        case BIT_MOVE_FWD:
                        case BIT_MOVE_REV:
                        case BIT_MOVE_STOP:
                            handle_bit_move(handle, bit->bit_type);
                            break;
                        case BIT_LONG_PRESS:
                            handle_bit_test_long(handle, bit->is_on);
                            break;
                    }
                }
            }
            break;
        case NL_C_FAILURE:
            genlmsg_parse(nlh, 0, attrs, GEN_STRING_A_MAX, generic_string_policy);

            if (attrs[GEN_STRING_A_MSG]) {
                char *data = nla_get_string(attrs[GEN_STRING_A_MSG]);
                //printf("BIT: failure attribute: %s\n", data);
            }

            break;
        default:
//            fprintf(stderr, "BIT: failure to parse unkown command\n"); fflush(stderr);
            break;
    }

    return NL_OK;
}

// rough idea of how many connections we'll deal with and the max we'll deal with in a single loop
#define MAX_CONNECTIONS 2
#define MAX_EVENTS 4

int main(int argc, char **argv) {
    struct nl_handle *handle;
    int retval, yes=1;

    // install signal handlers
    signal(SIGINT, quitproc);
    signal(SIGQUIT, quitproc);

    // Allocate a new netlink handle
    handle = nl_handle_alloc();

    // join ATI group (for multicast messages)
    nl_join_groups(handle, ATI_GROUP);

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
        fprintf(stderr, "BIT: epoll insertion error: nl_fd\n");
        return -1;
    }

    // Ask kernel to resolve family name to family id
    family = genl_ctrl_resolve(handle, "ATI");

    // Prepare handle to receive the answer by specifying the callback
    // function to be called for valid messages.
    retval = nl_socket_modify_cb(handle, NL_CB_VALID, NL_CB_CUSTOM, parse_cb, (void*)handle); // the arg pointer is our netlink handle
    retval |= nl_socket_modify_cb(handle, NL_CB_FINISH, NL_CB_CUSTOM, ignore_cb, (void*)"FINISH");
    retval |= nl_socket_modify_cb(handle, NL_CB_OVERRUN, NL_CB_CUSTOM, ignore_cb, (void*)"OVERRUN");
    retval |= nl_socket_modify_cb(handle, NL_CB_SKIPPED, NL_CB_CUSTOM, ignore_cb, (void*)"SKIPPED");
    retval |= nl_socket_modify_cb(handle, NL_CB_ACK, NL_CB_CUSTOM, ignore_cb, (void*)"ACK");
    retval |= nl_socket_modify_cb(handle, NL_CB_MSG_IN, NL_CB_CUSTOM, ignore_cb, (void*)"MSG_IN");
    retval |= nl_socket_modify_cb(handle, NL_CB_MSG_OUT, NL_CB_CUSTOM, ignore_cb, (void*)"MSG_OUT");
    retval |= nl_socket_modify_cb(handle, NL_CB_INVALID, NL_CB_CUSTOM, ignore_cb, (void*)"INVALID");
    retval |= nl_socket_modify_cb(handle, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, ignore_cb, (void*)"SEQ_CHECK");
    retval |= nl_socket_modify_cb(handle, NL_CB_SEND_ACK, NL_CB_CUSTOM, ignore_cb, (void*)"SEND_ACK");

    // wait for netlink and socket messages
    while(!close_nicely) {
        // wait for response, timeout, or cancel
        nfds = epoll_wait(efd, events, MAX_EVENTS, 30000); // timeout at 30 seconds

        if (nfds == -1) {
            /* select cancelled or other error */
            fprintf(stderr, "BIT: select cancelled: "); fflush(stderr);
            switch (errno) {
                case EBADF: fprintf(stderr, "EBADF\n"); break;
                case EFAULT: fprintf(stderr, "EFAULT\n"); break;
                case EINTR: fprintf(stderr, "EINTR\n"); break;
                case EINVAL: fprintf(stderr, "EINVAL\n"); break;
                default: fprintf(stderr, "say what? %i\n", errno); break;
            }
            close_nicely = 1; // exit loop
        } else if (nfds == 0) {
            // timeout occurred
        } else {
            int i, b, rsize;
            for (i=0; i<nfds; i++) {
                if (events[i].data.fd == nl_fd) {
                    // netlink talking 
//printf("BIT: nl\n");
                    nl_recvmsgs_default(handle); // will call callback functions
                }
            }
        }
    }

    // destroy netlink handle
    nl_handle_destroy(handle);

    return 0;
}


