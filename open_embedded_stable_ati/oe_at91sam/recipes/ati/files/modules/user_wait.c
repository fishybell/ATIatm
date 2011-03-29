#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

#include "netlink_user.h"

// rough idea of how many connections we'll deal with and the max we'll deal with in a single loop
#define MAX_CONNECTIONS 128
#define MAX_EVENTS 16

static int close_nicely = 0;

void quitproc() {
    close_nicely = 1;
}

int nl_register(int nl_sd, int id, char *name, int mlength, int trigger) {
    /* send the register command to the kernel */
    char *message = malloc(mlength + 6); // 1 for space, 4 for number up to 9999, 1 for null terminator
    sprintf(message, "%s %i", name, trigger);
    if (send_command(nl_sd, id, NL_C_REG, message, strlen(message)+1) < 0) {
        fprintf(stderr, "failed to send register message\n");
        return -1;
    }
    free(message);

    return 0;
}

int nl_unregister(int nl_sd, int id, char *name, int mlength) {
    /* send the unregister command to the kernel */
    if (send_command(nl_sd, id, NL_C_UNREG, name, mlength) < 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    // initial name
    char * name = "test";
    int mlength = strlen(name)+1;
    int i, trigger = 5;
    int id = 0; // generic netlink family id (may change)

    /* parse command line arguments */
    for (i=1; i<argc; i++) {
        if ((strncmp("-msg", argv[i], 4) == 0 && (i+1<argc)) || i+1 == argc) {
            // name passed in on command line (via -msg or as last parameter)
            if (i+1 == argc) {--i;} // name is last parameter
            name = argv[++i];
            mlength = strlen(name)+1;
        } else if ((strncmp("-num", argv[i], 4) == 0 || strncmp("-n", argv[i], 2) == 0) && (i+1<argc)) {
            // number of times to send the message passed in on command line (via -num or -n)
            int retval;
            if ((retval = sscanf(argv[++i], "%i", &trigger)) != 1 || trigger < TRIGGER_MIN || trigger > TRIGGER_MAX) {
                fprintf(stderr, "(%i) invalid argument to %s: %s\n", retval, argv[i-1], argv[i]);
                return -1;
            }
        } else {
            fprintf(stderr, "invalid argument: %s\n", argv[i]);
            return -1;
        }
    }

    /* install signal handlers */
    signal(SIGINT, quitproc);
    signal(SIGQUIT, quitproc);

    // create netlink socket (all groups)
    int nl_sd; /* the socket */
    nl_sd = create_nl_socket(1);
    if (nl_sd < 0) {
        fprintf(stderr, "create failure\n");
        return -1;
    }
    //nl_register(nl_sd, name, mlength, trigger);
  
    /* set up select */
    struct epoll_event ev, events[MAX_EVENTS];
    int efd; // epoll file descriptor
    struct timeval tv;
    int retval, nfds;
    efd = epoll_create(MAX_CONNECTIONS);

    /* add socket */
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = nl_sd;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, nl_sd, &ev) < 0) {
        fprintf(stderr, "epoll insertion error\n");
        return -1;
    }


    /* wait for further responses */
    while(!close_nicely) {
        /* wait for response, timeout, or cancel */
        nfds = epoll_wait(efd, events, MAX_EVENTS, 30000); // timeout at 30 seconds

        if (nfds == -1) {
            /* select cancelled or other error */
            fprintf(stderr, "select cancelled: "); fflush(stderr);
            switch (errno) {
                case EBADF: fprintf(stderr, "EBADF\n"); break;
                case EFAULT: fprintf(stderr, "EFAULT\n"); break;
                case EINTR: fprintf(stderr, "EINTR\n"); break;
                case EINVAL: fprintf(stderr, "EINVAL\n"); break;
                default: fprintf(stderr, "say what?\n"); break;
            }
            break;
        } else if (nfds == 0) {
            /* timeout occurred, refresh netlink socket */
            printf("timeout\n"); fflush(stdout);
            epoll_ctl(efd, EPOLL_CTL_DEL, nl_sd, NULL);
            close(nl_sd);
            nl_sd = create_nl_socket(1);
            id = 0; // reset id
            if (epoll_ctl(efd, EPOLL_CTL_ADD, nl_sd, &ev) < 0) {
                fprintf(stderr, "epoll reinsertion error\n");
                return -1;
            }
        } else {
            for (i=0; i<nfds; i++) {
                if (events[i].data.fd == nl_sd) {
                    /* receive response */
                    char result[256];
                    if (rec_response(nl_sd, result, sizeof(result)) > 0) {
                        /* retrieve id if we don't have one */
                        if (id == 0) {
                            id = get_family_id(nl_sd);
                        }
                        /* parse message and respond */
                        if (strncmp(result,"reregister",10) == 0) {
                            nl_unregister(nl_sd, id, name, mlength);
                            nl_register(nl_sd, id, name, mlength, trigger);
                        } else if (strncmp(result,"registered",10) == 0) {
                            // nothing on successful register
                        } else if (strncmp(result,"unregistered",12) == 0) {
                            // nothing on successful unregister
                        } else {
                            printf("kernel says: %s\n", result); fflush(stdout);
                        }
                    } else {
                        return -1;
                    }
                } else {
                    printf("no file in fd-set\n"); fflush(stdout);
                }
            }
        }
    }

    if (id != 0) {
        nl_unregister(nl_sd, id, name, mlength);
    }

    close(nl_sd);

    return 0;
}

