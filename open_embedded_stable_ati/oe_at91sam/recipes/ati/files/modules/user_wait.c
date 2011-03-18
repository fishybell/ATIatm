#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "netlink_user.h"

static int close_nicely = 0;

void quitproc() {
    close_nicely = 1;
}

int main(int argc, char **argv) {
    // initial name
    char * name = "test";
    int mlength = strlen(name)+1;
    int i;

    /* parse command line arguments */
    for (i=1; i<argc; i++) {
        if ((strncmp("-msg", argv[i], 4) == 0 && (i+1<argc)) || i+1 == argc) {
            // name passed in on command line (via -msg or as last parameter)
            if (i+1 == argc) {--i;} // name is last parameter
            name = argv[++i];
            mlength = strlen(name)+1;
        } else {
            fprintf(stderr, "invalid argument: %s\n", argv[i]);
            return -1;
        }
    }

    /* install signal handlers */
    signal(SIGINT, quitproc);
    signal(SIGQUIT, quitproc);

    // create netlink socket (no groups)
    int nl_sd; /* the socket */
    nl_sd = create_nl_socket(0);
    if (nl_sd < 0) {
        fprintf(stderr, "create failure\n");
        return -1;
    }
    int id = get_family_id(nl_sd);

    /* send the register command to the kernel */
    if (send_command(nl_sd, id, NL_C_REG, name, mlength) < 0) {
        return -1;
    }
   
    /* set up select */
    fd_set rfds, wfds; // read and write file descriptor sets
    struct timeval tv;
    int retval, max_fd;
    FD_ZERO(&rfds); // reset read fd set
    FD_ZERO(&wfds); // reset write fd set
    FD_SET(nl_sd, &rfds); // insert our netlink socket into the read fd set
    FD_SET(nl_sd, &wfds); // insert our netlink socket into the write fd set
    max_fd = nl_sd + 1;

    /* wait for further responses */
    while(!close_nicely) {
        /* wait for response, timeout, or cancel */
        tv.tv_sec = 5; tv.tv_usec = 0; // reset to 5 seconds each loop
        retval = select(max_fd, &rfds, NULL, NULL, &tv); // could poll on wfds if we wanted to also write back over the netlink socket

        if (retval == -1) {
            /* select cancelled or other error */
            fprintf(stderr, "select cancelled\n");
            break;
        } else if (retval == 0) {
            /* timeout occurred */
            printf("timeout\n");
        } else {
            if (FD_ISSET(nl_sd, &rfds)) {
                /* receive response */
                char result[256];
                if (rec_response(nl_sd, result, sizeof(result)) > 0) {
                    printf("kernel says: %s\n", result);
                } else {
                    return -1;
                }
            } else {
                printf("no file in fd-set\n");
            }
        }
    }

    /* send the unregister command to the kernel */
    if (send_command(nl_sd, id, NL_C_UNREG, name, mlength) < 0) {
        return -1;
    }
 
    close(nl_sd);

    return 0;
}

