#include "mcp.h"
#include "rf.h"
#include "slaveboss.h"
#include "rf_debug.h"

int verbose;   // so debugging works right in all modules

#define MAX_EVENTS 4

void print_help(int exval) {
   printf("slavetest [-h] [-v num] [-i ip_address]  [-p port]\n\n");
   printf("  -h            print this help and exit\n");
   printf("  -i 127.0.0.1  set RF connect address\n");
   printf("  -p 14004      set RF connect port\n");
   printf("  -v 0          set verbosity bits\n");
   exit(exval);
}

void DieWithError(char *errorMessage){
    char buf[200];
    strerror_r(errno,buf,200);
    DCMSG(RED,"slaveboss %s %s \n", errorMessage,buf);
    exit(1);
}

void setblocking(int sock) {
   int opts, yes=1;

   // disable Nagle's algorithm so we send messages as discrete packets
//   if (setsockopt(sock, SOL_SOCKET, TCP_NODELAY, &yes, sizeof(int)) == -1) {
//      DCMSG(RED, "Could not disable Nagle's algorithm\n");
//      perror("setsockopt(TCP_NODELAY)");
//   }

   // generic file descriptor setup
   opts = fcntl(sock, F_GETFL); // grab existing flags
   if (opts < 0) {
      DieWithError("fcntl(F_GETFL)");
   }
   opts = (opts ^ O_NONBLOCK); // remove nonblock from existing flags
   if (fcntl(sock, F_SETFL, opts) < 0) {
      DieWithError("fcntl(F_SETFL)");
   }
}

void setnonblocking(int sock, int keep) {
   int opts, yes=1;

   // disable Nagle's algorithm so we send messages as discrete packets
//   if (setsockopt(sock, SOL_SOCKET, TCP_NODELAY, &yes, sizeof(int)) == -1) {
//      DCMSG(RED, "Could not disable Nagle's algorithm\n");
//      perror("setsockopt(TCP_NODELAY)");
//   }

   if (keep) { // only do this once
      if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int)) < 0) { // set keepalive so we disconnect on link failure or timeout
         DieWithError("setsockopt(SO_KEEPALIVE)");
      }
   }

   // generic file descriptor setup
   opts = fcntl(sock, F_GETFL); // grab existing flags
   if (opts < 0) {
      DieWithError("fcntl(F_GETFL)");
   }
   opts = (opts | O_NONBLOCK); // add in nonblock to existing flags
   if (fcntl(sock, F_SETFL, opts) < 0) {
      DieWithError("fcntl(F_SETFL)");
   }
}

// packet formatting routines
char *do_STATUS_REQ(int a, int *num) {
   LB_status_req_t *pkt = malloc(sizeof(LB_status_req_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_EXPOSE(int a, int *num) {
   LB_expose_t *pkt = malloc(sizeof(LB_expose_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->expose = num[2];
   pkt->hitmode = num[3];
   pkt->tokill = num[4];
   pkt->react = num[5];
   pkt->mfs = num[6];
   pkt->thermal = num[7];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_MOVE(int a, int *num) {
   LB_move_t *pkt = malloc(sizeof(LB_move_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->direction = num[2];
   pkt->speed = num[3];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_CONFIGURE_HIT(int a, int *num) {
   LB_configure_t *pkt = malloc(sizeof(LB_configure_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->hitmode = num[2];
   pkt->tokill = num[3];
   pkt->react = num[4];
   pkt->sensitivity = num[5];
   pkt->timehits = num[6];
   pkt->hitcountset = num[7];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_GROUP_CONTROL(int a, int *num) {
   LB_group_control_t *pkt = malloc(sizeof(LB_group_control_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->gcmd = num[2];
   pkt->gaddr = num[3];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_AUDIO_CONTROL(int a, int *num) {
   LB_audio_control_t *pkt = malloc(sizeof(LB_audio_control_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->function = num[2];
   pkt->volume = num[3];
   pkt->playmode = num[4];
   pkt->track = num[5];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_POWER_CONTROL(int a, int *num) {
   LB_power_control_t *pkt = malloc(sizeof(LB_power_control_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->pcmd = num[2];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_PYRO_FIRE(int a, int *num) {
   LB_pyro_fire_t *pkt = malloc(sizeof(LB_pyro_fire_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->zone = num[2];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_STATUS_RESP_LIFTER(int a, int *num) {
   LB_status_resp_lifter_t *pkt = malloc(sizeof(LB_status_resp_lifter_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->hits = num[2];
   pkt->expose = num[3];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_STATUS_RESP_MOVER(int a, int *num) {
   LB_status_resp_mover_t *pkt = malloc(sizeof(LB_status_resp_mover_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->hits = num[2];
   pkt->expose = num[3];
   pkt->speed = num[4];
   pkt->dir = num[5];
   pkt->location = num[6];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_STATUS_RESP_EXT(int a, int *num) {
   LB_status_resp_ext_t *pkt = malloc(sizeof(LB_status_resp_ext_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->hits = num[2];
   pkt->expose = num[3];
   pkt->speed = num[4];
   pkt->dir = num[5];
   pkt->react = num[6];
   pkt->location = num[7];
   pkt->hitmode = num[8];
   pkt->tokill = num[9];
   pkt->sensitivity = num[10];
   pkt->timehits = num[11];
   pkt->fault = num[12];
   set_crc8(pkt);
   return (char*)pkt;
}



char *do_STATUS_NO_RESP(int a, int *num) {
   LB_packet_t *pkt = malloc(sizeof(LB_packet_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_QEXPOSE(int a, int *num) {
   LB_packet_t *pkt = malloc(sizeof(LB_packet_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_QCONCEAL(int a, int *num) {
   LB_packet_t *pkt = malloc(sizeof(LB_packet_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_DEVICE_REG(int a, int *num) {
   LB_device_reg_t *pkt = malloc(sizeof(LB_device_reg_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->dev_type = num[2];
   pkt->devid = num[3];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_REQUEST_NEW(int a, int *num) {
   LB_request_new_t *pkt = malloc(sizeof(LB_request_new_t));
   pkt->cmd = num[0];
   pkt->reregister = num[1];
   pkt->low_dev = num[2];
   pkt->high_dev = num[3];
   pkt->slottime = num[4];
   set_crc8(pkt);
   return (char*)pkt;
}


char *do_DEVICE_ADDR(int a, int *num) {
   LB_device_addr_t *pkt = malloc(sizeof(LB_device_addr_t));
   pkt->cmd = num[0];
   pkt->addr = num[1];
   pkt->new_addr = num[2];
   set_crc8(pkt);
   return (char*)pkt;
}


// convert a script line into an RF packet
char *rfFromLine(char *readline, const ssize_t r, int *mnum) {
   // remove carraige returns and line feeds from end
   if (readline[r-1] == '\n' || readline[r-1] == '\r') {readline[r-1] = '\0';}
   if (readline[r-2] == '\n' || readline[r-2] == '\r') {readline[r-2] = '\0';}
   *mnum = -1;

   // check for blank line
   if (r <= 2) {
      if (readline[0] == 'X') {
         // end of script
         exit(0);
      } else {
         // blank line
         printf("\n");
         return NULL;
      }
   } else  if (readline[0] == '#') { // check for comment
      DCMSG(BLACK, "Comment: %s", readline);
      return NULL;
   } else {
      int a, num[16];
      //DCMSG(RED, "Real line: %s", readline);
      a = sscanf(readline, "%i %i %i %i %i %i %i %i %i %i %i %i %i %i %i %i", &num[0], &num[1],  &num[2],  &num[3],  &num[4],  &num[5],  &num[6],  &num[7],  &num[8],  &num[9],  &num[10],  &num[11],  &num[12],  &num[13],  &num[14],  &num[15]);
//      for (i=0; i<a; i++) {
//         DCMSG(RED, "Num %i: %i", i, num[i]);
//      }
      if (a > 0) {
         *mnum = num[0];
         switch (num[0]) {
            case LBC_STATUS_REQ:
               return do_STATUS_REQ(a, num);
            case LBC_EXPOSE:
               return do_EXPOSE(a, num);
            case LBC_MOVE:
               return do_MOVE(a, num);
            case LBC_CONFIGURE_HIT:
               return do_CONFIGURE_HIT(a, num);
            case LBC_GROUP_CONTROL:
               return do_GROUP_CONTROL(a, num);
            case LBC_AUDIO_CONTROL:
               return do_AUDIO_CONTROL(a, num);
            case LBC_POWER_CONTROL:
               return do_POWER_CONTROL(a, num);
            case LBC_PYRO_FIRE:
               return do_PYRO_FIRE(a, num);
            case LBC_STATUS_RESP_LIFTER:
               return do_STATUS_RESP_LIFTER(a, num);
            case LBC_STATUS_RESP_MOVER:
               return do_STATUS_RESP_MOVER(a, num);
            case LBC_STATUS_RESP_EXT:
               return do_STATUS_RESP_EXT(a, num);
            case LBC_STATUS_NO_RESP:
               return do_STATUS_NO_RESP(a, num);
            case LBC_QEXPOSE:
               return do_QEXPOSE(a, num);
            case LBC_QCONCEAL:
               return do_QCONCEAL(a, num);
            case LBC_DEVICE_REG:
               return do_DEVICE_REG(a, num);
            case LBC_REQUEST_NEW:
               return do_REQUEST_NEW(a, num);
            case LBC_DEVICE_ADDR:
               return do_DEVICE_ADDR(a, num);
            default:
               return NULL;
         }
      } else {
         return NULL;
      }
   }
}

int main(int argc, char **argv) {
   int i, opt, lport, n = 1, sock, efd;
   struct sockaddr_in raddr;
   struct epoll_event ev, events[MAX_EVENTS]; // temporary event, main event list
   memset(&raddr, 0, sizeof(struct sockaddr_in));
   raddr.sin_family = AF_INET;
   raddr.sin_addr.s_addr = inet_addr("127.0.0.1");    // Any incoming interface
   raddr.sin_port = htons(14004);                     // Listen port
   verbose=0;

   // process the arguments
   //  -i 127.0.0.1  connect addresss
   //  -p 14004      connect port number
   //  -v 2          Verbosity level
   while ((opt = getopt(argc, argv, "hv:i:p:")) != -1) {
      switch (opt) {
         case 'h':
            print_help(0);
            break;
         case 'v':
            verbose = atoi(optarg);
            break;
         case 'i':
            raddr.sin_addr.s_addr = inet_addr(optarg);
            break;
         case 'p':
            raddr.sin_port = htons(atoi(optarg));
            break;
         case ':':
            fprintf(stderr, "Error - Option `%c' needs a value\n\n", optopt);
            print_help(1);
            break;
         case '?':
            fprintf(stderr, "Error - No such option: `%c'\n\n", optopt);
            print_help(1);
            break;
      }
   }
   DCMSG(BLACK,"SLAVETEST: verbosity is set to 0x%x", verbose);
   DCMSG(BLACK,"SLAVETEST: slaveboss address = %s:%d", inet_ntoa(raddr.sin_addr),htons(raddr.sin_port));


   // connect to given address
   DCMSG(MAGENTA, "CREATING SOCKET");
   if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
      DieWithError("Failed to open socket");
   }
   DCMSG(MAGENTA, "CONNECTING");
   if (connect(sock, (struct sockaddr *)&raddr, sizeof(raddr)) < 0) {
      DieWithError("Failed to connect");
   }
   setnonblocking(sock, 1); // set keep alive
   DCMSG(MAGENTA, "CONNECTED");

   // setup epoll
   efd = epoll_create(MAX_CONNECTIONS);
   memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN; // only for reading to start
   // listen to the socket always
   ev.data.fd = sock; // remember for later
   if (epoll_ctl(efd, EPOLL_CTL_ADD, sock, &ev) < 0) {
      DieWithError("epoll socket insertion error");
   }

   // run scripted tests
   for (i = optind; i < argc; i++, n++) {
      FILE *fp;
      char *readline = NULL;
      size_t len = 0;
      ssize_t r;
      printf ("Reading file %i %s\n", n, argv[i]);
      fp = fopen(argv[i], "r");
      if (fp == NULL) {
         DieWithError("Error opening file");
      }

      // read all lines of file
      while ((r = getline(&readline, &len, fp)) != -1) {
         int times = 15; // go through epoll loop 15 times for each line
         // get rf packet to send
         int num = -1;
         char *packet = rfFromLine(readline, r, &num);

         // good packet for this line? ie. not a comment/blank line
         if (packet != NULL) {
            char extra_buf[256];
            int extra_buf_c=0;
            // debug the outgoing line
            debugRF(BLUE, packet);

            // set epoll to watch for write
            memset(&ev, 0, sizeof(ev));
            ev.events = EPOLLIN | EPOLLOUT;
            if (epoll_ctl(efd, EPOLL_CTL_MOD, sock, &ev) < 0) {
               DieWithError("epoll socket change to write error");
            }

            // try to do writing and reading
            while (times-- > 0) {
               int nfds;
               // wait for epoll (1 second timeout)
               nfds = epoll_wait(efd, events, MAX_EVENTS, 1000); // 1 second timeout

               // timeout, write or read?
               if (nfds <= 0) {
                  // timeout
               } else {
                  for (n=0; n<nfds; n++) {
                     if (events[n].events & EPOLLOUT) {
                        // write
                        DCMSG_HEXB(BLUE, "WRITING DATA:", packet, RF_size(num));
                        int c = write(sock, packet, RF_size(num));
                        while (c < RF_size(num)) {
                           if (c <= 0) {
                              DCMSG(RED, "TRY AGAIN LATER");
                              continue; // try again later
                           } else {
                              while (c < RF_size(num)) {
                                 // keep trying
                                 DCMSG(RED, "...almost...");
                                 int s = write(sock, packet+c, RF_size(num)-c);
                                 if (s < 0) {
                                    DieWithError("Failed to write packet");
                                 }
                                 c += s; // increase size written so far
                              }
                           }
                        }
                        // set epoll to watch for reading only again
                        memset(&ev, 0, sizeof(ev));
                        ev.events = EPOLLIN;
                        if (epoll_ctl(efd, EPOLL_CTL_MOD, sock, &ev) < 0) {
                           DieWithError("epoll socket change to write error");
                        }
                     }
                     if (events[n].events & EPOLLIN || events[n].events & EPOLLPRI) {
                        // use buffer that was left over from last time first
                        char buf[sizeof(LB_packet_t)];
                        LB_packet_t *tpkt = (LB_packet_t*)&buf;
                        memcpy(buf, extra_buf, extra_buf_c);
                        // read
                        int c = read(sock, buf + extra_buf_c, sizeof(LB_packet_t));
                        extra_buf_c = 0; // reset extra buffer
                        if (c < 0) {
                           DieWithError("Failed to read packet");
                        } else {
                           DCMSG(CYAN, "Read %i of %i for cmd %i", c, RF_size(tpkt->cmd), tpkt->cmd);
                           setblocking(sock);
                           while (c < RF_size(tpkt->cmd)) {
                              int s = read(sock, buf + c, sizeof(LB_packet_t)-c);
                              if (s < 0) {
                                 DieWithError("Failed to read more packet");
                              }
                              c += s; // increase size read so far
                           }
                           setnonblocking(sock, 0); // leave keep alive alone
                        }
                        // did we read too much?
                        if (c > RF_size(tpkt->cmd)) {
                           memcpy(extra_buf, buf + RF_size(tpkt->cmd), RF_size(tpkt->cmd) - c);
                           extra_buf_c = RF_size(tpkt->cmd) - c;
                        }
                        // output what we did read
                        debugRF(MAGENTA, buf);
                        DCMSG_HEXB(MAGENTA, "READ DATA:", buf, RF_size(tpkt->cmd));
                     }
                  }
               }
            }

            // free packet memory
            free(packet);
         }
      }

      // free up memory once at end (getline will call realloc)
      free(readline);
   }
   return 0;
}

