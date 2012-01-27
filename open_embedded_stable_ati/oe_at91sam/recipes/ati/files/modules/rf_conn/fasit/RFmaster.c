#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>


// tcp port we'll listen to for new connections
#define PORT 4004

// size of client buffer
#define CLIENT_BUFFER 1024

// kill switch to program
static int close_nicely = 0;
static void quitproc() {
    close_nicely = 1;
}

int main(int argc, char **argv) {
    int retval, yes=1;
    int client, listener; // socket file descriptors
    socklen_t addrlen;
    struct sockaddr_in serveraddr, local;

    // install signal handlers
    signal(SIGINT, quitproc);
    signal(SIGQUIT, quitproc);

    // get the listener socket
    if((listener = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Server-socket() error ");

        // just exit
        return -1;
    }

    // "address already in use" error message
    if(setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("Server-setsockopt() error ");
        return -1;
    }

    // bind to socket
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = INADDR_ANY;
    serveraddr.sin_port = htons(PORT);
    memset(&(serveraddr.sin_zero), '\0', 8);

    if(bind(listener, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
        perror("Server-bind() error ");
        return -1;
    }

    // listen on socket
    if(listen(listener, 10) == -1) {
        perror("Server-listen() error ");
        return -1;
    }

    // accept new clients
    while (!close_nicely) {
        addrlen = sizeof(local);
        client = accept(listener, (struct sockaddr *) &local,
                        &addrlen);
        if(client < 0){
            perror("accept");
            close_nicely = 1;
        }
        int pid = fork();
        if (pid == 0) {
            // child process, break out of loop
//printf("forked child\n");
            close(listener);
            break;
        } else if (pid < 0) {
            perror("fork");
        }
    }
    // did the parent exit?
    if (close_nicely) {
        close(listener);
        return 0;
    }
//printf("is child\n");

    // set up epoll
    struct epoll_event ev, events[MAX_EVENTS];
    struct timeval tv;
    int nfds; // number of file descriptors (returned in a single epoll_wait call)
    efd = epoll_create(MAX_CONNECTIONS);

    // add client socket to epoll
    memset(&ev, 0, sizeof(ev));
    setnonblocking(client);
    ev.events = EPOLLIN;
    ev.data.fd = client;
    char *client_buf = malloc(CLIENT_BUFFER); // create a read buffer for the client
    memset(client_buf, '\0', CLIENT_BUFFER);
    if (epoll_ctl(efd, EPOLL_CTL_ADD, client, &ev) < 0) {
        fprintf(stderr, "epoll insertion error: client\n");
        return -1;
    }

    g_handle = handlecreate_nl_handle(client, ATI_GROUP);

    // wait for netlink and socket messages
    while(!close_nicely) {
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
            close_nicely = 1; // exit loop
        } else if (nfds == 0) {
            // timeout occurred
//printf("child %i timed out\n", client);
        } else {
            int i, b, rsize;
            for (i=0; i<nfds; i++) {
                if (events[i].data.fd == nl_fd) {
                    // netlink talking 
//printf("nl\n");
                    nl_recvmsgs_default(g_handle); // will call callback functions
                } else if (events[i].data.fd == client) {
                    // client talking
//printf("sk %i:", client);
                    b = strnlen(client_buf, CLIENT_BUFFER); // see where we left off
                    // is there any space left in the buffer?
//printf("%i", b);
                    if (b >= CLIENT_BUFFER) {
                        close_nicely = 1; // exit loop
                    }
                    // read client
                    rsize = read(client, client_buf+b, CLIENT_BUFFER-b); // read into buffer at appropriate place, at most to end of buffer
//printf(":%i\n", rsize);
                    // parse buffer and send any netlink messages needed
                    if (rsize == 0 || telnet_client(g_handle, client_buf, client) != 0) {
//printf("sk %i closing\n", client);
                        close_nicely = 1; // exit loop
                    }
                }
            }
        }
    }
    // the client is done
    close(client); // close socket
    free(client_buf); // free buffer

    // destroy netlink handle
    nl_handle_destroy(g_handle);

    return 0;
}

// Verifies the input is a correctly formed MAC address
int isMac(char* cmd) {
	int arg1, arg2;
	arg2 = 0;
    int i = 0;

	while(i <= 16) {
		int hex = cmd[arg2+i];
		if(i==2 || i==5 || i==8 || i==11 || i==14) {
			// Check for the colon to be in the right place
			if(hex != 58) {
				return 0;
			}
		}	// Make sure all numbers fall within the hexidecimal range
		else if ( (hex >= 48 && hex <= 58) || (hex >= 97 && hex <= 102) || (hex >= 65 && hex <= 70) ) {
			// nothing in here
		} else {
			return 0;
		}
		i++;	
	}
	return 1;
}



