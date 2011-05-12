#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>

#include "netlink_user.h"

// tcp port we'll listen to for new connections
#define PORT 4422

// size of client buffer
#define CLIENT_BUFFER 1024

// kill switch to program
static int close_nicely = 0;
static void quitproc() {
    close_nicely = 1;
}

// utility function to properly configure a client TCP connection
void setnonblocking(int sock) {
   int opts;

   opts = fcntl(sock, F_GETFL);
   if (opts < 0) {
      perror("fcntl(F_GETFL)");
      close_nicely = 1;
   }
   opts = (opts | O_NONBLOCK);
   if (fcntl(sock, F_SETFL, opts) < 0) {
      perror("fcntl(F_SETFL)");
      close_nicely = 1;
   }
}


static int ignore_cb(struct nl_msg *msg, void *arg) {
    return NL_OK;
}

static int parse_cb(struct nl_msg *msg, void *arg) {
    struct nlattr *attrs[NL_A_MAX+1];
    struct nlmsghdr *nlh = nlmsg_hdr(msg);
    struct genlmsghdr *ghdr = nlmsg_data(nlh);
    int client = (int)arg;
    char wbuf[1024];
    wbuf[0] = '\0';

    // Validate message and parse attributes
printf("Parsing: %i:%i\n", ghdr->cmd, client);
    switch (ghdr->cmd) {
        case NL_C_BATTERY:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                // battery value percentage
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                snprintf(wbuf, 1024, "B %i\n", value);
            }

            break;
        case NL_C_EXPOSE:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                if (value == 1) {
                    // 1 for exposed
                    snprintf(wbuf, 1024, "E\n");
                } else if (value == 0) {
                    // 0 for concealed
                    snprintf(wbuf, 1024, "C\n");
                } else {
                    // uknown or moving
                    snprintf(wbuf, 1024, "S %i\n", value);
                }
            }

            break;
        case NL_C_MOVE:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                // moving at # mph
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                snprintf(wbuf, 1024, "M %i\n", value-128);
            }

            break;
        case NL_C_POSITION:
            genlmsg_parse(nlh, 0, attrs, GEN_INT16_A_MAX, generic_int16_policy);

            if (attrs[GEN_INT16_A_MSG]) {
                // # feet from home
                int value = nla_get_u16(attrs[GEN_INT16_A_MSG]) - 0x8000; // message was unsigned, fix it
                snprintf(wbuf, 1024, "A %i\n", value);
            }

            break;
        case NL_C_STOP:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                // emergency stop reply (will likely cause other messages as well)
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                snprintf(wbuf, 1024, "X\n");
            }

            break;
        case NL_C_HITS:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                // current number of hits
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                snprintf(wbuf, 1024, "H %i\n", value);
            }

            break;
        case NL_C_HIT_LOG:
            genlmsg_parse(nlh, 0, attrs, GEN_STRING_A_MAX, generic_string_policy);

            if (attrs[GEN_STRING_A_MSG]) {
                // current hit log
                char *data = nla_get_string(attrs[GEN_STRING_A_MSG]);
                snprintf(wbuf, 1024, "%s\n", data);
            }

            break;
        case NL_C_HIT_CAL:
printf("NL_C_HIT_CAL\n");
            genlmsg_parse(nlh, 0, attrs, HIT_A_MAX, hit_calibration_policy);

            if (attrs[HIT_A_MSG]) {
                // calibration data
                struct hit_calibration *hit_c = (struct hit_calibration*)nla_data(attrs[HIT_A_MSG]);
                if (hit_c != NULL) {
                    switch (hit_c->set) {
                        case HIT_OVERWRITE_NONE:
                        case HIT_OVERWRITE_ALL:
                            // all calibration data
                            snprintf(wbuf, 1024, "L %i %i %i\nY %i %i\nF %i %i\n", hit_c->seperation, hit_c->sensitivity, hit_c->blank_time, hit_c->type, hit_c->invert, hit_c->hits_to_fall, hit_c->after_fall);
                            break;
                        case HIT_OVERWRITE_OTHER:
                            // type and hits_to_fall
                            snprintf(wbuf, 1024, "Y %i %i\nF %i %i\n", hit_c->type, hit_c->invert, hit_c->hits_to_fall, hit_c->after_fall);
                            break;
                        case HIT_GET_CAL:
                        case HIT_OVERWRITE_CAL:
                            // sensitivity and seperation
                            snprintf(wbuf, 1024, "L %i %i %i\n", hit_c->seperation, hit_c->sensitivity, hit_c->blank_time);
                            break;
                        case HIT_GET_TYPE:
                        case HIT_OVERWRITE_TYPE:
                            // type only
                            snprintf(wbuf, 1024, "Y %i %i\n", hit_c->type, hit_c->invert);
                            break;
                        case HIT_GET_FALL:
                        case HIT_OVERWRITE_FALL:
                            // hits_to_fall only
                            snprintf(wbuf, 1024, "F %i %i\n", hit_c->hits_to_fall, hit_c->after_fall);
                            break;
                    }
                }
            }

            break;
        case NL_C_BIT:
            genlmsg_parse(nlh, 0, attrs, BIT_A_MAX, bit_event_policy);

            if (attrs[BIT_A_MSG]) {
                // bit event data
                struct bit_event *bit = (struct bit_event*)nla_data(attrs[HIT_A_MSG]);
                if (bit != NULL) {
                    char btyp = 'x';
                    switch (bit->bit_type) {
                        case BIT_TEST: btyp = 'T'; break;
                        case BIT_MOVE_FWD: btyp = 'F'; break;
                        case BIT_MOVE_REV: btyp = 'R'; break;
                        case BIT_MOVE_STOP: btyp = 'S'; break;
                    }
                    snprintf(wbuf, 1024, "T %c %i\n", btyp, bit->is_on);
                }
            }
            break;
        case NL_C_ACCESSORY:
            genlmsg_parse(nlh, 0, attrs, ACC_A_MAX, accessory_conf_policy);

            if (attrs[ACC_A_MSG]) {
                // calibration data
                struct accessory_conf *acc_c = (struct accessory_conf*)nla_data(attrs[ACC_A_MSG]);
                if (acc_c != NULL) {
                    switch (acc_c->acc_type) {
                        case ACC_NES_MOON_GLOW:
                            // Moon Glow data
                            snprintf(wbuf, 1024, "Q MGL");
                            break;
                        case ACC_NES_PHI:
                            // Positive Hit Indicator data
                            snprintf(wbuf, 1024, "Q PHI");
                            break;
                        case ACC_NES_MFS:
                            // Muzzle Flash Simulator data
                            snprintf(wbuf, 1024, "Q MFS");
                            break;
                        case ACC_SES:
                            // SES data
                            snprintf(wbuf, 1024, "Q SES");
                            break;
                        case ACC_SMOKE:
                            // Smoke generator data
                            snprintf(wbuf, 1024, "Q SMK");
                            break;
                        case ACC_THERMAL:
                            // Thermal device data
                            snprintf(wbuf, 1024, "Q THM");
                            break;
                        case ACC_MILES_SDH:
                            // MILES Shootback Device Holder data 
                            snprintf(wbuf, 1024, "Q MSD");
                            break;
                    }

                    // some mean different things for different accessories
                    snprintf(wbuf+5, 1024-5, " %i %i %i %i %i %i %i %i %i %i %i %i %i\n", acc_c->exists, acc_c->on_now, acc_c->on_exp, acc_c->on_hit, acc_c->on_kill, acc_c->on_time, acc_c->off_time, acc_c->start_delay, acc_c->repeat_delay, acc_c->repeat, acc_c->ex_data1, acc_c->ex_data2, acc_c->ex_data3);
                }
            }
            break;

        case NL_C_GPS:
            genlmsg_parse(nlh, 0, attrs, GPS_A_MAX, gps_conf_policy);

            if (attrs[GPS_A_MSG]) {
                // calibration data
                struct gps_conf *gps_c = (struct gps_conf*)nla_data(attrs[GPS_A_MSG]);
                if (gps_c != NULL) {
                    // field of merit, integral latitude, fractional latitude, integral longitude, fractional longitude
                    snprintf(wbuf, 1024, "G %i %i %i %i %i\n", gps_c->fom, gps_c->intLat, gps_c->fraLat, gps_c->intLon, gps_c->fraLon);
                }
            }
            break;

        case NL_C_EVENT:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                // a mover/lifter event happened
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                snprintf(wbuf, 1024, "V %i\n", value);
            }
            break;

        case NL_C_FAILURE:
            genlmsg_parse(nlh, 0, attrs, GEN_STRING_A_MAX, generic_string_policy);

            if (attrs[GEN_STRING_A_MSG]) {
                char *data = nla_get_string(attrs[GEN_STRING_A_MSG]);
                snprintf(wbuf, 1024, "failure attribute: %s\n", data);
            }

            break;
        default:
            fprintf(stderr, "failure to parse unkown command\n"); fflush(stderr);
            break;
    }

    /* write back to the client */
    if (wbuf[0] != '\0') {
        write(client, wbuf, strnlen(wbuf,1024));
    } else {
        write(client, "error\n", 6);
    }

    return NL_OK;
}

// global family id for ATI netlink family
int family;

int telnet_client(struct nl_handle *handle, char *client_buf, int client) {
    char wbuf[1024];
    wbuf[0] = '\0';
    int arg1, arg2, arg3;
    // read as many commands out of the buffer as possible
    while (1) {
        // read line from client buffer
        int i;
        char cmd[CLIENT_BUFFER];
        for (i=0; i<CLIENT_BUFFER; i++) {
            cmd[i] = client_buf[i];
            if (cmd[i] == '\n' || cmd[i] == '\r') {
                // found command, stop copying here
                cmd[i] = '\0'; // null terminate and remove carraige return
                break;
            } else if (cmd[i] == '\0') {
                // found end of read data, wait for more
                return 0;
            }
        }
        if (i>=CLIENT_BUFFER) {
            // buffer too small, kill connection
            return -1;
        }

        // clear command from buffer
        int j;
        for (j=0; j<CLIENT_BUFFER && i<CLIENT_BUFFER; j++) {
            // move characters back to beginning of buffer
            client_buf[j] = client_buf[++i];
        }
        memset(client_buf+j,'\0',CLIENT_BUFFER-j); // clear end of buffer

        // send specific command to kernel
        int nl_cmd = NL_C_UNSPEC;
        switch (cmd[0]) {
            case 'A': case 'a':
                nl_cmd = NL_C_POSITION;
                break;
            case 'B': case 'b':
                nl_cmd = NL_C_BATTERY;
                break;
            case 'C': case 'c':
                nl_cmd = NL_C_EXPOSE;
                break;
            case 'D': case 'd':
                nl_cmd = NL_C_HIT_LOG;
                break;
            case 'E': case 'e':
                nl_cmd = NL_C_EXPOSE;
                break;
            case 'F': case 'f':
                nl_cmd = NL_C_HIT_CAL;
                break;
            case 'G': case 'g':
                nl_cmd = NL_C_GPS;
                break;
            case 'H': case 'h':
                nl_cmd = NL_C_HITS;
                break;
            case 'L': case 'l':
                nl_cmd = NL_C_HIT_CAL;
                break;
            case 'M': case 'm':
                nl_cmd = NL_C_MOVE;
                break;
            case 'Q': case 'q':
                nl_cmd = NL_C_ACCESSORY;
                break;
            case 'S': case 's':
                nl_cmd = NL_C_EXPOSE;
                break;
            case 'T': case 't':
                nl_cmd = NL_C_EXPOSE;
                break;
            case 'V': case 'v':
                nl_cmd = NL_C_EVENT;
                break;
            case 'Y': case 'y':
                nl_cmd = NL_C_HIT_CAL;
                break;
            case 'X': case 'x':
                nl_cmd = NL_C_STOP;
                break;
            case '?':
                // help command
                arg1 = cmd[1] == ' ' ? 2 : 1; // next letter location in the command (ignore up to one space)
                switch (cmd[arg1]) { /* second letter */
                    case 'A': case 'a':
                        snprintf(wbuf, 1024, "Request position\nFormat: A\n");
                        break;
                    case 'B': case 'b':
                        snprintf(wbuf, 1024, "Request battery\nFormat: B\n");
                        break;
                    case 'C': case 'c':
                        snprintf(wbuf, 1024, "Conceal\nFormat: C\n");
                        break;
                    case 'D': case 'd':
                        snprintf(wbuf, 1024, "Request full hit data\nFormat: D\n");
                        break;
                    case 'E': case 'e':
                        snprintf(wbuf, 1024, "Expose\nFormat: E\n");
                        break;
                    case 'F': case 'f':
                        snprintf(wbuf, 1024, "Request fall parameters\nFormat: F\nChange fall parameters\nFormat: F (0-100)fall_at_x_hits (1|0)bob_after_fall\n");
                        break;
                    case 'G': case 'g':
                        snprintf(wbuf, 1024, "Request GPS data\nFormat: G\n");
                        break;
                    case 'H': case 'h':
                        snprintf(wbuf, 1024, "Request hit data\nFormat: H\nReset Hit data\nFormat: H 0");
                        break;
                    case 'L': case 'l':
                        snprintf(wbuf, 1024, "Request hit calibration parameters\nFormat: L\nChange hit calibration parameters\nFormat: L (1-10000)milliseconds_between_hits (1-1000)hit_desensitivity (0-50000)milliseconds_blanking_time_from_start_expose\n");
                        break;
                    case 'M': case 'm':
                        snprintf(wbuf, 1024, "Movement speed request\nFormat: M M\nStop movement\nFormat: M\nChange speed\nFormat M (-127 to 126)speed_in_mph\n");
                        break;
                    case 'Q': case 'q':
                        snprintf(wbuf, 1024, "Types of accessories: MFS, MGL, SES, PHI, MSD, SMK, THM\nRequest accessory paramaters\nFormat: Q accessory_type\nChange accessory parameters\nFormat: Q accessory_type (0|1|2)active_soon_or_immediate (0|1|2|3)active_on_full_expose_or_partial_expose_or_during_partial (0|1|2)active_or_deactive_on_hit (0|1|2)active_or_deactive_on_kill (0-60000)milliseconds_on_time (0-60000)milliseconds_off_time (0-250)halfseconds_start_delay (0-250)halfseconds_repeat_delay (0-62|63)repeat_count_or_infinite ex1 ex2 ex3\n");
                        break;
                    case 'S': case 's':
                        snprintf(wbuf, 1024, "Request exposure data\nFormat: S\n");
                        break;
                    case 'T': case 't':
                        snprintf(wbuf, 1024, "Toggle target\nFormat: T\n");
                        break;
                    case 'Y': case 'y':
                        snprintf(wbuf, 1024, "Request hit sensor type\nFormat: Y\nChange hit sensor type\nFormat: Y (0|1|2)mechanical_or_nchs_or_miles (0|1)invert_input_line\n");
                        break;
                    case 'X': case 'x':
                        snprintf(wbuf, 1024, "Emergency stop\nFormat: X\n");
                        break;
                    default: // print default help
                        snprintf(wbuf, 1024, "A: Position\nB: Battery\nC: Conceal\nD: Hit Data\nE: Expose\nF: Fall\nG: GPS\nH: HITS\nL: Hit Calibration\nM: Movement\nQ: Accessory\nS: Exposure Status\nT: Toggle\nY: Hit Sensor Type\nX: Emergency Stop\n");
                        break;
                }
                write(client, wbuf, strnlen(wbuf,1024));
                break;
            case '\0':
                // empty string, just ignore
                break;
            default:
printf("unrecognized command '%c'\n", cmd[0]);
                break;
        }

        if (nl_cmd != NL_C_UNSPEC) {
            // Construct a generic netlink by allocating a new message
            struct nl_msg *msg;
            struct hit_calibration hit_c;
            struct accessory_conf acc_c;
            struct gps_conf gps_c;
            msg = nlmsg_alloc();
            genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, nl_cmd, 1);

            // fill in attribute according to message sent
            switch (nl_cmd) {
                case NL_C_BATTERY:
                    // request battery message
                    nla_put_u8(msg, GEN_INT8_A_MSG, 1);
                    break;
                case NL_C_EXPOSE:
                    if (cmd[0] == 'E' || cmd[0] == 'e') {
                        nla_put_u8(msg, GEN_INT8_A_MSG, EXPOSE); // expose command
                    } else if (cmd[0] == 'C' || cmd[0] == 'c') {
                        nla_put_u8(msg, GEN_INT8_A_MSG, CONCEAL); // conceal command
                    } else if (cmd[0] == 'T' || cmd[0] == 't') {
                        nla_put_u8(msg, GEN_INT8_A_MSG, TOGGLE); // toggle command
                    } else {
                        nla_put_u8(msg, GEN_INT8_A_MSG, EXPOSURE_REQ); // exposure status request
                    }
                    break;
                case NL_C_MOVE:
                    if (cmd[1] == '\0') {
                        nla_put_u8(msg, GEN_INT8_A_MSG, VELOCITY_REQ); // velocity request
                    } else if (sscanf(cmd+1, "%i", &arg1) == 1) {
                        if (arg1 > 126 || arg1 < -127) {
                            arg1 = 0; // stay away from the edge conditions
                        }
                        nla_put_u8(msg, GEN_INT8_A_MSG, 128+arg1); // move request (add 128 as we're not signed)
                    } else {
                        nla_put_u8(msg, GEN_INT8_A_MSG, VELOCITY_STOP); // stop
                    }
                    break;
                case NL_C_POSITION:
                    // request position message
                    nla_put_u16(msg, GEN_INT16_A_MSG, 1);
                    break;
                case NL_C_STOP:
                    // emergency stop message
                    nla_put_u8(msg, GEN_INT8_A_MSG, 1);
                    break;
                case NL_C_HITS:
                    if (cmd[1] == '\0') {
                        nla_put_u8(msg, GEN_INT8_A_MSG, HIT_REQ); // request hits message
                    } else if (sscanf(cmd+1, "%i", &arg1) == 1) {
                        if (arg1 == 0) {
                            nla_put_u8(msg, GEN_INT8_A_MSG, arg1); // reset hits (to X) message
                        } else {
                            nla_put_u8(msg, GEN_INT8_A_MSG, HIT_REQ); // request hits message
                        }
                    } else {
                        nla_put_u8(msg, GEN_INT8_A_MSG, HIT_REQ); // request hits message
                    }
                    break;
                case NL_C_HIT_LOG:
                    // hit log message
                    nla_put_string(msg, GEN_STRING_A_MSG, ""); // ignored
                    break;
                case NL_C_HIT_CAL:
                    arg2 = sscanf(cmd+1, "%i", &arg1);
                    memset(&hit_c, 0, sizeof(struct hit_calibration));
                    switch (cmd[0]) {
                        case 'L': case 'l':
                            if (arg2 == 1) {
                                // set calibration message
                                if (sscanf(cmd+1, "%i %i %i", &arg1, &arg2, &arg3) == 3) {
                                    hit_c.seperation = arg1;
                                    hit_c.sensitivity = arg2;
                                    hit_c.blank_time = arg3;
                                    hit_c.set = HIT_OVERWRITE_CAL;
                                }
                            } else {
                                // get calibration bounds request
                                hit_c.set = HIT_GET_CAL;
                            }
                            break;
                        case 'Y': case 'y':
                            if (arg2 == 1) {
                                if (sscanf(cmd+1, "%i %i", &arg1, &arg2) == 2) {
                                    // set type and invert values message
                                    hit_c.type = arg1;
                                    hit_c.invert = arg3;
                                    hit_c.set = HIT_OVERWRITE_TYPE;
                                }
                            } else {
                                // get type value request
                                hit_c.set = HIT_GET_TYPE;
                            }
                            break;
                        case 'F': case 'f':
                            if (arg2 == 1) {
                                if (sscanf(cmd+1, "%i %i", &arg1, &arg2) == 2) {
                                    // set hits_to_fall and after_fall values message
                                    hit_c.hits_to_fall = arg1;
                                    hit_c.after_fall = arg2;
                                    hit_c.set = HIT_OVERWRITE_FALL;
                                }
                            } else {
                                // get hits_to_fall value request
                                hit_c.set = HIT_GET_FALL;
                            }
                            break;
                    }
                    // put calibration data in message
                    nla_put(msg, HIT_A_MSG, sizeof(struct hit_calibration), &hit_c);
                    break;
                case NL_C_ACCESSORY:
                    arg1 = cmd[1] == ' ' ? 2 : 1; // next letter location in the command (ignore up to one space)
                    arg2 = arg1 + 3; // start of arguments
                    // find the type of accessory
                    memset(&acc_c, 0, sizeof(struct accessory_conf));
                    switch (cmd[arg1]) { /* first letter of second group */
                        case 'M' : case 'm' :
                           switch (cmd[arg1 + 1]) { /* second letter */
                               case 'F' : case 'f' :
                                   switch (cmd[arg1 + 2]) { /* third letter */
                                       case 'S' : case 's' :
                                           acc_c.acc_type = ACC_NES_MFS;
                                           break;
                                   }
                                   break;
                               case 'G' : case 'g' :
                                   switch (cmd[arg1 + 2]) { /* third letter */
                                       case 'L' : case 'l' :
                                           acc_c.acc_type = ACC_NES_MOON_GLOW;
                                           break;
                                   }
                                   break;
                               case 'S' : case 's' :
                                   switch (cmd[arg1 + 2]) { /* third letter */
                                       case 'D' : case 'd' :
                                           acc_c.acc_type = ACC_MILES_SDH;
                                           break;
                                   }
                                   break;
                           }
                           break;
                        case 'P' : case 'p' :
                           switch (cmd[arg1 + 1]) { /* second letter */
                               case 'H' : case 'h' :
                                   switch (cmd[arg1 + 2]) { /* third letter */
                                       case 'I' : case 'i' :
                                           acc_c.acc_type = ACC_NES_PHI;
                                           break;
                                   }
                                   break;
                           }
                           break;
                        case 'S' : case 's' :
                           switch (cmd[arg1 + 1]) { /* second letter */
                               case 'E' : case 'e' :
                                   switch (cmd[arg1 + 2]) { /* third letter */
                                       case 'S' : case 's' :
                                           acc_c.acc_type = ACC_SES;
                                           break;
                                   }
                                   break;
                               case 'M' : case 'm' :
                                   switch (cmd[arg1 + 2]) { /* third letter */
                                       case 'K' : case 'k' :
                                           acc_c.acc_type = ACC_SMOKE;
                                           break;
                                   }
                                   break;
                           }
                           break;
                        case 'T' : case 't' :
                           switch (cmd[arg1 + 1]) { /* second letter */
                               case 'H' : case 'h' :
                                   switch (cmd[arg1 + 2]) { /* third letter */
                                       case 'M' : case 'm' :
                                           acc_c.acc_type = ACC_THERMAL;
                                           break;
                                   }
                                   break;
                           }
                           break;
                    }

                    // grab as many pieces as we can get (always in same order for all accessory types)
                    unsigned int req = 1, onn = 0, one = 0, onh = 0, onk = 0, ont = 0, oft = 0, std = 0, rpd = 0, rpt = 0, ex1 = 0, ex2 = 0, ex3 = 0; // placeholders as we can't take address of bit-field for sscanf
//                    req = onn =  one = onh = onk = std = rpd = ex1 = ex2 = ex3 = 0; // zero by default
                    if (sscanf(cmd+arg2, "%i %i %i %i %i %i %i %i %i %i %i %i %i", &onn, &one, &onh, &onk, &ont, &oft, &std, &rpd, &rpt, &ex1, &ex2, &ex3) > 0) {
                        req = 0;
                    }
                    acc_c.request = req;
                    acc_c.exists = 0;
                    acc_c.on_now = onn;
                    acc_c.on_exp = one;
                    acc_c.on_hit = onh;
                    acc_c.on_kill = onk;
                    acc_c.on_time = ont;
                    acc_c.off_time = oft;
                    acc_c.start_delay = std;
                    acc_c.repeat_delay = rpd;
                    acc_c.repeat = rpt;
                    acc_c.ex_data1 = ex1;
                    acc_c.ex_data2 = ex2;
                    acc_c.ex_data3 = ex3;

//printf("Q X%iX %i %i %i %i %i %i %i %i %i %i %i %i %i\n", acc_c.acc_type, acc_c.exists, acc_c.on_now, acc_c.on_exp, acc_c.on_hit, acc_c.on_kill, acc_c.on_time, acc_c.off_time, acc_c.start_delay, acc_c.repeat_delay, acc_c.repeat, acc_c.ex_data1, acc_c.ex_data2, acc_c.ex_data3);

                    // put configuration data in message
                    nla_put(msg, ACC_A_MSG, sizeof(struct accessory_conf), &acc_c);
                    break;

                case NL_C_GPS:
                    // for request, everything is zeroed out
                    memset(&gps_c, 0, sizeof(struct gps_conf));

                    // put gps request data in message
                    nla_put(msg, GPS_A_MSG, sizeof(struct gps_conf), &gps_c);
                    break;
                case NL_C_EVENT:
                    if (sscanf(cmd+1, "%i", &arg1) == 1) {
                        nla_put_u8(msg, GEN_INT8_A_MSG, arg1); // create event X
                    }
                    break;
            }

            // Send message over netlink handle
            nl_send_auto_complete(handle, msg);

            // Free message
            nlmsg_free(msg);
        }
    }
    return 0;
}

// rough idea of how many connections we'll deal with and the max we'll deal with in a single loop
#define MAX_CONNECTIONS 2
#define MAX_EVENTS 4

int main(int argc, char **argv) {
    struct nl_handle *handle;
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
printf("forked child\n");
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
printf("is child\n");

    // Allocate a new netlink handle
    handle = nl_handle_alloc();

    // join ATI group (for multicast messages)
    nl_join_groups(handle, 1);

    // Connect to generic netlink handle on kernel side
    genl_connect(handle);

    // set up epoll
    struct epoll_event ev, events[MAX_EVENTS];
    int efd; // epoll file descriptor
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

    // add netlink socket to epoll
    int nl_fd = nl_socket_get_fd(handle); // netlink socket file descriptor
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = nl_fd;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, nl_fd, &ev) < 0) {
        fprintf(stderr, "epoll insertion error: nl_fd\n");
        return -1;
    }

    // Ask kernel to resolve family name to family id
    family = genl_ctrl_resolve(handle, "ATI");

    // Prepare handle to receive the answer by specifying the callback
    // function to be called for valid messages.
    retval = nl_socket_modify_cb(handle, NL_CB_VALID, NL_CB_CUSTOM, parse_cb, (void*)client); // pass client file descriptor
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
printf("nl\n");
                    nl_recvmsgs_default(handle); // will call callback functions
                } else if (events[i].data.fd == client) {
                    // client talking
printf("sk %i:", client);
                    b = strnlen(client_buf, CLIENT_BUFFER); // see where we left off
                    // is there any space left in the buffer?
printf("%i", b);
                    if (b >= CLIENT_BUFFER) {
                        close_nicely = 1; // exit loop
                    }
                    // read client
                    rsize = read(client, client_buf+b, CLIENT_BUFFER-b); // read into buffer at appropriate place, at most to end of buffer
printf(":%i\n", rsize);
                    // parse buffer and send any netlink messages needed
                    if (rsize == 0 || telnet_client(handle, client_buf, client) != 0) {
printf("sk %i closing\n", client);
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
    nl_handle_destroy(handle);

    return 0;
}


