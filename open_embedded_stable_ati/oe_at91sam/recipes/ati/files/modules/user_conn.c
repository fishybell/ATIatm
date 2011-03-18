#include <stdio.h>
#include <string.h>

#include "netlink_user.h"

int main(int argc, char **argv) {
    // initial message
    char * message = "hello world!";
    int mlength = strlen(message)+1;
    int i, loop = 1;

    /* parse command line arguments */
    for (i=1; i<argc; i++) {
        if ((strncmp("-msg", argv[i], 4) == 0 && (i+1<argc)) || i+1 == argc) {
            // message passed in on command line (via -msg or as last parameter)
            if (i+1 == argc) {--i;} // message is last parameter
            message = argv[++i];
            mlength = strlen(message)+1;
        } else if ((strncmp("-loop", argv[i], 5) == 0 || strncmp("-n", argv[i], 2) == 0) && (i+1<argc)) {
            // number of times to send the message passed in on command line (via -loop or -n)
            int retval;
            if ((retval = sscanf(argv[++i], "%i", &loop)) != 1) {
                fprintf(stderr, "(%i) invalid argument to %s: %s\n", retval, argv[i-1], argv[i]);
                return -1;
            }
        } else {
            fprintf(stderr, "invalid argument: %s\n", argv[i]);
            return -1;
        }
    }

    // create netlink socket (no groups)
    int nl_sd; /* the socket */
    nl_sd = create_nl_socket(0);
    if (nl_sd < 0) {
        fprintf(stderr, "create failure\n");
        return -1;
    }
    int id = get_family_id(nl_sd);

    // send/receive "loop" number of times
    while (loop--) {
        /* send the command to the kernel */
        if (send_command(nl_sd, id, NL_C_ECHO, message, mlength) < 0) {
            return -1;
        }
   
        /* receive response */
        char result[256];
        if (rec_response(nl_sd, result, sizeof(result)) > 0) {
            printf("kernel says: %s\n", result);
        } else {
            return -1;
        }
    }

    close(nl_sd);

    return 0;
}

