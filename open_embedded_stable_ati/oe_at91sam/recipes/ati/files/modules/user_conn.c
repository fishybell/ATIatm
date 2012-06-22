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

#include "defaults.h"
#include "netlink_user.h"
#include "fasit/faults.h"
#include "target_generic_output.h"

// tcp port we'll listen to for new connections
#define PORT 4422

// size of client buffer
#define CLIENT_BUFFER 1024

// global connection junk
static int efd, nl_fd; // epoll file descriptor and netlink file descriptor
static struct nl_handle *g_handle;
static struct nl_handle *handlecreate_nl_handle(int client, int group);

// global family id for ATI netlink family
int family;

// kill switch to program
static int close_nicely = 0;
static void quitproc() {
    close_nicely = 1;
}

static void stopMover() {
    struct nl_msg *msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, NL_C_MOVE, 1);
    nla_put_u16(msg, GEN_INT16_A_MSG, VELOCITY_STOP); // stop
    nl_send_auto_complete(g_handle, msg);
    nlmsg_free(msg);
    close_nicely = 0;
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
//printf("Parsing: %i:%i\n", ghdr->cmd, client);
    switch (ghdr->cmd) {
        case NL_C_BATTERY:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                // battery value percentage
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                if (value == BATTERY_SHUTDOWN) {
                    snprintf(wbuf, 1024, "K\n");
                } else {
                    snprintf(wbuf, 1024, "B %i\n", value);
                }
            }

            break;
        case NL_C_EXPOSE:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                if (value == 1) {
                    // 1 for exposed
                    //snprintf(wbuf, 1024, "E\n");
					snprintf(wbuf, 1024, "S %i\n", value);
                } else if (value == 0) {
                    // 0 for concealed
                    //snprintf(wbuf, 1024, "C\n");
					snprintf(wbuf, 1024, "S %i\n", value);
                } else {
                    // uknown or moving
                    snprintf(wbuf, 1024, "S %i\n", value);
                }
            }

            break;
        case NL_C_MOVE:
            genlmsg_parse(nlh, 0, attrs, GEN_INT16_A_MAX, generic_int16_policy);

            if (attrs[GEN_INT16_A_MSG]) {
                // moving at # mph
                int value = nla_get_u16(attrs[GEN_INT16_A_MSG]);
                snprintf(wbuf, 1024, "M %f\n", ((float)(value-32768))/10.0);
            }

            break;
        case NL_C_SLEEP:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                // wake/sleep
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                snprintf(wbuf, 1024, "P %i\n", value);
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
//printf("NL_C_HIT_CAL\n");
            genlmsg_parse(nlh, 0, attrs, HIT_A_MAX, hit_calibration_policy);

            if (attrs[HIT_A_MSG]) {
                // calibration data
                struct hit_calibration *hit_c = (struct hit_calibration*)nla_data(attrs[HIT_A_MSG]);
                if (hit_c != NULL) {
                    switch (hit_c->set) {
                        case HIT_OVERWRITE_NONE:
                        case HIT_OVERWRITE_ALL:
                            // all calibration data
                            snprintf(wbuf, 1024, "L %i %i %i %i\nY %i %i\nF %i %i\n", hit_c->seperation, hit_c->sensitivity, hit_c->blank_time, hit_c->enable_on, hit_c->type, hit_c->invert, hit_c->hits_to_kill, hit_c->after_kill);
                            break;
                        case HIT_OVERWRITE_OTHER:
                            // type and hits_to_kill
                            snprintf(wbuf, 1024, "Y %i %i\nF %i %i\n", hit_c->type, hit_c->invert, hit_c->hits_to_kill, hit_c->after_kill);
                            break;
                        case HIT_GET_CAL:
                        case HIT_OVERWRITE_CAL:
                            // sensitivity and seperation
                            snprintf(wbuf, 1024, "L %i %i %i %i\n", hit_c->seperation, hit_c->sensitivity, hit_c->blank_time, hit_c->enable_on);
                            break;
                        case HIT_GET_TYPE:
                        case HIT_OVERWRITE_TYPE:
                            // type only
                            snprintf(wbuf, 1024, "Y %i %i\n", hit_c->type, hit_c->invert);
                            break;
                        case HIT_GET_KILL:
                        case HIT_OVERWRITE_KILL:
                            // hits_to_kill only
                            snprintf(wbuf, 1024, "F %i %i\n", hit_c->hits_to_kill, hit_c->after_kill);
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
                    char *btyp = "x";
                    switch (bit->bit_type) {
                        case BIT_TEST: btyp = "TEST"; break;
                        case BIT_MOVE_FWD: btyp = "FWD"; break;
                        case BIT_MOVE_REV: btyp = "REV"; break;
                        case BIT_MOVE_STOP: btyp = "STOP"; break;
                        case BIT_MODE: btyp = "MODE"; break;
                        case BIT_KNOB: btyp = "KNOB"; break;
                        case BIT_LONG_PRESS: btyp = "LONG"; break;
                    }
                    snprintf(wbuf, 1024, "BIT %s %i\n", btyp, bit->is_on);
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
                        case ACC_NES_MGL:
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
                switch (value) {
                  case EVENT_RAISE: // start of raise
                     snprintf(wbuf, 1024, "V %i start of raise\n", value);
                     break;
                  case EVENT_UP: // finished raising
                     snprintf(wbuf, 1024, "V %i finished raising\n", value);
                     break;
                  case EVENT_LOWER: // start of lower
                     snprintf(wbuf, 1024, "V %i start of lower\n", value);
                     break;
                  case EVENT_DOWN: // finished lowering
                     snprintf(wbuf, 1024, "V %i finished lowering\n", value);
                     break;
                  case EVENT_MOVE: // start of move
                     snprintf(wbuf, 1024, "V %i start of move\n", value);
                     break;
                  case EVENT_MOVING: // reached target speed
                     snprintf(wbuf, 1024, "V %i reached target speed\n", value);
                     break;
                  case EVENT_POSITION: // changed position
                     snprintf(wbuf, 1024, "V %i changed position\n", value);
                     break;
                  case EVENT_COAST: // started coast
                     snprintf(wbuf, 1024, "V %i started coast\n", value);
                     break;
                  case EVENT_STOP: // started stopping
                     snprintf(wbuf, 1024, "V %i started stopping\n", value);
                     break;
                  case EVENT_STOPPED: // finished stopping
                     snprintf(wbuf, 1024, "V %i finished stopping\n", value);
                     break;
                  case EVENT_HIT: // hit
                     snprintf(wbuf, 1024, "V %i hit\n", value);
                     break;
                  case EVENT_KILL: // kill
                     snprintf(wbuf, 1024, "V %i kill\n", value);
                     break;
                  case EVENT_SHUTDOWN: // shutdown
                     snprintf(wbuf, 1024, "V %i shutdown\n", value);
                     break;
                  case EVENT_DOCK: // dock
                     snprintf(wbuf, 1024, "V %i dock\n", value);
                     break;
                  case EVENT_UNDOCKED: // undocked
                     snprintf(wbuf, 1024, "V %i undocked\n", value);
                     break;
                  case EVENT_SLEEP: // sleep
                     snprintf(wbuf, 1024, "V %i sleep\n", value);
                     break;
                  case EVENT_WAKE: // wake
                     snprintf(wbuf, 1024, "V %i wake\n", value);
                     break;
                  case EVENT_HOME_LIMIT: // home limit
                     snprintf(wbuf, 1024, "V %i home limit\n", value);
                     break;
                  case EVENT_END_LIMIT: // end limit
                     snprintf(wbuf, 1024, "V %i end limit\n", value);
                     break;
                  case EVENT_DOCK_LIMIT: // dock limit
                     snprintf(wbuf, 1024, "V %i dock limit\n", value);
                     break;
                  case EVENT_TIMED_OUT: // timeout
                     snprintf(wbuf, 1024, "V %i timeout\n", value);
                     break;
                  case EVENT_IS_MOVING: // speed change
                     snprintf(wbuf, 1024, "V %i speed change\n", value);
                     break;
                  case EVENT_CHARGING: // charging
                     snprintf(wbuf, 1024, "V %i charging\n", value);
                     break;
                  case EVENT_NOTCHARGING: // not charging
                     snprintf(wbuf, 1024, "V %i not charging\n", value);
                     break;
                  case EVENT_ENABLE_BATTERY_CHK: // not charging
                     snprintf(wbuf, 1024, "V %i enable battery check\n", value);
                     break;
                  case EVENT_DISABLE_BATTERY_CHK: // error
                     snprintf(wbuf, 1024, "V %i disable battery check\n", value);
                     break;
                  case EVENT_ERROR: // error
                     snprintf(wbuf, 1024, "V %i error\n", value);
                     break;
                  default: // Other event
                     snprintf(wbuf, 1024, "V %i unknown\n", value);
                     break;
                }
            }
            break;

        case NL_C_FAULT:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

            if (attrs[GEN_INT8_A_MSG]) {
                // battery value percentage
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                switch (value) {
                   case ERR_normal:
                      snprintf(wbuf, 1024, "U normal\n");
                      break;
                   case ERR_both_limits_active:
                      snprintf(wbuf, 1024, "U both limits active\n");
                      break;
                   case ERR_invalid_direction_req:
                      snprintf(wbuf, 1024, "U invalid direction req\n");
                      break;
                   case ERR_invalid_speed_req:
                      snprintf(wbuf, 1024, "U invalid speed req\n");
                      break;
                   case ERR_speed_zero_req:
                      snprintf(wbuf, 1024, "U speed zero req\n");
                      break;
                   case ERR_stop_right_limit:
                      snprintf(wbuf, 1024, "U stop right limit\n");
                      break;
                   case ERR_stop_left_limit:
                      snprintf(wbuf, 1024, "U stop left limit\n");
                      break;
                   case ERR_stop_by_distance:
                      snprintf(wbuf, 1024, "U stop by distance\n");
                      break;
                   case ERR_emergency_stop:
                      snprintf(wbuf, 1024, "U emergency stop\n");
                      break;
                   case ERR_no_movement:
                      snprintf(wbuf, 1024, "U no movement\n");
                      break;
                   case ERR_over_speed:
                      snprintf(wbuf, 1024, "U over speed\n");
                      break;
                   case ERR_unassigned:
                      snprintf(wbuf, 1024, "U unassigned\n");
                      break;
                   case ERR_wrong_direction:
                      snprintf(wbuf, 1024, "U wrong direction\n");
                      break;
                   case ERR_stop:
                      snprintf(wbuf, 1024, "U stop\n");
                      break;
                   case ERR_lifter_stuck_at_limit:
                      snprintf(wbuf, 1024, "U lifter stuck at limit\n");
                      break;
                   case ERR_actuation_not_complete:
                      snprintf(wbuf, 1024, "U actuation not complete\n");
                      break;
                   case ERR_not_leave_conceal:
                      snprintf(wbuf, 1024, "U not leave conceal\n");
                      break;
                   case ERR_not_leave_expose:
                      snprintf(wbuf, 1024, "U not leave expose\n");
                      break;
                   case ERR_not_reach_expose:
                      snprintf(wbuf, 1024, "U not reach expose\n");
                      break;
                   case ERR_not_reach_conceal:
                      snprintf(wbuf, 1024, "U not reach conceal\n");
                      break;
                   case ERR_low_battery:
                      snprintf(wbuf, 1024, "U low battery\n");
                      break;
                   case ERR_engine_stop:
                      snprintf(wbuf, 1024, "U engine stop\n");
                      break;
                   case ERR_IR_failure:
                      snprintf(wbuf, 1024, "U IR failure\n");
                      break;
                   case ERR_audio_failure:
                      snprintf(wbuf, 1024, "U audio failure\n");
                      break;
                   case ERR_miles_failure:
                      snprintf(wbuf, 1024, "U miles failure\n");
                      break;
                   case ERR_thermal_failure:
                      snprintf(wbuf, 1024, "U thermal failure\n");
                      break;
                   case ERR_hit_sensor_failure:
                      snprintf(wbuf, 1024, "U hit sensor failure\n");
                      break;
                   case ERR_invalid_target_type:
                      snprintf(wbuf, 1024, "U invalid target type\n");
                      break;
                   case ERR_bad_RF_packet:
                      snprintf(wbuf, 1024, "U bad RF packet\n");
                      break;
                   case ERR_bad_checksum:
                      snprintf(wbuf, 1024, "U bad checksum\n");
                      break;
                   case ERR_unsupported_command:
                      snprintf(wbuf, 1024, "U unsupported command\n");
                      break;
                   case ERR_invalid_exception:
                      snprintf(wbuf, 1024, "U invalid exception\n");
                      break;
                   case ERR_left_dock_limit:
                      snprintf(wbuf, 1024, "U Left Dock\n");
                      break;
                   case ERR_stop_dock_limit:
                      snprintf(wbuf, 1024, "U Arrived at Dock\n");
                      break;
                   case ERR_disconnected_SIT:
                      snprintf(wbuf, 1024, "U disconnected SIT\n");
                      break;
                   case ERR_connected_SIT:
                      snprintf(wbuf, 1024, "U connected SIT\n");
                      break;
                   case ERR_critical_battery:
                      snprintf(wbuf, 1024, "U critical battery\n");
                      break;
                   case ERR_normal_battery:
                      snprintf(wbuf, 1024, "U normal battery\n");
                      break;
                   case ERR_charging_battery:
                      snprintf(wbuf, 1024, "U charging battery\n");
                      break;
                   case ERR_notcharging_battery:
                      snprintf(wbuf, 1024, "U not charging battery\n");
                      break;
                   case ERR_target_killed:
                      snprintf(wbuf, 1024, "U target killed\n");
                      break;
                   case ERR_target_asleep:
                      snprintf(wbuf, 1024, "U target asleep\n");
                      break;
                   case ERR_target_awake:
                      snprintf(wbuf, 1024, "U target awake\n");
                      break;
                   default:
                      snprintf(wbuf, 1024, "U Unknown: %i\n", value);
                      break;
                }
            }

            break;
        case NL_C_FAILURE:
            genlmsg_parse(nlh, 0, attrs, GEN_STRING_A_MAX, generic_string_policy);

            if (attrs[GEN_STRING_A_MSG]) {
                char *data = nla_get_string(attrs[GEN_STRING_A_MSG]);
                snprintf(wbuf, 1024, "failure attribute: %s\n", data);
            } else {
                snprintf(wbuf, 1024, "failed to get attribute\n");
            }

            break;
        case NL_C_CMD_EVENT:
            genlmsg_parse(nlh, 0, attrs, CMD_EVENT_A_MAX, cmd_event_policy);

            if (attrs[CMD_EVENT_A_MSG]) {
                struct cmd_event *et;
                memset(et, 0, sizeof(struct cmd_event));
                et = (struct cmd_event*)nla_data(attrs[CMD_EVENT_A_MSG]);
                snprintf(wbuf, 1024, "Command Event to %i: cmd|%i size|%i attr|%i data|%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n", et->role, et->cmd, et->payload_size, et->attribute, et->payload[0], et->payload[1], et->payload[2], et->payload[3], et->payload[4], et->payload[5], et->payload[6], et->payload[7], et->payload[8], et->payload[9], et->payload[10], et->payload[11], et->payload[12], et->payload[13], et->payload[14], et->payload[15]);
            } else {
                snprintf(wbuf, 1024, "failed to get attribute\n");
            }

            break;
        case NL_C_DMSG:
        case NL_C_SCENARIO:
            // ignore these
            snprintf(wbuf, 1024, "ignored unknown command %i\n", ghdr->cmd);
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

char* runCmd(char *cmd, int read, char *buffer, int buffer_max) {
   FILE *fp;
   int status;

   // open pipe
   fp = popen(cmd, read?"r":"w");
   if (fp == NULL) {
      return "Error: fp is NULL";
   }

   // read/write
   if (read) {
      if (fread(buffer, buffer_max, sizeof(char), fp) == -1) {
         return "Error: no data read";
      }
   } else {
      if (fwrite(buffer, buffer_max, sizeof(char), fp) == -1) {
         return "Error: no data written";
      }
   }

   // close pipe
   status = pclose(fp);
   if (status == -1) {
      return "Error: could not close";
   }
   return buffer;
}

// Reads the eeprom at the specified address and for the specified size
char* readEeprom(int address, int size) {
   static char buffer[256];
   char cmd[256];
   buffer[size+1] = '\0'; // null terminate read buffer

   // create command
   snprintf(cmd, 256, "/usr/bin/eeprom_rw read -addr 0x%04X -size 0x%02X", address, size);
   
   // run command and send data, return result
   return runCmd(cmd, 1, buffer, size); // 1 == read
}

// Writes the data to the eeprom gui at the specified address
char* writeEeprom(int address, int size, int blank, char *data) {
   char cmd[256];

   // create command
   snprintf(cmd, 256, "/usr/bin/eeprom_rw write -addr 0x%04X -size %i -blank 0x%02X", address, size, blank);
   
   // run command and send data, return result
   return runCmd(cmd, 0, data, size); // 0 == write
}

// Checks to see if a string contains only decimal numbers
int isNumber(char *data) {
   int i = 0;
   // return 0 is the data is blank
   if (strlen(data) == 0) return 0;
   for(i = 0; i < strlen(data); i++) {
      int character = data[i];
      if (!isdigit(character)) {
         return 0;
      }
   }
      return 1;
}

int telnet_client(struct nl_handle *handle, char *client_buf, int client) {
    char wbuf[1024];
    wbuf[0] = '\0';
    int arg1, arg2, arg3, arg4;
    // default parameters
    int param1, param2, param3, param4, fparam1, fparam2, sparam1, sparam2;
    int nparam1, nparam2, nparam3, nparam4, nparam5, nparam6, nparam7, nparam8;
    int nparam9, nparam10, nparam11, nparam12, nparam13, nexists;
    int gparam1, gparam2, gparam3, gparam4, gparam5, gparam6, gparam7, gparam8;
    int gparam9, gparam10, gparam11, gparam12, gexists;
    int pparam1, pparam2, pparam3, pparam4, pparam5, pparam6, pparam7, pparam8;
    int pparam9, pparam10, pparam11, pparam12, pexists;
    int kparam1, kparam2, kparam3, kparam4, kparam5, kparam6, kparam7, kparam8;
    int kparam9, kparam10, kparam11, kparam12, kexists;
    int tparam1, tparam2, tparam3, tparam4, tparam5, tparam6, tparam7, tparam8;
    int tparam9, tparam10, tparam11, tparam12, texists;
    int yparam1, yparam2, yparam3, yparam4, yparam5, yparam6, yparam7, yparam8;
    int yparam9, yparam10, yparam11, yparam12, yexists;
    int eparam1, eparam2, jparam1, jparam2;
    char eparam3[5];
	int farg1;
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
            case 'K': case 'k':
                nl_cmd = NL_C_BATTERY;
                break;
            case 'L': case 'l':
                nl_cmd = NL_C_HIT_CAL;
                break;
            case 'M': case 'm':
                nl_cmd = NL_C_MOVE;
                break;
            case 'O': case 'o':
                nl_cmd = NL_C_BIT;
                break;
            case 'P': case 'p':
                nl_cmd = NL_C_SLEEP;
                break;
            case 'Q': case 'q':
                nl_cmd = NL_C_ACCESSORY;
                break;
            case 'R': case 'r':
                nl_cmd = NL_C_GOHOME;
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
            case 'X': case 'x':
                nl_cmd = NL_C_STOP;
                break;
            case 'Y': case 'y':
                nl_cmd = NL_C_HIT_CAL;
                break;
            case 'Z': case 'z':
                nl_cmd = NL_C_BIT;
                break;
            case 'I': case 'i':
               arg1 = cmd[1] == ' ' ? 2 : 1; // find index of second argument
               arg2 = cmd[arg1+1] == ' ' ? arg1+2 : arg1+1; // find index of third argument
               arg3 = strlen(cmd+arg2)+1; // size value, add 1 for null terminator 
               int nest_arg3 = strlen(cmd+5)+1;  // size value if we are dealing with "I H1 <value>" etc.
               int third = 1;
               switch (cmd[arg1]) { /* second letter */
                  case 'A': case 'a':	  // Reads and sets an address location
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "I A %s\n", writeEeprom(ADDRESS_LOC, arg3, ADDRESS_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I A %s\n", readEeprom(ADDRESS_LOC, ADDRESS_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'B': case 'b':     // Reads the board type
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "I B %s\n", writeEeprom(BOARD_LOC, arg3, BOARD_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I B %s\n", readEeprom(BOARD_LOC, BOARD_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'C': case 'c':     // Sets and Reads the connect port number
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "I C %s\n", writeEeprom(CONNECT_PORT_LOC, arg3, CONNECT_PORT_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I C %s\n", readEeprom(CONNECT_PORT_LOC, CONNECT_PORT_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'D': case 'd':      // Reads communication type
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "I D %s\n", writeEeprom(COMMUNICATION_LOC, arg3, COMMUNICATION_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I D %s\n", readEeprom(COMMUNICATION_LOC, COMMUNICATION_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'E': case 'e':	   // Sets and reads battery defaults
                     if (sscanf(cmd+arg2, "%i %i", &eparam1, &eparam2) == 2) {        // write
                        char ebuf1[5], ebuf2[5];
                        // target type or mover type or reverse
                        snprintf(ebuf1, 5, "%i", eparam1);
                        // default
                        snprintf(ebuf2, 5, "%i", eparam2);
                        switch (eparam1) {
                           case 1:	// SIT
                              snprintf(wbuf, 1024, "I E SIT %s\n", writeEeprom(SIT_BATTERY_LOC, strnlen(ebuf2, 5), SIT_BATTERY_SIZE, ebuf2)); 
                              break;
                           case 2:	// SAT
                              snprintf(wbuf, 1024, "I E SAT %s\n", writeEeprom(SAT_BATTERY_LOC, strnlen(ebuf2, 5), SAT_BATTERY_SIZE, ebuf2)); 
                              break;
                           case 3:	// SES
                              snprintf(wbuf, 1024, "I E SES %s\n", writeEeprom(SES_BATTERY_LOC, strnlen(ebuf2, 5), SES_BATTERY_SIZE, ebuf2)); 
                              break;
                           case 4:	// MIT
                              snprintf(wbuf, 1024, "I E MIT %s\n", writeEeprom(MIT_BATTERY_LOC, strnlen(ebuf2, 5), MIT_BATTERY_SIZE, ebuf2)); 
                              break;
                           case 5:	// MAT
                              snprintf(wbuf, 1024, "I E MAT %s\n", writeEeprom(MAT_BATTERY_LOC, strnlen(ebuf2, 5), MAT_BATTERY_SIZE, ebuf2)); 
                              break;
                           case 6:	// Reverse
                              snprintf(wbuf, 1024, "I E REVERSE %s\n", writeEeprom(REVERSE_LOC, strnlen(ebuf2, 5), REVERSE_SIZE, ebuf2));
                              break;
                           default:
                              snprintf(wbuf, 1024, "Please choose a correct parameter\n(1-SIT|2-SAT|3-SES|4-MIT|5-MAT|6-Reverse\n");
                              break;
                        }
                     } else {	// read
                        sscanf(cmd+arg2, "%s", &eparam3);
                        char bat1[BATTERY_SIZE+1], bat2[BATTERY_SIZE+1], bat3[BATTERY_SIZE+1], bat4[BATTERY_SIZE+1], bat5[BATTERY_SIZE+1], bat6[MOVER_SIZE+1], bat7[MOVER_SIZE+1], bat8[MOVER_SIZE+1], bat9[MOVER_SIZE+1];
                        
                        if (memcmp(eparam3, "sit", 3)==0 || memcmp(eparam3, "SIT", 3)==0) {			// read SIT battery default
                           memset(bat1, 0, BATTERY_SIZE+1);
                           memcpy(bat1,readEeprom(SIT_BATTERY_LOC, SIT_BATTERY_SIZE), BATTERY_SIZE);
                           if (!isNumber(bat1)) {  //revert to default value
                              snprintf(bat1, BATTERY_SIZE, "%i", SIT_BATTERY);
                           }
                           snprintf(wbuf, 1024, "I E SIT %s\n", bat1);
                        } else if (memcmp(eparam3, "sat", 3)==0 || memcmp(eparam3, "SAT", 3)==0) {	// read SAT battery default
                           memset(bat2, 0, BATTERY_SIZE+1);
                           memcpy(bat2,readEeprom(SAT_BATTERY_LOC, SAT_BATTERY_SIZE), BATTERY_SIZE);
                           if (!isNumber(bat2)) {  //revert to default value
                              snprintf(bat2, BATTERY_SIZE, "%i", SAT_BATTERY);
                           }
                           snprintf(wbuf, 1024, "I E SAT %s\n", bat2);
                        } else if (memcmp(eparam3, "ses", 3)==0 || memcmp(eparam3, "SES", 3)==0) {	// read SES battery default
                           memset(bat3, 0, BATTERY_SIZE+1);
                           memcpy(bat3,readEeprom(SES_BATTERY_LOC, SES_BATTERY_SIZE), BATTERY_SIZE);
                           if (!isNumber(bat3)) {  //revert to default value
                              snprintf(bat3, BATTERY_SIZE, "%i", SES_BATTERY);
                           }
                           snprintf(wbuf, 1024, "I E SES %s\n", bat3);
                        } else if (memcmp(eparam3, "mit", 3)==0 || memcmp(eparam3, "MIT", 3)==0) {	// read MIT battery default
                           memset(bat4, 0, BATTERY_SIZE+1);
                           memcpy(bat4,readEeprom(MIT_BATTERY_LOC, MIT_BATTERY_SIZE), BATTERY_SIZE);
                           if (!isNumber(bat4)) {  //revert to default value
                              snprintf(bat4, BATTERY_SIZE, "%i", MIT_BATTERY);
                           }
                           snprintf(wbuf, 1024, "I E MIT %s\n", bat4);
                        } else if (memcmp(eparam3, "mat", 3)==0 || memcmp(eparam3, "MAT", 3)==0) {	// read MAT battery default
                           memset(bat5, 0, BATTERY_SIZE+1);
                           memcpy(bat5,readEeprom(MAT_BATTERY_LOC, MAT_BATTERY_SIZE), BATTERY_SIZE);
                           if (!isNumber(bat5)) {  //revert to default value
                              snprintf(bat5, BATTERY_SIZE, "%i", MAT_BATTERY);
                           }
                           snprintf(wbuf, 1024, "I E MAT %s\n", bat5);
                        } else if (memcmp(eparam3, "reverse", 7)==0 || memcmp(eparam3, "REVERSE", 7)==0) {	// read reverse
                           memset(bat6, 0, MOVER_SIZE+1);
                           memcpy(bat6,readEeprom(REVERSE_LOC, REVERSE_SIZE), MOVER_SIZE);
                           if (!isNumber(bat6)) {  //revert to default value
                              snprintf(bat6, BATTERY_SIZE, "%i", REVERSE);
                           }
                           snprintf(wbuf, 1024, "I E REVERSE %s\n", bat6);
                        } else {
                           snprintf(wbuf, 1024, "I E Incorrect Format\n");
                        }
                     }
                     break;
                  case 'F': case 'f':      // Sets and reads fall parameters
                     if (sscanf(cmd+arg2, "%i %i", &fparam1, &fparam2) == 2) {  // write
                        //write
                        char fbuf1[5], fbuf2[5];
                        // kill at X hits
                        snprintf(fbuf1, 5, "%i", fparam1);
                        // at fall do what?
                        snprintf(fbuf2, 5, "%i", fparam2);
                        // write out the hit calibration parameters
                        snprintf(wbuf, 1024, "I F %s %s\n", writeEeprom(FALL_KILL_AT_X_HITS_LOC, strnlen(fbuf1, 5), FALL_KILL_AT_X_HITS_SIZE, fbuf1), writeEeprom(FALL_AT_FALL_LOC, strnlen(fbuf2, 5), FALL_AT_FALL_SIZE, fbuf2));
                     } else {   // read
                        char hkr[33], afr[33];
                        memset(hkr, 0, 33);
                        memcpy(hkr, readEeprom(FALL_KILL_AT_X_HITS_LOC, FALL_KILL_AT_X_HITS_SIZE), 32);
                        if (!isNumber(hkr)) {  //revert to default value
                           snprintf(hkr, 5, "%i", FALL_KILL_AT_X_HITS);
                        }
                        memset(afr, 0, 33);
                        memcpy(afr, readEeprom(FALL_AT_FALL_LOC, FALL_AT_FALL_SIZE), 32);
                        if (!isNumber(afr)) {  //revert to default value
                           snprintf(afr, 5, "%i", FALL_AT_FALL);
                        }
                        snprintf(wbuf, 1024, "I F %s %s\n", hkr, afr);
                     }
                     break;
                  case 'G': case 'g':	   // Moon Glow defaults
                        if (sscanf(cmd+arg2, "%i %i %i %i %i %i %i %i %i %i %i %i %i", &gexists, &gparam1, &gparam2, &gparam3, &gparam4, &gparam5, &gparam6, &gparam7, &gparam8, &gparam9, &gparam10, &gparam11, &gparam12) == 13) {        // write
                        char mglExist[5], gbuf1[5], gbuf2[5], gbuf3[5], gbuf4[5];  
                        char gbuf5[5], gbuf6[5], gbuf7[5], gbuf8[5];    
                        char gbuf9[5], gbuf10[5], gbuf11[5], gbuf12[5];
						//exists
						snprintf(mglExist, 5, "%i", gexists);                      
                        // activate
                        snprintf(gbuf1, 5, "%i", gparam1);
                        // activate on what kind of exposure
                        snprintf(gbuf2, 5, "%i", gparam2);
                        // activate on hit
                        snprintf(gbuf3, 5, "%i", gparam3);
                        // activate on kill
                        snprintf(gbuf4, 5, "%i", gparam4);
                        // milliseconds on time
                        snprintf(gbuf5, 5, "%i", gparam5);
                        // milliseconds off time
                        snprintf(gbuf6, 5, "%i", gparam6);
                        // start delay
                        snprintf(gbuf7, 5, "%i", gparam7);
                        // repeat delay
                        snprintf(gbuf8, 5, "%i", gparam8);
                        // repeat count
                        snprintf(gbuf9, 5, "%i", gparam9);
                        // ex1
                        snprintf(gbuf10, 5, "%i", gparam10);
                        // ex2
                        snprintf(gbuf11, 5, "%i", gparam11);
                        // ex3
                        snprintf(gbuf12, 5, "%i", gparam12);
						// write all the mfs defaults
                        snprintf(wbuf, 1024, "I G %s %s %s %s %s %s %s %s %s %s %s %s %s\n",
    writeEeprom(MGL_EXISTS_LOC, strnlen(mglExist, 5), MGL_EXISTS_SIZE, mglExist), 
	writeEeprom(MGL_ACTIVATE_LOC, strnlen(gbuf1, 5), MGL_ACTIVATE_SIZE, gbuf1),
    writeEeprom(MGL_ACTIVATE_EXPOSE_LOC, strnlen(gbuf2, 5), MGL_ACTIVATE_EXPOSE_SIZE, gbuf2), 
    writeEeprom(MGL_ACTIVATE_ON_HIT_LOC, strnlen(gbuf3, 5), MGL_ACTIVATE_ON_HIT_SIZE, gbuf3), 
    writeEeprom(MGL_ACTIVATE_ON_KILL_LOC, strnlen(gbuf4, 5), MGL_ACTIVATE_ON_KILL_SIZE, gbuf4),
    writeEeprom(MGL_MS_ON_TIME_LOC, strnlen(gbuf5, 5), MGL_MS_ON_TIME_SIZE, gbuf5),
    writeEeprom(MGL_MS_OFF_TIME_LOC, strnlen(gbuf6, 5), MGL_MS_OFF_TIME_SIZE, gbuf6),
    writeEeprom(MGL_START_DELAY_LOC, strnlen(gbuf7, 5), MGL_START_DELAY_SIZE, gbuf7),
    writeEeprom(MGL_REPEAT_DELAY_LOC, strnlen(gbuf8, 5), MGL_REPEAT_DELAY_SIZE, gbuf8),
    writeEeprom(MGL_REPEAT_COUNT_LOC, strnlen(gbuf9, 5), MGL_REPEAT_COUNT_SIZE, gbuf9),
    writeEeprom(MGL_EX1_LOC, strnlen(gbuf10, 5), MGL_EX1_SIZE, gbuf10),
    writeEeprom(MGL_EX2_LOC, strnlen(gbuf11, 5), MGL_EX2_SIZE, gbuf11),
    writeEeprom(MGL_EX3_LOC, strnlen(gbuf12, 5), MGL_EX3_SIZE, gbuf12));
                     } else {	// read
						char mglExists[MGL_SIZE+1], mgl1[MGL_SIZE+1], mgl2[MGL_SIZE+1], mgl3[MGL_SIZE+1], mgl4[MGL_SIZE+1], mgl5[MGL_SIZE+1], mgl6[MGL_SIZE+1], mgl7[MGL_SIZE+1], mgl8[MGL_SIZE+1], mgl9[MGL_SIZE+1], mgl10[MGL_SIZE+1], mgl11[MGL_SIZE+1], mgl12[MGL_SIZE+1];
                        memset(mglExists, 0, MGL_SIZE+1);
						memcpy(mglExists,readEeprom(MGL_EXISTS_LOC, MGL_EXISTS_SIZE), MGL_SIZE);
                        if (!isNumber(mglExists)) {  //revert to default value
                           snprintf(mglExists, MGL_SIZE, "%i", MGL_EXISTS);
                        }
						memset(mgl1, 0, MGL_SIZE+1);
						memcpy(mgl1,readEeprom(MGL_ACTIVATE_LOC, MGL_ACTIVATE_SIZE), MGL_SIZE);
                        if (!isNumber(mgl1)) {  //revert to default value
                           snprintf(mgl1, MGL_SIZE, "%i", MGL_ACTIVATE);
                        }
                        memset(mgl2, 0, MGL_SIZE+1);
                        memcpy(mgl2,readEeprom(MGL_ACTIVATE_EXPOSE_LOC, MGL_ACTIVATE_EXPOSE_SIZE), MGL_SIZE);
                        if (!isNumber(mgl2)) {  //revert to default value
                           snprintf(mgl2, MGL_SIZE, "%i", MGL_ACTIVATE_EXPOSE);
                        }
                        memset(mgl3, 0, MGL_SIZE+1);
                        memcpy(mgl3,readEeprom(MGL_ACTIVATE_ON_HIT_LOC, MGL_ACTIVATE_ON_HIT_SIZE), MGL_SIZE);
                        if (!isNumber(mgl3)) {  //revert to default value
                           snprintf(mgl3, MGL_SIZE, "%i", MGL_ACTIVATE_ON_HIT);
                        }
                        memset(mgl4, 0, MGL_SIZE+1);
                        memcpy(mgl4,readEeprom(MGL_ACTIVATE_ON_KILL_LOC, MGL_ACTIVATE_ON_KILL_SIZE), MGL_SIZE);
                        if (!isNumber(mgl4)) {  //revert to default value
                           snprintf(mgl4, MGL_SIZE, "%i", MGL_ACTIVATE_ON_KILL);
                        }
                        memset(mgl5, 0, MGL_SIZE+1);
                        memcpy(mgl5,readEeprom(MGL_MS_ON_TIME_LOC, MGL_MS_ON_TIME_SIZE), MGL_SIZE);
                        if (!isNumber(mgl5)) {  //revert to default value
                           snprintf(mgl5, MGL_SIZE, "%i", MGL_MS_ON_TIME);
                        }
                        memset(mgl6, 0, MGL_SIZE+1);
                        memcpy(mgl6,readEeprom(MGL_MS_OFF_TIME_LOC, MGL_MS_OFF_TIME_SIZE), MGL_SIZE);
                        if (!isNumber(mgl6)) {  //revert to default value
                           snprintf(mgl6, MGL_SIZE, "%i", MGL_MS_OFF_TIME);
                        }
                        memset(mgl7, 0, MGL_SIZE+1);
                        memcpy(mgl7,readEeprom(MGL_START_DELAY_LOC, MGL_START_DELAY_SIZE), MGL_SIZE);
                        if (!isNumber(mgl7)) {  //revert to default value
                           snprintf(mgl7, MGL_SIZE, "%i", MGL_START_DELAY);
                        }
                        memset(mgl8, 0, MGL_SIZE+1);
                        memcpy(mgl8,readEeprom(MGL_REPEAT_DELAY_LOC, MGL_REPEAT_DELAY_SIZE), MGL_SIZE);
                        if (!isNumber(mgl8)) {  //revert to default value
                           snprintf(mgl8, MGL_SIZE, "%i", MGL_REPEAT_DELAY);
                        }
                        memset(mgl9, 0, MGL_SIZE+1);
                        memcpy(mgl9,readEeprom(MGL_REPEAT_COUNT_LOC, MGL_REPEAT_COUNT_SIZE), MGL_SIZE);
                        if (!isNumber(mgl9)) {  //revert to default value
                           snprintf(mgl9, MGL_SIZE, "%i", MGL_REPEAT_COUNT);
                        }
                        memset(mgl10, 0, MGL_SIZE+1);
                        memcpy(mgl10,readEeprom(MGL_EX1_LOC, MGL_EX1_SIZE), MGL_SIZE);
                        if (!isNumber(mgl10)) {  //revert to default value
                           snprintf(mgl10, MGL_SIZE, "%i", MGL_EX1);
                        }
                        memset(mgl11, 0, MGL_SIZE+1);
                        memcpy(mgl11,readEeprom(MGL_EX2_LOC, MGL_EX2_SIZE), MGL_SIZE);
                        if (!isNumber(mgl11)) {  //revert to default value
                           snprintf(mgl11, MGL_SIZE, "%i", MGL_EX2);
                        }
                        memset(mgl12, 0, MGL_SIZE+1);
                        memcpy(mgl12,readEeprom(MGL_EX3_LOC, MGL_EX3_SIZE), MGL_SIZE);
                        if (!isNumber(mgl12)) {  //revert to default value
                           snprintf(mgl12, MGL_SIZE, "%i", MGL_EX3);
                        }
                        snprintf(wbuf, 1024, "I G %s %s %s %s %s %s %s %s %s %s %s %s %s\n", mglExists, mgl1, mgl2, mgl3, mgl4, mgl5, mgl6, mgl7, mgl8, mgl9, mgl10, mgl11, mgl12);
                        
                     }
                     break;
                  case 'H': case 'h':      // Sets and Reads Hit Calibration
                     if (sscanf(cmd+arg2, "%i %i %i %i", &param1, &param2, &param3, &param4) == 4) {        // write
                        char hbuf1[5], hbuf2[5], hbuf3[5], hbuf4[5];                    
                        // milliseconds between hits
                        snprintf(hbuf1, 5, "%i", param1);
                        // hit desensitivity
                        snprintf(hbuf2, 5, "%i", param2);
                        // milliseconds blanking time from start expose
                        snprintf(hbuf3, 5, "%i", param3);
                        // enable on value
                        snprintf(hbuf4, 5, "%i", param4);
						// write all the hit calabration parameters
                        snprintf(wbuf, 1024, "I H %s %s %s %s\n", writeEeprom(HIT_MSECS_BETWEEN_LOC, strnlen(hbuf1, 5), HIT_MSECS_BETWEEN_SIZE, hbuf1), writeEeprom(HIT_DESENSITIVITY_LOC, strnlen(hbuf2, 5), HIT_DESENSITIVITY_SIZE, hbuf2), writeEeprom(HIT_START_BLANKING_LOC, strnlen(hbuf3, 5), HIT_START_BLANKING_SIZE, hbuf3), writeEeprom(HIT_ENABLE_ON_LOC, strnlen(hbuf4, 5), HIT_ENABLE_ON_SIZE, hbuf4));
                     } else {	// read
						char msr[HIT_MSECS_BETWEEN_SIZE+1], hdr[HIT_DESENSITIVITY_SIZE+1], btr[HIT_START_BLANKING_SIZE+1], evr[HIT_ENABLE_ON_SIZE+1], hbuf1[9];
						memset(msr, 0, HIT_MSECS_BETWEEN_SIZE+1);
						memcpy(msr,readEeprom(HIT_MSECS_BETWEEN_LOC, HIT_MSECS_BETWEEN_SIZE), HIT_MSECS_BETWEEN_SIZE);
                        if (!isNumber(msr)) {  //revert to default value
                           snprintf(msr, HIT_MSECS_BETWEEN_SIZE, "%i", HIT_MSECS_BETWEEN);
                        }
                        memset(hdr, 0, HIT_DESENSITIVITY_SIZE+1);
                        memcpy(hdr,readEeprom(HIT_DESENSITIVITY_LOC, HIT_DESENSITIVITY_SIZE), HIT_DESENSITIVITY_SIZE);
                        if (!isNumber(hdr)) {  //revert to default value
                           snprintf(hdr, HIT_DESENSITIVITY_SIZE, "%i", HIT_DESENSITIVITY);
                        }
                        memset(btr, 0, HIT_START_BLANKING_SIZE+1);
                        memcpy(btr,readEeprom(HIT_START_BLANKING_LOC, HIT_START_BLANKING_SIZE), HIT_START_BLANKING_SIZE);
                        if (!isNumber(btr)) {  //revert to default value
                           snprintf(btr, HIT_START_BLANKING_SIZE, "%i", HIT_START_BLANKING);
                        }
                        memset(evr, 0, HIT_ENABLE_ON_SIZE+1);
                        memcpy(evr,readEeprom(HIT_ENABLE_ON_LOC, HIT_ENABLE_ON_SIZE), HIT_ENABLE_ON_SIZE);
                        if (!isNumber(evr)) {  //revert to default value
                           snprintf(evr, HIT_ENABLE_ON_SIZE, "%i", HIT_ENABLE_ON);
                        }
                        snprintf(wbuf, 1024, "I H %s %s %s %s\n", msr, hdr, btr, evr);
                        
                     }
                     break;
                  case 'I': case 'i':      // Sets and Reads the IP address
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "I I %s\n", writeEeprom(IP_ADDRESS_LOC, arg3, IP_ADDRESS_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I I %s\n", readEeprom(IP_ADDRESS_LOC, IP_ADDRESS_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'J': case 'j':      // Sets and reads SES defaults
                     if (sscanf(cmd+arg2, "%i %i", &jparam1, &jparam2) == 2) {  
                        //write
                        char jbuf1[5], jbuf2[5];
                        // no loop or infinite loop
                        snprintf(jbuf1, 5, "%i", jparam1);
                        // mode
                        snprintf(jbuf2, 5, "%i", jparam2);
                        // write out the hit calibration parameters
                        if (memcmp(jbuf1, "0", 1)==1) {	// no loop
                           snprintf(wbuf, 1024, "I J %s %s\n", writeEeprom(SES_LOOP_LOC, strnlen(jbuf1, 5), SES_LOOP_SIZE, "1"), writeEeprom(SES_MODE_LOC, strnlen(jbuf2, 5), SES_MODE_SIZE, jbuf2));
                        } else {	// infinite loop
                           snprintf(wbuf, 1024, "I J %s %s\n", writeEeprom(SES_LOOP_LOC, 11, SES_LOOP_SIZE, "0xFFFFFFFF"), writeEeprom(SES_MODE_LOC, strnlen(jbuf2, 5), SES_MODE_SIZE, jbuf2));
                        }
                     } else {   // read
                        char loop[SES_SIZE+1], mode[SES_SIZE+1];
                        memset(loop, 0, SES_SIZE+1);
                        memcpy(loop, readEeprom(SES_LOOP_LOC, SES_LOOP_SIZE), SES_SIZE);
                        if (memcmp(loop, "", 1)==0) {  //revert to default value
                           snprintf(loop, SES_SIZE, "%i", SES_LOOP);
                        }
                        memset(mode, 0, SES_SIZE+1);
                        memcpy(mode, readEeprom(SES_MODE_LOC, SES_MODE_SIZE), SES_SIZE);
                        if (!isNumber(mode)) {  //revert to default value
                           snprintf(mode, SES_SIZE, "%i", SES_MODE);
                        }
                        snprintf(wbuf, 1024, "I J %s %s\n", loop, mode);
                     }
                     break;
                  case 'K': case 'k':	   // SMK (smoke) defaults
                     if (sscanf(cmd+arg2, "%i %i %i %i %i %i %i %i %i %i %i %i %i", &kexists, &kparam1, &kparam2, &kparam3, &kparam4, &kparam5, &kparam6, &kparam7, &kparam8, &kparam9, &kparam10, &kparam11, &kparam12) == 13) {        // write
                        char smkExist[5], kbuf1[5], kbuf2[5], kbuf3[5], kbuf4[5];  
                        char kbuf5[5], kbuf6[5], kbuf7[5], kbuf8[5];    
                        char kbuf9[5], kbuf10[5], kbuf11[5], kbuf12[5];  
                        // exists
                        snprintf(smkExist, 5, "%i", kexists);                    
                        // activate
                        snprintf(kbuf1, 5, "%i", kparam1);
                        // activate on what kind of exposure
                        snprintf(kbuf2, 5, "%i", kparam2);
                        // activate on hit
                        snprintf(kbuf3, 5, "%i", kparam3);
                        // activate on kill
                        snprintf(kbuf4, 5, "%i", kparam4);
                        // milliseconds on time
                        snprintf(kbuf5, 5, "%i", kparam5);
                        // milliseconds off time
                        snprintf(kbuf6, 5, "%i", kparam6);
                        // start delay
                        snprintf(kbuf7, 5, "%i", kparam7);
                        // repeat delay
                        snprintf(kbuf8, 5, "%i", kparam8);
                        // repeat count
                        snprintf(kbuf9, 5, "%i", kparam9);
                        // ex1
                        snprintf(kbuf10, 5, "%i", kparam10);
                        // ex2
                        snprintf(kbuf11, 5, "%i", kparam11);
                        // ex3
                        snprintf(kbuf12, 5, "%i", kparam12);
						// write all the mfs defaults
                        snprintf(wbuf, 1024, "I K %s %s %s %s %s %s %s %s %s %s %s %s %s\n", 
    writeEeprom(SMK_EXISTS_LOC, strnlen(smkExist, 5), SMK_EXISTS_SIZE, smkExist),
	writeEeprom(SMK_ACTIVATE_LOC, strnlen(kbuf1), SMK_ACTIVATE_SIZE, kbuf1),
    writeEeprom(SMK_ACTIVATE_EXPOSE_LOC, strnlen(kbuf2, 5), SMK_ACTIVATE_EXPOSE_SIZE, kbuf2), 
    writeEeprom(SMK_ACTIVATE_ON_HIT_LOC, strnlen(kbuf3, 5), SMK_ACTIVATE_ON_HIT_SIZE, kbuf3), 
    writeEeprom(SMK_ACTIVATE_ON_KILL_LOC, strnlen(kbuf4, 5), SMK_ACTIVATE_ON_KILL_SIZE, kbuf4),
    writeEeprom(SMK_MS_ON_TIME_LOC, strnlen(kbuf5, 5), SMK_MS_ON_TIME_SIZE, kbuf5),
    writeEeprom(SMK_MS_OFF_TIME_LOC, strnlen(kbuf6, 5), SMK_MS_OFF_TIME_SIZE, kbuf6),
    writeEeprom(SMK_START_DELAY_LOC, strnlen(kbuf7, 5), SMK_START_DELAY_SIZE, kbuf7),
    writeEeprom(SMK_REPEAT_DELAY_LOC, strnlen(kbuf8, 5), SMK_REPEAT_DELAY_SIZE, kbuf8),
    writeEeprom(SMK_REPEAT_COUNT_LOC, strnlen(kbuf9, 5), SMK_REPEAT_COUNT_SIZE, kbuf9),
    writeEeprom(SMK_EX1_LOC, strnlen(kbuf10, 5), SMK_EX1_SIZE, kbuf10),
    writeEeprom(SMK_EX2_LOC, strnlen(kbuf11, 5), SMK_EX2_SIZE, kbuf11),
    writeEeprom(SMK_EX3_LOC, strnlen(kbuf12, 5), SMK_EX3_SIZE, kbuf12));
                     } else {	// read
						char smkExists[SMK_SIZE+1], smk1[SMK_SIZE+1], smk2[SMK_SIZE+1], smk3[SMK_SIZE+1], smk4[SMK_SIZE+1], smk5[SMK_SIZE+1], smk6[SMK_SIZE+1], smk7[SMK_SIZE+1], smk8[SMK_SIZE+1], smk9[SMK_SIZE+1], smk10[SMK_SIZE+1], smk11[SMK_SIZE+1], smk12[MFS_SIZE+1];
                        memset(smkExists, 0, SMK_SIZE+1);
						memcpy(smkExists,readEeprom(SMK_EXISTS_LOC, SMK_EXISTS_SIZE), SMK_SIZE);
                        if (!isNumber(smkExists)) {  //revert to default value
                           snprintf(smkExists, SMK_SIZE, "%i", SMK_EXISTS);
                        }
						memset(smk1, 0, SMK_SIZE+1);
						memcpy(smk1,readEeprom(SMK_ACTIVATE_LOC, SMK_ACTIVATE_SIZE), SMK_SIZE);
                        if (!isNumber(smk1)) {  //revert to default value
                           snprintf(smk1, SMK_SIZE, "%i", SMK_ACTIVATE);
                        }
                        memset(smk2, 0, SMK_SIZE+1);
                        memcpy(smk2,readEeprom(SMK_ACTIVATE_EXPOSE_LOC, SMK_ACTIVATE_EXPOSE_SIZE), SMK_SIZE);
                        if (!isNumber(smk2)) {  //revert to default value
                           snprintf(smk2, SMK_SIZE, "%i", SMK_ACTIVATE_EXPOSE);
                        }
                        memset(smk3, 0, MFS_SIZE+1);
                        memcpy(smk3,readEeprom(SMK_ACTIVATE_ON_HIT_LOC, SMK_ACTIVATE_ON_HIT_SIZE), SMK_SIZE);
                        if (!isNumber(smk3)) {  //revert to default value
                           snprintf(smk3, SMK_SIZE, "%i", SMK_ACTIVATE_ON_HIT);
                        }
                        memset(smk4, 0, SMK_SIZE+1);
                        memcpy(smk4,readEeprom(SMK_ACTIVATE_ON_KILL_LOC, SMK_ACTIVATE_ON_KILL_SIZE), SMK_SIZE);
                        if (!isNumber(smk4)) {  //revert to default value
                           snprintf(smk4, SMK_SIZE, "%i", SMK_ACTIVATE_ON_KILL);
                        }
                        memset(smk5, 0, SMK_SIZE+1);
                        memcpy(smk5,readEeprom(SMK_MS_ON_TIME_LOC, SMK_MS_ON_TIME_SIZE), SMK_SIZE);
                        if (!isNumber(smk5)) {  //revert to default value
                           snprintf(smk5, SMK_SIZE, "%i", SMK_MS_ON_TIME);
                        }
                        memset(smk6, 0, SMK_SIZE+1);
                        memcpy(smk6,readEeprom(SMK_MS_OFF_TIME_LOC, SMK_MS_OFF_TIME_SIZE), SMK_SIZE);
                        if (!isNumber(smk6)) {  //revert to default value
                           snprintf(smk6, SMK_SIZE, "%i", MFS_MS_OFF_TIME);
                        }
                        memset(smk7, 0, SMK_SIZE+1);
                        memcpy(smk7,readEeprom(SMK_START_DELAY_LOC, SMK_START_DELAY_SIZE), SMK_SIZE);
                        if (!isNumber(smk7)) {  //revert to default value
                           snprintf(smk7, SMK_SIZE, "%i", SMK_START_DELAY);
                        }
                        memset(smk8, 0, SMK_SIZE+1);
                        memcpy(smk8,readEeprom(SMK_REPEAT_DELAY_LOC, SMK_REPEAT_DELAY_SIZE), SMK_SIZE);
                        if (!isNumber(smk8)) {  //revert to default value
                           snprintf(smk8, SMK_SIZE, "%i", SMK_REPEAT_DELAY);
                        }
                        memset(smk9, 0, SMK_SIZE+1);
                        memcpy(smk9,readEeprom(SMK_REPEAT_COUNT_LOC, SMK_REPEAT_COUNT_SIZE), SMK_SIZE);
                        if (!isNumber(smk9)) {  //revert to default value
                           snprintf(smk9, SMK_SIZE, "%i", SMK_REPEAT_COUNT);
                        }
                        memset(smk10, 0, SMK_SIZE+1);
                        memcpy(smk10,readEeprom(SMK_EX1_LOC, SMK_EX1_SIZE), SMK_SIZE);
                        if (!isNumber(smk10)) {  //revert to default value
                           snprintf(smk10, SMK_SIZE, "%i", SMK_EX1);
                        }
                        memset(smk11, 0, SMK_SIZE+1);
                        memcpy(smk11,readEeprom(SMK_EX2_LOC, SMK_EX2_SIZE), SMK_SIZE);
                        if (!isNumber(smk11)) {  //revert to default value
                           snprintf(smk11, SMK_SIZE, "%i", SMK_EX2);
                        }
                        memset(smk12, 0, SMK_SIZE+1);
                        memcpy(smk12,readEeprom(SMK_EX3_LOC, SMK_EX3_SIZE), SMK_SIZE);
                        if (!isNumber(smk12)) {  //revert to default value
                           snprintf(smk12, SMK_SIZE, "%i", SMK_EX3);
                        }
                        snprintf(wbuf, 1024, "I K %s %s %s %s %s %s %s %s %s %s %s %s %s\n", smkExists, smk1, smk2, smk3, smk4, smk5, smk6, smk7, smk8, smk9, smk10, smk11, smk12);
                        
                     }
                     break;
                  case 'L': case 'l':      // Sets and Reads the listen port number
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "I L %s\n", writeEeprom(LISTEN_PORT_LOC, arg3, LISTEN_PORT_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I L %s\n", readEeprom(LISTEN_PORT_LOC, LISTEN_PORT_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'M': case 'm':      // Sets and Reads the MAC address
                     if (arg3 > 1) { // are they passing in information?
						if (isMac(cmd+arg2)) {
                           snprintf(wbuf, 1024, "I M %s\n", writeEeprom(MAC_ADDRESS_LOC, arg3, MAC_ADDRESS_SIZE, cmd+arg2)); // writes and prints out what it wrote
                        } else {
                           snprintf(wbuf, 1024, "I M Invalid MAC address");
                        }
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I M %s\n", readEeprom(MAC_ADDRESS_LOC, MAC_ADDRESS_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'N': case 'n':	   // MFS defaults
                     if (sscanf(cmd+arg2, "%i %i %i %i %i %i %i %i %i %i %i %i %i %i", &nexists, &nparam1, &nparam2, &nparam3, &nparam4, &nparam5, &nparam6, &nparam7, &nparam8, &nparam9, &nparam10, &nparam11, &nparam12, &nparam13) == 14) {        // write
                        char mfsExist[5], nbuf1[5], nbuf2[5], nbuf3[5], nbuf4[5];  
                        char nbuf5[5], nbuf6[5], nbuf7[5], nbuf8[5];    
                        char nbuf9[5], nbuf10[5], nbuf11[5], nbuf12[5], nbuf13[5];  
                        // exists
                        snprintf(mfsExist, 5, "%i", nexists);                
                        // activate
                        snprintf(nbuf1, 5, "%i", nparam1);
                        // activate on what kind of exposure
                        snprintf(nbuf2, 5, "%i", nparam2);
                        // activate on hit
                        snprintf(nbuf3, 5, "%i", nparam3);
                        // activate on kill
                        snprintf(nbuf4, 5, "%i", nparam4);
                        // milliseconds on time
                        snprintf(nbuf5, 5, "%i", nparam5);
                        // milliseconds off time
                        snprintf(nbuf6, 5, "%i", nparam6);
                        // start delay
                        snprintf(nbuf7, 5, "%i", nparam7);
                        // repeat delay
                        snprintf(nbuf8, 5, "%i", nparam8);
                        // repeat count
                        snprintf(nbuf9, 5, "%i", nparam9);
                        // ex1
                        snprintf(nbuf10, 5, "%i", nparam10);
                        // ex2
                        snprintf(nbuf11, 5, "%i", nparam11);
                        // ex3
                        snprintf(nbuf12, 5, "%i", nparam12);
                        // mode
                        snprintf(nbuf13, 5, "%i", nparam13);
						// write all the mfs defaults
                        snprintf(wbuf, 1024, "I N %s %s %s %s %s %s %s %s %s %s %s %s %s %s\n", 
    writeEeprom(MFS_EXISTS_LOC, strnlen(mfsExist, 5), MFS_EXISTS_SIZE, mfsExist),
	writeEeprom(MFS_ACTIVATE_LOC, strnlen(nbuf1, 5), MFS_ACTIVATE_SIZE, nbuf1),
    writeEeprom(MFS_ACTIVATE_EXPOSE_LOC, strnlen(nbuf2, 5), MFS_ACTIVATE_EXPOSE_SIZE, nbuf2), 
    writeEeprom(MFS_ACTIVATE_ON_HIT_LOC, strnlen(nbuf3, 5), MFS_ACTIVATE_ON_HIT_SIZE, nbuf3), 
    writeEeprom(MFS_ACTIVATE_ON_KILL_LOC, strnlen(nbuf4, 5), MFS_ACTIVATE_ON_KILL_SIZE, nbuf4),
    writeEeprom(MFS_MS_ON_TIME_LOC, strnlen(nbuf5, 5), MFS_MS_ON_TIME_SIZE, nbuf5),
    writeEeprom(MFS_MS_OFF_TIME_LOC, strnlen(nbuf6, 5), MFS_MS_OFF_TIME_SIZE, nbuf6),
    writeEeprom(MFS_START_DELAY_LOC, strnlen(nbuf7, 5), MFS_START_DELAY_SIZE, nbuf7),
    writeEeprom(MFS_REPEAT_DELAY_LOC, strnlen(nbuf8, 5), MFS_REPEAT_DELAY_SIZE, nbuf8),
    writeEeprom(MFS_REPEAT_COUNT_LOC, strnlen(nbuf9, 5), MFS_REPEAT_COUNT_SIZE, nbuf9),
    writeEeprom(MFS_EX1_LOC, strnlen(nbuf10, 5), MFS_EX1_SIZE, nbuf10),
    writeEeprom(MFS_EX2_LOC, strnlen(nbuf11, 5), MFS_EX2_SIZE, nbuf11),
    writeEeprom(MFS_EX3_LOC, strnlen(nbuf12, 5), MFS_EX3_SIZE, nbuf12),
    writeEeprom(MFS_MODE_LOC, strnlen(nbuf13, 5), MFS_MODE_SIZE, nbuf13));
                     } else {	// read
						char mfsExists[MFS_SIZE+1], mfs1[MFS_SIZE+1], mfs2[MFS_SIZE+1], mfs3[MFS_SIZE+1], mfs4[MFS_SIZE+1], mfs5[MFS_SIZE+1], mfs6[MFS_SIZE+1], mfs7[MFS_SIZE+1], mfs8[MFS_SIZE+1], mfs9[MFS_SIZE+1], mfs10[MFS_SIZE+1], mfs11[MFS_SIZE+1], mfs12[MFS_SIZE+1], mfs13[MFS_SIZE+1];
						// exists
						memset(mfsExists, 0, MFS_SIZE+1);
						memcpy(mfsExists,readEeprom(MFS_EXISTS_LOC, MFS_EXISTS_SIZE), MFS_SIZE);
						if (!isNumber(mfsExists)) {  //revert to default value
                           snprintf(mfsExists, MFS_SIZE, "%i", MFS_EXISTS);
                        }
						memset(mfs1, 0, MFS_SIZE+1);
						memcpy(mfs1,readEeprom(MFS_ACTIVATE_LOC, MFS_ACTIVATE_SIZE), MFS_SIZE);
                        if (!isNumber(mfs1)) {  //revert to default value
                           snprintf(mfs1, MFS_SIZE, "%i", MFS_ACTIVATE);
                        }
                        memset(mfs2, 0, MFS_SIZE+1);
                        memcpy(mfs2,readEeprom(MFS_ACTIVATE_EXPOSE_LOC, MFS_ACTIVATE_EXPOSE_SIZE), MFS_SIZE);
                        if (!isNumber(mfs2)) {  //revert to default value
                           snprintf(mfs2, MFS_SIZE, "%i", MFS_ACTIVATE_EXPOSE);
                        }
                        memset(mfs3, 0, MFS_SIZE+1);
                        memcpy(mfs3,readEeprom(MFS_ACTIVATE_ON_HIT_LOC, MFS_ACTIVATE_ON_HIT_SIZE), MFS_SIZE);
                        if (!isNumber(mfs3)) {  //revert to default value
                           snprintf(mfs3, MFS_SIZE, "%i", MFS_ACTIVATE_ON_HIT);
                        }
                        memset(mfs4, 0, MFS_SIZE+1);
                        memcpy(mfs4,readEeprom(MFS_ACTIVATE_ON_KILL_LOC, MFS_ACTIVATE_ON_KILL_SIZE), MFS_SIZE);
                        if (!isNumber(mfs4)) {  //revert to default value
                           snprintf(mfs4, MFS_SIZE, "%i", MFS_ACTIVATE_ON_KILL);
                        }
                        memset(mfs5, 0, MFS_SIZE+1);
                        memcpy(mfs5,readEeprom(MFS_MS_ON_TIME_LOC, MFS_MS_ON_TIME_SIZE), MFS_SIZE);
                        if (!isNumber(mfs5)) {  //revert to default value
                           snprintf(mfs5, MFS_SIZE, "%i", MFS_MS_ON_TIME);
                        }
                        memset(mfs6, 0, MFS_SIZE+1);
                        memcpy(mfs6,readEeprom(MFS_MS_OFF_TIME_LOC, MFS_MS_OFF_TIME_SIZE), MFS_SIZE);
                        if (!isNumber(mfs6)) {  //revert to default value
                           snprintf(mfs6, MFS_SIZE, "%i", MFS_MS_OFF_TIME);
                        }
                        memset(mfs7, 0, MFS_SIZE+1);
                        memcpy(mfs7,readEeprom(MFS_START_DELAY_LOC, MFS_START_DELAY_SIZE), MFS_SIZE);
                        if (!isNumber(mfs7)) {  //revert to default value
                           snprintf(mfs7, MFS_SIZE, "%i", MFS_START_DELAY);
                        }
                        memset(mfs8, 0, MFS_SIZE+1);
                        memcpy(mfs8,readEeprom(MFS_REPEAT_DELAY_LOC, MFS_REPEAT_DELAY_SIZE), MFS_SIZE);
                        if (!isNumber(mfs8)) {  //revert to default value
                           snprintf(mfs8, MFS_SIZE, "%i", MFS_REPEAT_DELAY);
                        }
                        memset(mfs9, 0, MFS_SIZE+1);
                        memcpy(mfs9,readEeprom(MFS_REPEAT_COUNT_LOC, MFS_REPEAT_COUNT_SIZE), MFS_SIZE);
                        if (!isNumber(mfs9)) {  //revert to default value
                           snprintf(mfs9, MFS_SIZE, "%i", MFS_REPEAT_COUNT);
                        }
                        memset(mfs10, 0, MFS_SIZE+1);
                        memcpy(mfs10,readEeprom(MFS_EX1_LOC, MFS_EX1_SIZE), MFS_SIZE);
                        if (!isNumber(mfs10)) {  //revert to default value
                           snprintf(mfs10, MFS_SIZE, "%i", MFS_EX1);
                        }
                        memset(mfs11, 0, MFS_SIZE+1);
                        memcpy(mfs11,readEeprom(MFS_EX2_LOC, MFS_EX2_SIZE), MFS_SIZE);
                        if (!isNumber(mfs11)) {  //revert to default value
                           snprintf(mfs11, MFS_SIZE, "%i", MFS_EX2);
                        }
                        memset(mfs12, 0, MFS_SIZE+1);
                        memcpy(mfs12,readEeprom(MFS_EX3_LOC, MFS_EX3_SIZE), MFS_SIZE);
                        if (!isNumber(mfs12)) {  //revert to default value
                           snprintf(mfs12, MFS_SIZE, "%i", MFS_EX3);
                        }
                        memset(mfs13, 0, MFS_SIZE+1);
                        memcpy(mfs13,readEeprom(MFS_MODE_LOC, MFS_MODE_SIZE), MFS_SIZE);
                        if (!isNumber(mfs13)) {  //revert to default value
                           snprintf(mfs13, MFS_SIZE, "%i", MFS_MODE);
                        }
                        snprintf(wbuf, 1024, "I N %s %s %s %s %s %s %s %s %s %s %s %s %s %s\n", mfsExists, mfs1, mfs2, mfs3, mfs4, mfs5, mfs6, mfs7, mfs8, mfs9, mfs10, mfs11, mfs12, mfs13);
                        
                     }
                     break;
                  case 'O': case 'o':      // Sets and Reads bob type
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "I O %s\n", writeEeprom(BOB_TYPE_LOC, arg3, BOB_TYPE_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I O %s\n", readEeprom(BOB_TYPE_LOC, BOB_TYPE_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'P': case 'p':      // PHI defaults
                     if (sscanf(cmd+arg2, "%i %i %i %i %i %i %i %i %i %i %i %i %i", &pexists, &pparam1, &pparam2, &pparam3, &pparam4, &pparam5, &pparam6, &pparam7, &pparam8, &pparam9, &pparam10, &pparam11, &pparam12) == 13) {        // write
                        char phiExist[5], pbuf1[5], pbuf2[5], pbuf3[5], pbuf4[5];  
                        char pbuf5[5], pbuf6[5], pbuf7[5], pbuf8[5];    
                        char pbuf9[5], pbuf10[5], pbuf11[5], pbuf12[5];  
                        // exists
                        snprintf(phiExist, 5, "%i", pexists);                    
                        // activate
                        snprintf(pbuf1, 5, "%i", pparam1);
                        // activate on what kind of exposure
                        snprintf(pbuf2, 5, "%i", pparam2);
                        // activate on hit
                        snprintf(pbuf3, 5, "%i", pparam3);
                        // activate on kill
                        snprintf(pbuf4, 5, "%i", pparam4);
                        // milliseconds on time
                        snprintf(pbuf5, 5, "%i", pparam5);
                        // milliseconds off time
                        snprintf(pbuf6, 5, "%i", pparam6);
                        // start delay
                        snprintf(pbuf7, 5, "%i", pparam7);
                        // repeat delay
                        snprintf(pbuf8, 5, "%i", pparam8);
                        // repeat count
                        snprintf(pbuf9, 5, "%i", pparam9);
                        // ex1
                        snprintf(pbuf10, 5, "%i", pparam10);
                        // ex2
                        snprintf(pbuf11, 5, "%i", pparam11);
                        // ex3
                        snprintf(pbuf12, 5, "%i", pparam12);
						// write all the phi defaults
                        snprintf(wbuf, 1024, "I P %s %s %s %s %s %s %s %s %s %s %s %s %s\n", 
    writeEeprom(PHI_EXISTS_LOC, strnlen(phiExist, 5), PHI_EXISTS_SIZE, phiExist),
	writeEeprom(PHI_ACTIVATE_LOC, strnlen(pbuf1, 5), PHI_ACTIVATE_SIZE, pbuf1),
    writeEeprom(PHI_ACTIVATE_EXPOSE_LOC, strnlen(pbuf2, 5), PHI_ACTIVATE_EXPOSE_SIZE, pbuf2), 
    writeEeprom(PHI_ACTIVATE_ON_HIT_LOC, strnlen(pbuf3, 5), PHI_ACTIVATE_ON_HIT_SIZE, pbuf3), 
    writeEeprom(PHI_ACTIVATE_ON_KILL_LOC, strnlen(pbuf4, 5), PHI_ACTIVATE_ON_KILL_SIZE, pbuf4),
    writeEeprom(PHI_MS_ON_TIME_LOC, strnlen(pbuf5, 5), PHI_MS_ON_TIME_SIZE, pbuf5),
    writeEeprom(PHI_MS_OFF_TIME_LOC, strnlen(pbuf6, 5), PHI_MS_OFF_TIME_SIZE, pbuf6),
    writeEeprom(PHI_START_DELAY_LOC, strnlen(pbuf7, 5), PHI_START_DELAY_SIZE, pbuf7),
    writeEeprom(PHI_REPEAT_DELAY_LOC, strnlen(pbuf8, 5), PHI_REPEAT_DELAY_SIZE, pbuf8),
    writeEeprom(PHI_REPEAT_COUNT_LOC, strnlen(pbuf9, 5), PHI_REPEAT_COUNT_SIZE, pbuf9),
    writeEeprom(PHI_EX1_LOC, strnlen(pbuf10, 5), PHI_EX1_SIZE, pbuf10),
    writeEeprom(PHI_EX2_LOC, strnlen(pbuf11, 5), PHI_EX2_SIZE, pbuf11),
    writeEeprom(PHI_EX3_LOC, strnlen(pbuf12, 5), PHI_EX3_SIZE, pbuf12));
                     } else {	// read
						char phiExists[PHI_SIZE+1], phi1[PHI_SIZE+1], phi2[PHI_SIZE+1], phi3[PHI_SIZE+1], phi4[PHI_SIZE+1], phi5[PHI_SIZE+1], phi6[PHI_SIZE+1], phi7[PHI_SIZE+1], phi8[PHI_SIZE+1], phi9[PHI_SIZE+1], phi10[PHI_SIZE+1], phi11[PHI_SIZE+1], phi12[PHI_SIZE+1];
                        memset(phiExists, 0, PHI_SIZE+1);
						memcpy(phiExists,readEeprom(PHI_EXISTS_LOC, PHI_EXISTS_SIZE), PHI_SIZE);
                        if (!isNumber(phiExists)) {  //revert to default value
                           snprintf(phiExists, PHI_SIZE, "%i", PHI_EXISTS);
                        }
						memset(phi1, 0, PHI_SIZE+1);
						memcpy(phi1,readEeprom(PHI_ACTIVATE_LOC, PHI_ACTIVATE_SIZE), PHI_SIZE);
                        if (!isNumber(phi1)) {  //revert to default value
                           snprintf(phi1, PHI_SIZE, "%i", PHI_ACTIVATE);
                        }
                        memset(phi2, 0, PHI_SIZE+1);
                        memcpy(phi2,readEeprom(PHI_ACTIVATE_EXPOSE_LOC, PHI_ACTIVATE_EXPOSE_SIZE), PHI_SIZE);
                        if (!isNumber(phi2)) {  //revert to default value
                           snprintf(phi2, PHI_SIZE, "%i", PHI_ACTIVATE_EXPOSE);
                        }
                        memset(phi3, 0, PHI_SIZE+1);
                        memcpy(phi3,readEeprom(PHI_ACTIVATE_ON_HIT_LOC, PHI_ACTIVATE_ON_HIT_SIZE), PHI_SIZE);
                        if (!isNumber(phi3)) {  //revert to default value
                           snprintf(phi3, PHI_SIZE, "%i", PHI_ACTIVATE_ON_HIT);
                        }
                        memset(phi4, 0, PHI_SIZE+1);
                        memcpy(phi4,readEeprom(PHI_ACTIVATE_ON_KILL_LOC, PHI_ACTIVATE_ON_KILL_SIZE), PHI_SIZE);
                        if (!isNumber(phi4)) {  //revert to default value
                           snprintf(phi4, PHI_SIZE, "%i", PHI_ACTIVATE_ON_KILL);
                        }
                        memset(phi5, 0, PHI_SIZE+1);
                        memcpy(phi5,readEeprom(PHI_MS_ON_TIME_LOC, PHI_MS_ON_TIME_SIZE), PHI_SIZE);
                        if (!isNumber(phi5)) {  //revert to default value
                           snprintf(phi5, PHI_SIZE, "%i", PHI_MS_ON_TIME);
                        }
                        memset(phi6, 0, PHI_SIZE+1);
                        memcpy(phi6,readEeprom(PHI_MS_OFF_TIME_LOC, PHI_MS_OFF_TIME_SIZE), PHI_SIZE);
                        if (!isNumber(phi6)) {  //revert to default value
                           snprintf(phi6, PHI_SIZE, "%i", PHI_MS_OFF_TIME);
                        }
                        memset(phi7, 0, PHI_SIZE+1);
                        memcpy(phi7,readEeprom(PHI_START_DELAY_LOC, PHI_START_DELAY_SIZE), PHI_SIZE);
                        if (!isNumber(phi7)) {  //revert to default value
                           snprintf(phi7, PHI_SIZE, "%i", PHI_START_DELAY);
                        }
                        memset(phi8, 0, PHI_SIZE+1);
                        memcpy(phi8,readEeprom(PHI_REPEAT_DELAY_LOC, PHI_REPEAT_DELAY_SIZE), PHI_SIZE);
                        if (!isNumber(phi8)) {  //revert to default value
                           snprintf(phi8, PHI_SIZE, "%i", PHI_REPEAT_DELAY);
                        }
                        memset(phi9, 0, PHI_SIZE+1);
                        memcpy(phi9,readEeprom(PHI_REPEAT_COUNT_LOC, PHI_REPEAT_COUNT_SIZE), PHI_SIZE);
                        if (!isNumber(phi9)) {  //revert to default value
                           snprintf(phi9, PHI_SIZE, "%i", PHI_REPEAT_COUNT);
                        }
                        memset(phi10, 0, PHI_SIZE+1);
                        memcpy(phi10,readEeprom(PHI_EX1_LOC, PHI_EX1_SIZE), PHI_SIZE);
                        if (!isNumber(phi10)) {  //revert to default value
                           snprintf(phi10, PHI_SIZE, "%i", PHI_EX1);
                        }
                        memset(phi11, 0, PHI_SIZE+1);
                        memcpy(phi11,readEeprom(PHI_EX2_LOC, PHI_EX2_SIZE), PHI_SIZE);
                        if (!isNumber(phi11)) {  //revert to default value
                           snprintf(phi11, PHI_SIZE, "%i", PHI_EX2);
                        }
                        memset(phi12, 0, PHI_SIZE+1);
                        memcpy(phi12,readEeprom(PHI_EX3_LOC, PHI_EX3_SIZE), PHI_SIZE);
                        if (!isNumber(phi12)) {  //revert to default value
                           snprintf(phi12, PHI_SIZE, "%i", PHI_EX3);
                        }
                        snprintf(wbuf, 1024, "I P %s %s %s %s %s %s %s %s %s %s %s %s %s\n", phiExists, phi1, phi2, phi3, phi4, phi5, phi6, phi7, phi8, phi9, phi10, phi11, phi12);
                        
                     }
                     break;
                  case 'Q': case 'q':      // Sets and Reads the Default Docking End
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "I Q %s\n", writeEeprom(DOCKING_END_LOC, arg3, DOCKING_END_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I Q %s\n", readEeprom(DOCKING_END_LOC, DOCKING_END_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'R': case 'r':      // Reboot
                     //printf("Go down for a reboot");
                     snprintf(wbuf, 1024, "I R\n"); // rebooting
                     runCmd("reboot", 1, NULL, 0);
                     break;
                  case 'S': case 's':		// Hit Sensor Defaults
                     if (sscanf(cmd+arg2, "%i %i", &sparam1, &sparam2) == 2) {  // write
                        //write
                        char sbuf1[5], sbuf2[5];
                        // hit sensor type
                        snprintf(sbuf1, 5, "%i", sparam1);
                        // Invert sensor input line
                        snprintf(sbuf2, 5, "%i", sparam2);
                        // write out the hit sensor defaults
                        snprintf(wbuf, 1024, "I S %s %s\n", writeEeprom(HIT_SENSOR_TYPE_LOC, strlen(sbuf1)+1, HIT_SENSOR_TYPE_SIZE, sbuf1), writeEeprom(HIT_SENSOR_INVERT_LOC, strlen(sbuf2)+1, HIT_SENSOR_TYPE_SIZE, sbuf2));
                     } else {   // read
                        char str[HIT_SENSOR_TYPE_SIZE+1], isr[HIT_SENSOR_TYPE_SIZE+1];
                        memset(str, 0, HIT_SENSOR_TYPE_SIZE+1);
                        memcpy(str, readEeprom(HIT_SENSOR_TYPE_LOC, HIT_SENSOR_TYPE_SIZE), HIT_SENSOR_TYPE_SIZE);
                        if (!isNumber(str)) {  //revert to default value
                           snprintf(str, HIT_SENSOR_TYPE_SIZE, "%i", HIT_SENSOR_TYPE);
                        }
                        memset(isr, 0, HIT_SENSOR_TYPE_SIZE+1);
                        memcpy(isr, readEeprom(HIT_SENSOR_INVERT_LOC, HIT_SENSOR_TYPE_SIZE), HIT_SENSOR_TYPE_SIZE);
                        if (!isNumber(isr)) {  //revert to default value
                           snprintf(isr, HIT_SENSOR_TYPE_SIZE, "%i", HIT_SENSOR_INVERT);
                        }
                        snprintf(wbuf, 1024, "I S %s %s\n", str, isr);
                     }
                     break;
                  case 'T': case 't':	   // Thermal defaults
                     if (sscanf(cmd+arg2, "%i %i %i %i %i %i %i %i %i %i %i %i %i", &texists, &tparam1, &tparam2, &tparam3, &tparam4, &tparam5, &tparam6, &tparam7, &tparam8, &tparam9, &tparam10, &tparam11, &tparam12) == 13) {        // write
                        char thmExist[5], tbuf1[5], tbuf2[5], tbuf3[5], tbuf4[5];  
                        char tbuf5[5], tbuf6[5], tbuf7[5], tbuf8[5];    
                        char tbuf9[5], tbuf10[5], tbuf11[5], tbuf12[5]; 
                        // exists
                        snprintf(thmExist, 5, "%i", texists);                     
                        // activate
                        snprintf(tbuf1, 5, "%i", tparam1);
                        // activate on what kind of exposure
                        snprintf(tbuf2, 5, "%i", tparam2);
                        // activate on hit
                        snprintf(tbuf3, 5, "%i", tparam3);
                        // activate on kill
                        snprintf(tbuf4, 5, "%i", tparam4);
                        // milliseconds on time
                        snprintf(tbuf5, 5, "%i", tparam5);
                        // milliseconds off time
                        snprintf(tbuf6, 5, "%i", tparam6);
                        // start delay
                        snprintf(tbuf7, 5, "%i", tparam7);
                        // repeat delay
                        snprintf(tbuf8, 5, "%i", tparam8);
                        // repeat count
                        snprintf(tbuf9, 5, "%i", tparam9);
                        // ex1
                        snprintf(tbuf10, 5, "%i", tparam10);
                        // ex2
                        snprintf(tbuf11, 5, "%i", tparam11);
                        // ex3
                        snprintf(tbuf12, 5, "%i", tparam12);
						// write all the thm defaults
                        snprintf(wbuf, 1024, "I T %s %s %s %s %s %s %s %s %s %s %s %s %s\n", 
    writeEeprom(THM_EXISTS_LOC, strnlen(thmExist, 5), THM_EXISTS_SIZE, thmExist),
	writeEeprom(THM_ACTIVATE_LOC, strnlen(tbuf1, 5), THM_ACTIVATE_SIZE, tbuf1),
    writeEeprom(THM_ACTIVATE_EXPOSE_LOC, strnlen(tbuf2, 5), THM_ACTIVATE_EXPOSE_SIZE, tbuf2), 
    writeEeprom(THM_ACTIVATE_ON_HIT_LOC, strnlen(tbuf3, 5), THM_ACTIVATE_ON_HIT_SIZE, tbuf3), 
    writeEeprom(THM_ACTIVATE_ON_KILL_LOC, strnlen(tbuf4, 5), THM_ACTIVATE_ON_KILL_SIZE, tbuf4),
    writeEeprom(THM_MS_ON_TIME_LOC, strnlen(tbuf5, 5), THM_MS_ON_TIME_SIZE, tbuf5),
    writeEeprom(THM_MS_OFF_TIME_LOC, strnlen(tbuf6, 5), THM_MS_OFF_TIME_SIZE, tbuf6),
    writeEeprom(THM_START_DELAY_LOC, strnlen(tbuf7, 5), THM_START_DELAY_SIZE, tbuf7),
    writeEeprom(THM_REPEAT_DELAY_LOC, strnlen(tbuf8, 5), THM_REPEAT_DELAY_SIZE, tbuf8),
    writeEeprom(THM_REPEAT_COUNT_LOC, strnlen(tbuf9, 5), THM_REPEAT_COUNT_SIZE, tbuf9),
    writeEeprom(THM_EX1_LOC, strnlen(tbuf10, 5), THM_EX1_SIZE, tbuf10),
    writeEeprom(THM_EX2_LOC, strnlen(tbuf11, 5), THM_EX2_SIZE, tbuf11),
    writeEeprom(THM_EX3_LOC, strnlen(tbuf12, 5), THM_EX3_SIZE, tbuf12));
                     } else {	// read
						char thmExists[THM_SIZE+1], thm1[THM_SIZE+1], thm2[THM_SIZE+1], thm3[THM_SIZE+1], thm4[THM_SIZE+1], thm5[THM_SIZE+1], thm6[THM_SIZE+1], thm7[THM_SIZE+1], thm8[THM_SIZE+1], thm9[THM_SIZE+1], thm10[THM_SIZE+1], thm11[THM_SIZE+1], thm12[THM_SIZE+1];
                        memset(thmExists, 0, THM_SIZE+1);
						memcpy(thmExists,readEeprom(THM_EXISTS_LOC, THM_EXISTS_SIZE), THM_SIZE);
                        if (!isNumber(thmExists)) {  //revert to default value
                           snprintf(thmExists, THM_SIZE, "%i", THM_EXISTS);
                        }
						memset(thm1, 0, THM_SIZE+1);
						memcpy(thm1,readEeprom(THM_ACTIVATE_LOC, THM_ACTIVATE_SIZE), THM_SIZE);
                        if (!isNumber(thm1)) {  //revert to default value
                           snprintf(thm1, THM_SIZE, "%i", THM_ACTIVATE);
                        }
                        memset(thm2, 0, THM_SIZE+1);
                        memcpy(thm2,readEeprom(THM_ACTIVATE_EXPOSE_LOC, THM_ACTIVATE_EXPOSE_SIZE), THM_SIZE);
                        if (!isNumber(thm2)) {  //revert to default value
                           snprintf(thm2, THM_SIZE, "%i", THM_ACTIVATE_EXPOSE);
                        }
                        memset(thm3, 0, THM_SIZE+1);
                        memcpy(thm3,readEeprom(THM_ACTIVATE_ON_HIT_LOC, THM_ACTIVATE_ON_HIT_SIZE), THM_SIZE);
                        if (!isNumber(thm3)) {  //revert to default value
                           snprintf(thm3, THM_SIZE, "%i", THM_ACTIVATE_ON_HIT);
                        }
                        memset(thm4, 0, THM_SIZE+1);
                        memcpy(thm4,readEeprom(THM_ACTIVATE_ON_KILL_LOC, THM_ACTIVATE_ON_KILL_SIZE), THM_SIZE);
                        if (!isNumber(thm4)) {  //revert to default value
                           snprintf(thm4, THM_SIZE, "%i", THM_ACTIVATE_ON_KILL);
                        }
                        memset(thm5, 0, THM_SIZE+1);
                        memcpy(thm5,readEeprom(THM_MS_ON_TIME_LOC, THM_MS_ON_TIME_SIZE), THM_SIZE);
                        if (!isNumber(thm5)) {  //revert to default value
                           snprintf(thm5, THM_SIZE, "%i", THM_MS_ON_TIME);
                        }
                        memset(thm6, 0, THM_SIZE+1);
                        memcpy(thm6,readEeprom(THM_MS_OFF_TIME_LOC, THM_MS_OFF_TIME_SIZE), THM_SIZE);
                        if (!isNumber(thm6)) {  //revert to default value
                           snprintf(thm6, THM_SIZE, "%i", THM_MS_OFF_TIME);
                        }
                        memset(thm7, 0, THM_SIZE+1);
                        memcpy(thm7,readEeprom(THM_START_DELAY_LOC, THM_START_DELAY_SIZE), THM_SIZE);
                        if (!isNumber(thm7)) {  //revert to default value
                           snprintf(thm7, THM_SIZE, "%i", THM_START_DELAY);
                        }
                        memset(thm8, 0, THM_SIZE+1);
                        memcpy(thm8,readEeprom(THM_REPEAT_DELAY_LOC, THM_REPEAT_DELAY_SIZE), THM_SIZE);
                        if (!isNumber(thm8)) {  //revert to default value
                           snprintf(thm8, THM_SIZE, "%i", THM_REPEAT_DELAY);
                        }
                        memset(thm9, 0, THM_SIZE+1);
                        memcpy(thm9,readEeprom(THM_REPEAT_COUNT_LOC, THM_REPEAT_COUNT_SIZE), THM_SIZE);
                        if (!isNumber(thm9)) {  //revert to default value
                           snprintf(thm9, THM_SIZE, "%i", THM_REPEAT_COUNT);
                        }
                        memset(thm10, 0, THM_SIZE+1);
                        memcpy(thm10,readEeprom(THM_EX1_LOC, THM_EX1_SIZE), THM_SIZE);
                        if (!isNumber(thm10)) {  //revert to default value
                           snprintf(thm10, THM_SIZE, "%i", THM_EX1);
                        }
                        memset(thm11, 0, THM_SIZE+1);
                        memcpy(thm11,readEeprom(THM_EX2_LOC, THM_EX2_SIZE), THM_SIZE);
                        if (!isNumber(thm11)) {  //revert to default value
                           snprintf(thm11, THM_SIZE, "%i", THM_EX2);
                        }
                        memset(thm12, 0, THM_SIZE+1);
                        memcpy(thm12,readEeprom(THM_EX3_LOC, THM_EX3_SIZE), THM_SIZE);
                        if (!isNumber(thm12)) {  //revert to default value
                           snprintf(thm12, THM_SIZE, "%i", THM_EX3);
                        }
                        snprintf(wbuf, 1024, "I T %s %s %s %s %s %s %s %s %s %s %s %s %s\n", thmExists, thm1, thm2, thm3, thm4, thm5, thm6, thm7, thm8, thm9, thm10, thm11, thm12);
                     }
                     break; 
                  case 'U': case 'u':  // radio frequency
                     if (arg3 > 1) { // are they passing in information?
                        // writes not written
                        writeEeprom(RADIO_WRITTEN_LOC, 1, RADIO_WRITTEN_SIZE, RADIO_WRITTEN);
                        snprintf(wbuf, 1024, "I U %s\n", writeEeprom(RADIO_FREQ_LOC, arg3, RADIO_FREQ_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I U %s\n", readEeprom(RADIO_FREQ_LOC, RADIO_FREQ_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'V': case 'v':  // radio power low
                     if (arg3 > 1) { // are they passing in information?
                        // writes not written
                        writeEeprom(RADIO_WRITTEN_LOC, 1, RADIO_WRITTEN_SIZE, RADIO_WRITTEN);
                        snprintf(wbuf, 1024, "I V %s\n", writeEeprom(RADIO_POWER_L_LOC, arg3, RADIO_POWER_L_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I V %s\n", readEeprom(RADIO_POWER_L_LOC, RADIO_POWER_L_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'W': case 'w':  // radio power high
                     if (arg3 > 1) { // are they passing in information?
                        // writes not written
                        writeEeprom(RADIO_WRITTEN_LOC, 3, RADIO_WRITTEN_SIZE, RADIO_WRITTEN);
                        snprintf(wbuf, 1024, "I W %s\n", writeEeprom(RADIO_POWER_H_LOC, arg3, RADIO_POWER_H_SIZE, cmd+arg2));
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I W %s\n", readEeprom(RADIO_POWER_H_LOC, RADIO_POWER_H_SIZE)); 
                     }
                     break;
                  case 'X': case 'x':      // Sets and Reads the serial number
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "I X %s\n", writeEeprom(SERIAL_NUMBER_LOC, arg3, SERIAL_NUMBER_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        char serial_holder[SERIAL_SIZE+1];
                        memset(serial_holder, 0 , SERIAL_SIZE+1);
                        memcpy(serial_holder, readEeprom(SERIAL_NUMBER_LOC, SERIAL_NUMBER_SIZE), SERIAL_SIZE);
                        if (memcmp(serial_holder, "", 1)==0) { //use default value
                           snprintf(serial_holder, SERIAL_SIZE, "%s", SERIAL_NUMBER);
                        }
                        snprintf(wbuf, 1024, "I X %s\n", serial_holder); 
                     }
                     break;
                  case 'Y': case 'y':	   // MSDH defaults
                     if (sscanf(cmd+arg2, "%i %i %i %i %i %i %i %i %i %i %i %i %i", &yexists, &yparam1, &yparam2, &yparam3, &yparam4, &yparam5, &yparam6, &yparam7, &yparam8, &yparam9, &yparam10, &yparam11, &yparam12) == 13) {        // write
                        char msdExist[5], ybuf1[5], ybuf2[5], ybuf3[5], ybuf4[5];  
                        char ybuf5[5], ybuf6[5], ybuf7[5], ybuf8[5];    
                        char ybuf9[5], ybuf10[5], ybuf11[5], ybuf12[5]; 
                        // exists
                        snprintf(msdExist, 5, "%i", yexists);                     
                        // activate
                        snprintf(ybuf1, 5, "%i", yparam1);
                        // activate on what kind of exposure
                        snprintf(ybuf2, 5, "%i", yparam2);
                        // activate on hit
                        snprintf(ybuf3, 5, "%i", yparam3);
                        // activate on kill
                        snprintf(ybuf4, 5, "%i", yparam4);
                        // milliseconds on time
                        snprintf(ybuf5, 5, "%i", yparam5);
                        // milliseconds off time
                        snprintf(ybuf6, 5, "%i", yparam6);
                        // start delay
                        snprintf(ybuf7, 5, "%i", yparam7);
                        // repeat delay
                        snprintf(ybuf8, 5, "%i", yparam8);
                        // repeat count
                        snprintf(ybuf9, 5, "%i", yparam9);
                        // ex1
                        snprintf(ybuf10, 5, "%i", yparam10);
                        // ex2
                        snprintf(ybuf11, 5, "%i", yparam11);
                        // ex3
                        snprintf(ybuf12, 5, "%i", yparam12);
						// write all the thm defaults
                        snprintf(wbuf, 1024, "I Y %s %s %s %s %s %s %s %s %s %s %s %s %s\n", 
    writeEeprom(MSD_EXISTS_LOC, strnlen(msdExist, 5), MSD_EXISTS_SIZE, msdExist),
	writeEeprom(MSD_ACTIVATE_LOC, strnlen(ybuf1, 5), MSD_ACTIVATE_SIZE, ybuf1),
    writeEeprom(MSD_ACTIVATE_EXPOSE_LOC, strnlen(ybuf2, 5), MSD_ACTIVATE_EXPOSE_SIZE, ybuf2), 
    writeEeprom(MSD_ACTIVATE_ON_HIT_LOC, strnlen(ybuf3, 5), MSD_ACTIVATE_ON_HIT_SIZE, ybuf3), 
    writeEeprom(MSD_ACTIVATE_ON_KILL_LOC, strnlen(ybuf4, 5), MSD_ACTIVATE_ON_KILL_SIZE, ybuf4),
    writeEeprom(MSD_MS_ON_TIME_LOC, strnlen(ybuf5, 5), MSD_MS_ON_TIME_SIZE, ybuf5),
    writeEeprom(MSD_MS_OFF_TIME_LOC, strnlen(ybuf6, 5), MSD_MS_OFF_TIME_SIZE, ybuf6),
    writeEeprom(MSD_START_DELAY_LOC, strnlen(ybuf7, 5), MSD_START_DELAY_SIZE, ybuf7),
    writeEeprom(MSD_REPEAT_DELAY_LOC, strnlen(ybuf8, 5), MSD_REPEAT_DELAY_SIZE, ybuf8),
    writeEeprom(MSD_REPEAT_COUNT_LOC, strnlen(ybuf9, 5), MSD_REPEAT_COUNT_SIZE, ybuf9),
    writeEeprom(MSD_EX1_LOC, strnlen(ybuf10, 5), MSD_EX1_SIZE, ybuf10),
    writeEeprom(MSD_EX2_LOC, strnlen(ybuf11, 5), MSD_EX2_SIZE, ybuf11),
    writeEeprom(MSD_EX3_LOC, strnlen(ybuf12, 5), MSD_EX3_SIZE, ybuf12));
                     } else {	// read
						char msdExists[MSD_SIZE+1], msd1[MSD_SIZE+1], msd2[MSD_SIZE+1], msd3[MSD_SIZE+1], msd4[MSD_SIZE+1], msd5[MSD_SIZE+1], msd6[MSD_SIZE+1], msd7[MSD_SIZE+1], msd8[MSD_SIZE+1], msd9[MSD_SIZE+1], msd10[MSD_SIZE+1], msd11[MSD_SIZE+1], msd12[MSD_SIZE+1];
                        memset(msdExists, 0, MSD_SIZE+1);
						memcpy(msdExists,readEeprom(MSD_EXISTS_LOC, MSD_EXISTS_SIZE), MSD_SIZE);
                        if (!isNumber(msdExists)) {  //revert to default value
                           snprintf(msdExists, MSD_SIZE, "%i", MSD_EXISTS);
                        }
						memset(msd1, 0, MSD_SIZE+1);
						memcpy(msd1,readEeprom(MSD_ACTIVATE_LOC, MSD_ACTIVATE_SIZE), MSD_SIZE);
                        if (!isNumber(msd1)) {  //revert to default value
                           snprintf(msd1, MSD_SIZE, "%i", MSD_ACTIVATE);
                        }
                        memset(msd2, 0, MSD_SIZE+1);
                        memcpy(msd2,readEeprom(MSD_ACTIVATE_EXPOSE_LOC, MSD_ACTIVATE_EXPOSE_SIZE), MSD_SIZE);
                        if (!isNumber(msd2)) {  //revert to default value
                           snprintf(msd2, MSD_SIZE, "%i", MSD_ACTIVATE_EXPOSE);
                        }
                        memset(msd3, 0, MSD_SIZE+1);
                        memcpy(msd3,readEeprom(MSD_ACTIVATE_ON_HIT_LOC, MSD_ACTIVATE_ON_HIT_SIZE), MSD_SIZE);
                        if (!isNumber(msd3)) {  //revert to default value
                           snprintf(msd3, MSD_SIZE, "%i", MSD_ACTIVATE_ON_HIT);
                        }
                        memset(msd4, 0, MSD_SIZE+1);
                        memcpy(msd4,readEeprom(MSD_ACTIVATE_ON_KILL_LOC, MSD_ACTIVATE_ON_KILL_SIZE), MSD_SIZE);
                        if (!isNumber(msd4)) {  //revert to default value
                           snprintf(msd4, MSD_SIZE, "%i", MSD_ACTIVATE_ON_KILL);
                        }
                        memset(msd5, 0, MSD_SIZE+1);
                        memcpy(msd5,readEeprom(MSD_MS_ON_TIME_LOC, MSD_MS_ON_TIME_SIZE), MSD_SIZE);
                        if (!isNumber(msd5)) {  //revert to default value
                           snprintf(msd5, MSD_SIZE, "%i", MSD_MS_ON_TIME);
                        }
                        memset(msd6, 0, MSD_SIZE+1);
                        memcpy(msd6,readEeprom(MSD_MS_OFF_TIME_LOC, MSD_MS_OFF_TIME_SIZE), MSD_SIZE);
                        if (!isNumber(msd6)) {  //revert to default value
                           snprintf(msd6, MSD_SIZE, "%i", MSD_MS_OFF_TIME);
                        }
                        memset(msd7, 0, MSD_SIZE+1);
                        memcpy(msd7,readEeprom(MSD_START_DELAY_LOC, MSD_START_DELAY_SIZE), MSD_SIZE);
                        if (!isNumber(msd7)) {  //revert to default value
                           snprintf(msd7, MSD_SIZE, "%i", MSD_START_DELAY);
                        }
                        memset(msd8, 0, MSD_SIZE+1);
                        memcpy(msd8,readEeprom(MSD_REPEAT_DELAY_LOC, MSD_REPEAT_DELAY_SIZE), MSD_SIZE);
                        if (!isNumber(msd8)) {  //revert to default value
                           snprintf(msd8, MSD_SIZE, "%i", MSD_REPEAT_DELAY);
                        }
                        memset(msd9, 0, MSD_SIZE+1);
                        memcpy(msd9,readEeprom(MSD_REPEAT_COUNT_LOC, MSD_REPEAT_COUNT_SIZE), MSD_SIZE);
                        if (!isNumber(msd9)) {  //revert to default value
                           snprintf(msd9, MSD_SIZE, "%i", MSD_REPEAT_COUNT);
                        }
                        memset(msd10, 0, MSD_SIZE+1);
                        memcpy(msd10,readEeprom(MSD_EX1_LOC, MSD_EX1_SIZE), MSD_SIZE);
                        if (!isNumber(msd10)) {  //revert to default value
                           snprintf(msd10, MSD_SIZE, "%i", MSD_EX1);
                        }
                        memset(msd11, 0, MSD_SIZE+1);
                        memcpy(msd11,readEeprom(MSD_EX2_LOC, MSD_EX2_SIZE), MSD_SIZE);
                        if (!isNumber(msd11)) {  //revert to default value
                           snprintf(msd11, MSD_SIZE, "%i", MSD_EX2);
                        }
                        memset(msd12, 0, MSD_SIZE+1);
                        memcpy(msd12,readEeprom(MSD_EX3_LOC, MSD_EX3_SIZE), MSD_SIZE);
                        if (!isNumber(msd12)) {  //revert to default value
                           snprintf(msd12, MSD_SIZE, "%i", MSD_EX3);
                        }
                        snprintf(wbuf, 1024, "I Y %s %s %s %s %s %s %s %s %s %s %s %s %s\n", msdExists, msd1, msd2, msd3, msd4, msd5, msd6, msd7, msd8, msd9, msd10, msd11, msd12);
                     }
                     break;
                  case 'Z': case 'z':      // Sets and Reads the Default Home End
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "I Z %s\n", writeEeprom(HOME_END_LOC, arg3, HOME_END_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "I Z %s\n", readEeprom(HOME_END_LOC, HOME_END_SIZE)); // reads and prints out what it read
                     }
                     break;
               }
               write(client, wbuf, strnlen(wbuf,1024));
               break;
           case 'J': case 'j':
               arg1 = cmd[1] == ' ' ? 2 : 1; // find index of second argument
               arg2 = cmd[arg1+1] == ' ' ? arg1+2 : arg1+1; // find index of third argument
               arg3 = strlen(cmd+arg2)+1; // size value, add 1 for null terminator 
               switch (cmd[arg1]) { /* second letter */
                  case 'A': case 'a':	  // Reads and sets the full flash version
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "J A %s\n", writeEeprom(MAJOR_VERSION_LOC, arg3, MAJOR_VERSION_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "J A %s\n", readEeprom(MAJOR_VERSION_LOC, MAJOR_VERSION_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'B': case 'b':	  // Reads and sets the partial flash version
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "J B %s\n", writeEeprom(MINOR_VERSION_LOC, arg3, MINOR_VERSION_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "J B %s\n", readEeprom(MINOR_VERSION_LOC, MINOR_VERSION_SIZE)); // reads and prints out what it read
                     }
                     break;
                  case 'C': case 'c':	  // Will the radio reprogram?
                     if (arg3 > 1) { // are they passing in information?
                        snprintf(wbuf, 1024, "J C %s\n", writeEeprom(RADIO_WRITTEN_LOC, arg3, RADIO_WRITTEN_SIZE, cmd+arg2)); // writes and prints out what it wrote
                     } else { // they are reading information
                        snprintf(wbuf, 1024, "J C %s\n", readEeprom(RADIO_WRITTEN_LOC, RADIO_WRITTEN_SIZE)); // reads and prints out what it read
                     }
                     break;
               }
               write(client, wbuf, strnlen(wbuf,1024));
               break;
            case '#':
                if (sscanf(cmd+1, "%i", &arg1) == 1) {
                   epoll_ctl(efd, EPOLL_CTL_DEL, nl_fd, NULL);
                   nl_handle_destroy(handle);
                   g_handle = handlecreate_nl_handle(client, arg1);
                }
                break;
            case '?':
                // help command
                arg1 = cmd[1] == ' ' ? 2 : 1; // next letter location in the command (ignore up to one space)
				arg2 = cmd[3] == ' ' ? 4 : 3;
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
                        snprintf(wbuf, 1024, "Request fall parameters\nFormat: F\nChange fall parameters\nFormat: F (0-100)kill_at_x_hits (0|1|2|3|4)at_kill_do_fall_or_kill_or_stop_or_fall_and_stop_or_bob\n");
                        break;
                    case 'G': case 'g':
                        snprintf(wbuf, 1024, "Request GPS data\nFormat: G\n");
                        break;
                    case 'H': case 'h':
                        snprintf(wbuf, 1024, "Request hit data\nFormat: H\nReset Hit data\nFormat: H 0\n");
                        break;
                    case 'I': case 'i':
                        switch (cmd[arg2]) { /* second letter */
                            case 'A': case 'a':
                                snprintf(wbuf, 1024, "Get/Set address location\nFormat: I A <address location>\n");
                                break;
			                case 'B': case 'b':
                          	    snprintf(wbuf, 1024, "Get/Set board type\nFormat: I B <HSAT>, <LSAT>, <MAT>, <MIT>, <SES>, <SIT>, <BASE>, <HHC>\n");
				                break;
			                case 'D': case 'd':
                        	    snprintf(wbuf, 1024, "Get/Set comm type\nFormat: I D <local>, <network>, <radio>, <wifi>, <wimax>\n");
				                break;
			                case 'I': case 'i':
                        	    snprintf(wbuf, 1024, "Get/Set current IP address\nFormat: I P <ip>\n");			
				                break;
			                case 'C': case 'c':
                        	    snprintf(wbuf, 1024, "Get/Set connect port number\nFormat: I C <cport>\n");		
                                break;
                            case 'E': case 'e':
                                snprintf(wbuf, 1024, "Request battery/mover defaults\nFormat I E (SIT|SAT|SES|MIT|MAT|REVERSE)\nChange battery/mover defaults\nFormat I E (1-SIT|2-SAT|3-SES|4-MIT|5-MAT|6-Mover Reverse) (default)\n");
                                break;
                            case 'F': case 'f':
                                snprintf(wbuf, 1024, "Request fall parameters\nFormat: I F\nChange fall parameters\nFormat: I F (0-100)kill_at_x_hits (0|1|2|3|4)at_kill_do_fall_or_kill_or_stop_or_fall_and_stop_or_bob\n");
								break;
                            case 'G': case 'g':
                                snprintf(wbuf, 1024, "Request MGL defaults\nFormat: I G\nChange MGL defaults\nFormat: I G (0|1|2)active_soon_or_immediate (0|1|2|3)active_on_full_expose_or_partial_expose_or_during_partial (0|1|2)active_or_deactive_on_hit (0|1|2)active_or_deactive_on_kill (0-60000)milliseconds_on_time (0-60000)milliseconds_off_time (0-250)halfseconds_start_delay (0-250)halfseconds_repeat_delay (0-62|63)repeat_count_or_infinite ex1 ex2 ex3\n");
                                break;
                            case 'H': case 'h':
                                snprintf(wbuf, 1024, "Request hit calibration parameters\nFormat: I H\nChange hit calibration parameters\nFormat: I H (1-10000)milliseconds_between_hits (1-1000)hit_desensitivity (0-50000)milliseconds_blanking_time_from_start_expose (0-4)enable_on_value (0-blank_always, 1-enable_always, 2-enable_at_position, 3-disable_at_position, 4-blank_on_concealed)\n");
                                break;
                            case 'J': case 'j':
                                snprintf(wbuf, 1024, "Request SES defaults\nFormat: I J\nChange SES defaults\nFormat: I J (1-No Loop|0xFFFFFFFF-Infinite Loop) (0-maintenance|1-testing|2-record|3-lifefire|4-Error|5-Stop|6-record started|7-encoding started|8-recording/encoding finished|9-copying)\n");
                                break;
                            case 'K': case 'k':
                                snprintf(wbuf, 1024, "Request SMK defaults\nFormat: I K\nChange SMK defaults\nFormat: I K (0|1|2)active_soon_or_immediate (0|1|2|3)active_on_full_expose_or_partial_expose_or_during_partial (0|1|2)active_or_deactive_on_hit (0|1|2)active_or_deactive_on_kill (0-60000)milliseconds_on_time (0-60000)milliseconds_off_time (0-250)halfseconds_start_delay (0-250)halfseconds_repeat_delay (0-62|63)repeat_count_or_infinite ex1 ex2 ex3\n");
				                break;
			                case 'L': case 'l':
                        	    snprintf(wbuf, 1024, "Get/Set listen port number\nFormat: I L <lport>\n");		
				                break;
			                case 'M': case 'm':   
                        	    snprintf(wbuf, 1024, "Get/Set current mac address\nFormat: I M <mac>\n");	
                                break;	
                            case 'N': case 'n':
                                snprintf(wbuf, 1024, "Request MFS defaults\nFormat: I N\nChange MFS defaults\nFormat: I N (0|1|2)active_soon_or_immediate (0|1|2|3)active_on_full_expose_or_partial_expose_or_during_partial (0|1|2)active_or_deactive_on_hit (0|1|2)active_or_deactive_on_kill (0-60000)milliseconds_on_time (0-60000)milliseconds_off_time (0-250)halfseconds_start_delay (0-250)halfseconds_repeat_delay (0-62|63)repeat_count_or_infinite ex1 ex2 ex3 (0|1)mode single_or_burst\n");
				                break;
                            case 'O': case 'o':
                                snprintf(wbuf, 1024, "Get/Set Bob Style\nFormat: I O (0|1)fasit_bob_at_hit_or_ats_bob_at_kill\n");	
                                break;
                            case 'P': case 'p':
                                snprintf(wbuf, 1024, "Request PHI defaults\nFormat: I P\nChange PHI defaults\nFormat: I P (0|1|2)active_soon_or_immediate (0|1|2|3)active_on_full_expose_or_partial_expose_or_during_partial (0|1|2)active_or_deactive_on_hit (0|1|2)active_or_deactive_on_kill (0-60000)milliseconds_on_time (0-60000)milliseconds_off_time (0-250)halfseconds_start_delay (0-250)halfseconds_repeat_delay (0-62|63)repeat_count_or_infinite ex1 ex2 ex3\n");
				                break;
                            case 'Q': case 'q':
                                snprintf(wbuf, 1024, "Docking Station Defaults\nFormat I Q (0|1|2|3) left_right_nodockleft_nodockright\n");
                                break;
			                case 'R': case 'r':
                                snprintf(wbuf, 1024, "Reboot\nFormat: I R reboot\n");			
				                break;
                            case 'S': case 's':
                                snprintf(wbuf, 1024, "Request hit sensor type\nFormat: I S\nChange hit sensor type\nFormat: I S (0|1|2)mechanical_or_nchs_or_miles (0|1)invert_input_line\n");
                                break;
                            case 'T': case 't':
                                snprintf(wbuf, 1024, "Request THM defaults\nFormat: I T\nChange THM defaults\nFormat: I T (0|1|2)active_soon_or_immediate (0|1|2|3)active_on_full_expose_or_partial_expose_or_during_partial (0|1|2)active_or_deactive_on_hit (0|1|2)active_or_deactive_on_kill (0-60000)milliseconds_on_time (0-60000)milliseconds_off_time (0-250)halfseconds_start_delay (0-250)halfseconds_repeat_delay (0-62|63)repeat_count_or_infinite (#)number_of_thermal_devices ex2 ex3\n");
		                        break;
                            case 'U': case 'u':
                                snprintf(wbuf, 1024, "Request Radio Frequency\nFormat: I U\nChange Radio Frequency\nFormat: I U <xxx.xxx>\n");
                                break;
                            case 'V': case 'v':
                                snprintf(wbuf, 1024, "Request Radio Power Low\nFormat: I V\nChange Radio Power Low\nFormat: I V <power>\n");
                                break;
                            case 'W': case 'w':
                                snprintf(wbuf, 1024, "Request Radio Power High\nFormat: I W\nChange Radio Power High\nFormat: I W <power>\n");
                                break;
                            case 'X': case 'x':
                                snprintf(wbuf, 1024, "Request Serial Number\nFormat: I X\nChange Serial Number\nFormat: I X <xxxxx-x-x>\n");
                                break;
                            case 'Y': case 'y':
                                snprintf(wbuf, 1024, "Request MSD defaults\nFormat: I Y\nChange MSD defaults\nFormat: I Y (0|1|2)active_soon_or_immediate (0|1|2|3)active_on_full_expose_or_partial_expose_or_during_partial (0|1|2)active_or_deactive_on_hit (0|1|2)active_or_deactive_on_kill (0-60000)milliseconds_on_time (0-60000)milliseconds_off_time (0-250)halfseconds_start_delay (0-250)halfseconds_repeat_delay (0-62|63)repeat_count_or_infinite (1-330)playerID (00-36)code (00-FF)ammo\n");
			                    break;
                            case 'Z': case 'z':
                                snprintf(wbuf, 1024, "Home End Defaults\nFormat I Z (0|1) left_right\n");
                                break;
                            default:
                                snprintf(wbuf, 1024, "I A: Address\nI B: Board Type\nI C: Connect Port Number\nI D: Comm Type\nI E: Battery/Mover Defaults\nI F: Fall Parameters Defaults\nI G: MGL Defaults\nI H: Hit Calibration Defaults\nI I: IP Address\nI J: SES defaults\nI K: SMK Defaults\nI L: Listen Port Number\nI M: MAC Address\nI N: MFS Defaults\nI O: Bob type\nI P: PHI defaults\nI Q: Docking Station Default\nI R: Reboot\nI S: Hit Sensor Defaults\nI T: THM Defaults\nI U: Radio Frequency\nI V: Radio Power Low\nI W: Radio Power High\nI X: Serial Number\nI Y: MSD Defaults\nI Z: Mover Home End Defaults\n");
                                break;
				        }
                        break; 
                    case 'J': case 'j':
                        switch (cmd[arg2]) { /* second letter */
                            case 'A': case 'a':
                                snprintf(wbuf, 1024, "Get/Set Full Flash Version\nFormat: J A <flash version>\n");
                                break;
                            case 'B': case 'b':
                                snprintf(wbuf, 1024, "Get/Set Partial Flash Version\nFormat: J B <flash version>\n");
                                break;
                            case 'C': case 'c':
                                snprintf(wbuf, 1024, "Get/Set Will the radio reprogram?\nFormat: J C (Y|N)\n");
                                break;
                            default:
                                snprintf(wbuf, 1024, "J A: Full Flash Version\nJ B: Partial Flash Version\nJ C: Program The Radio?\n");
                                break;
                        }
                        break;
                    case 'K': case 'k':
                        snprintf(wbuf, 1024, "Shutdown device\nFormat: K\n");
                        break;
                    case 'L': case 'l':
                        snprintf(wbuf, 1024, "Request hit calibration parameters\nFormat: L\nChange hit calibration parameters\nFormat: L (1-10000)milliseconds_between_hits (1-1000)hit_desensitivity (0-1024)tenthseconds_blanking_time_from_start_expose (0-4)enable_on_value (0-blank_always, 1-enable_always, 2-enable_at_position, 3-disable_at_position, 4-blank_on_concealed)\n");
                        break;
                    case 'M': case 'm':
                        snprintf(wbuf, 1024, "Movement speed request\nFormat: M M\nStop movement\nFormat: M\nChange speed\nFormat M (-32767 to 32766)speed_in_mph\n");
                        break;
                    case 'O': case 'o':
                        snprintf(wbuf, 1024, "Change Mode\nFormat: O (0-3)mode_number\nMode request\nFormat: O\n");
                        break;
                    case 'P': case 'p':
                        snprintf(wbuf, 1024, "Sleep command\nFormat: P (0|1|2|3)sleep_or_wake_or_request_or_dock\n");
                        break;
                    case 'Q': case 'q':
                        snprintf(wbuf, 1024, "Types of accessories: MFS, MGL, SES, PHI, MSD, SMK, THM\nRequest accessory paramaters\nFormat: Q accessory_type\nChange accessory parameters\nFormat: Q accessory_type (0|1|2)active_soon_or_immediate (0|1|2|3)active_on_full_expose_or_partial_expose_or_during_partial (0|1|2)active_or_deactive_on_hit (0|1|2)active_or_deactive_on_kill (0-60000)milliseconds_on_time (0-60000)milliseconds_off_time (0-250)halfseconds_start_delay (0-250)halfseconds_repeat_delay (0-62|63)repeat_count_or_infinite ex1 ex2 ex3\n");
                        break;
                    case 'R': case 'r':
                        snprintf(wbuf, 1024, "Go Home used for Scenario Commands: 0-Pause, 1-Abort, 2-Restart\n");
                        break;
                    case 'S': case 's':
                        snprintf(wbuf, 1024, "Request exposure data\nFormat: S\n");
                        break;
                    case 'T': case 't':
                        snprintf(wbuf, 1024, "Toggle target\nFormat: T\n");
                        break;
                    case 'V': case 'v':
                        snprintf(wbuf, 1024, "Send event to kernel\nFormat: V (0-255)event\nCurrently defined events 0-11:\n0: Start of raise\n1: Finished raising\n2: Start of lower\n3: Finished lowering\n4: Start of move\n5: Reached target speed\n6: Changed position\n7: Started coast\n8: Started stopping\n9: Finished stopping\n10: Hit\n11: Kill\n12: Shutdown\n13: Dock\n14: Undocked\n15: Sleep\n:16: Wake\n17: Home limit\n18: End limit\n19: Dock Limit\n20: Timed out\n21: Mover changed speed\n22: Charging\n23: Not charging\n24: Enable Battery Check\n25: Disable Battery Check\n26: Error\nUnknown\n");
                        break;
                    case 'X': case 'x':
                        snprintf(wbuf, 1024, "Emergency stop\nFormat: X\n");
                        break;
                    case 'Y': case 'y':
                        snprintf(wbuf, 1024, "Request hit sensor type\nFormat: Y\nChange hit sensor type\nFormat: Y (0|1|2)mechanical_or_nchs_or_miles (0|1)invert_input_line\n");
                        break;
                    case 'Z': case 'z':
                        snprintf(wbuf, 1024, "Request knob value\nFormat: Z\n");
                        break;
                    default: // print default help
                        snprintf(wbuf, 1024, "A: Position\nB: Battery\nC: Conceal\nD: Hit Data\nE: Expose\nF: Fall\nG: GPS\nH: HITS\nI: Eeprom\nJ: Eeprom Cont.\nK: Shutdown\nL: Hit Calibration\nM: Movement\nO: Mode\nP: Sleep\nQ: Accessory\nS: Exposure Status\nT: Toggle\nU: Faults\nV: Event\nX: Emergency Stop\nY: Hit Sensor Type\nZ: Knob\n");
                        break;
                }
                write(client, wbuf, strnlen(wbuf,1024));
                break;
            case '\0':
                // empty string, just ignore
                break;
            default:
//printf("unrecognized command '%c'\n", cmd[0]);
                break;
        }

        if (nl_cmd != NL_C_UNSPEC) {
            // Construct a generic netlink by allocating a new message
            struct nl_msg *msg;
            struct hit_calibration hit_c;
            struct accessory_conf acc_c;
            struct gps_conf gps_c;
            struct bit_event bit_c;
            msg = nlmsg_alloc();
            genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, nl_cmd, 1);

            // fill in attribute according to message sent
            switch (nl_cmd) {
                case NL_C_BATTERY:
                    // request battery message
                    if (cmd[0] == 'K' || cmd[0] == 'k') {
                       nla_put_u8(msg, GEN_INT8_A_MSG, BATTERY_SHUTDOWN);
                    } else {
                       nla_put_u8(msg, GEN_INT8_A_MSG, BATTERY_REQUEST);
                    }
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
					//printf("Converting velocity to a float.\n");
					     ;float farg1;
                    if (cmd[1] == '\0') {
                        nla_put_u16(msg, GEN_INT16_A_MSG, VELOCITY_REQ); // velocity request
                        break;
                    }
                    int i = 1;
                    if (cmd[1] == 'C' || cmd[1] == 'c') {
                        nl_cmd = NL_C_CONTINUOUS;
                        nlmsg_free(msg);
                        msg = nlmsg_alloc();
                        genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, nl_cmd, 1);
                        i ++;
                        snprintf(wbuf, 1024, "Continuous Move Request [%s]\n", cmd+i);
                        write(client, wbuf, strnlen(wbuf,1024));
                    } else if (cmd[1] == 'A' || cmd[1] == 'a') {
                        nl_cmd = NL_C_MOVEAWAY;
                        nlmsg_free(msg);
                        msg = nlmsg_alloc();
                        genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, nl_cmd, 1);
                        i ++;
                        snprintf(wbuf, 1024, "Move AWAY Request [%s]\n", cmd+i);
                        write(client, wbuf, strnlen(wbuf,1024));
                    }
                    if (sscanf(cmd+i, "%f", &farg1) == 1) {
						farg1 = farg1*10;	// muliplied to work with floats in target_mover_generic
                        if (farg1 > 32766 || farg1 < -32767) {
                            farg1 = 0; // stay away from the edge conditions
                        }
                        nla_put_u16(msg, GEN_INT16_A_MSG, 32768+farg1); // move request (add 32768 as we're not signed)
                    } else {
                        nla_put_u16(msg, GEN_INT16_A_MSG, VELOCITY_STOP); // stop
                    }
                    break;
                case NL_C_BIT:
                    if (cmd[0] == 'O' || cmd[0] == 'o') {
                        bit_c.is_on = 0;
                        if (sscanf(cmd+1, "%i", &arg1) == 1) {
                            bit_c.bit_type = BIT_MODE; // change mode
                            bit_c.is_on = arg1; // mode value
                        } else {
                            bit_c.bit_type = BIT_MODE_REQ; // mode value request
                        }
                    } else {
                        bit_c.bit_type = BIT_KNOB_REQ; // knob value request
                    }
                    nla_put(msg, BIT_A_MSG, sizeof(struct bit_event), &bit_c);
                    break;
                case NL_C_SLEEP:
                    if (sscanf(cmd+1, "%i", &arg1) != 2) {
                        nla_put_u8(msg, GEN_INT8_A_MSG, arg1); // sleep command
                    } else {
                        nla_put_u8(msg, GEN_INT8_A_MSG, SLEEP_REQUEST); // sleep status request
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
                        if (arg1 >= 0 && arg1 < HIT_REQ) {
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
                                if (sscanf(cmd+1, "%i %i %i %i", &arg1, &arg2, &arg3, &arg4) == 4) {
                                    hit_c.seperation = arg1;
                                    hit_c.sensitivity = arg2;
                                    hit_c.blank_time = arg3;
                                    hit_c.enable_on = arg4;
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
                                    hit_c.invert = arg2;
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
                                    // set hits_to_kill and after_kill values message
                                    hit_c.hits_to_kill = arg1;
                                    hit_c.after_kill = arg2;
                                    hit_c.set = HIT_OVERWRITE_KILL;
                                }
                            } else {
                                // get hits_to_kill value request
                                hit_c.set = HIT_GET_KILL;
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
                                           acc_c.acc_type = ACC_NES_MGL;
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
                case NL_C_GOHOME:
                    nla_put_u8(msg, GEN_INT8_A_MSG, 1); // create event X
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

static struct nl_handle *handlecreate_nl_handle(int client, int group) {
    // Allocate a new netlink handle
    struct epoll_event ev;
    struct nl_handle *handle = nl_handle_alloc();

    // join ATI group (for multicast messages)
    nl_join_groups(handle, group);

    // Connect to generic netlink handle on kernel side
    genl_connect(handle);

    // add netlink socket to epoll
    nl_fd = nl_socket_get_fd(handle); // netlink socket file descriptor
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = nl_fd;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, nl_fd, &ev) < 0) {
        fprintf(stderr, "epoll insertion error: nl_fd\n");
        return NULL;
    }

    // Ask kernel to resolve family name to family id
    family = genl_ctrl_resolve(handle, "ATI");

    // Prepare handle to receive the answer by specifying the callback
    // function to be called for valid messages.
    nl_socket_modify_cb(handle, NL_CB_VALID, NL_CB_CUSTOM, parse_cb, (void*)client); // pass client file descriptor
    nl_socket_modify_cb(handle, NL_CB_FINISH, NL_CB_CUSTOM, ignore_cb, (void*)"FINISH");
    nl_socket_modify_cb(handle, NL_CB_OVERRUN, NL_CB_CUSTOM, ignore_cb, (void*)"OVERRUN");
    nl_socket_modify_cb(handle, NL_CB_SKIPPED, NL_CB_CUSTOM, ignore_cb, (void*)"SKIPPED");
    nl_socket_modify_cb(handle, NL_CB_ACK, NL_CB_CUSTOM, ignore_cb, (void*)"ACK");
    nl_socket_modify_cb(handle, NL_CB_MSG_IN, NL_CB_CUSTOM, ignore_cb, (void*)"MSG_IN");
    nl_socket_modify_cb(handle, NL_CB_MSG_OUT, NL_CB_CUSTOM, ignore_cb, (void*)"MSG_OUT");
    nl_socket_modify_cb(handle, NL_CB_INVALID, NL_CB_CUSTOM, ignore_cb, (void*)"INVALID");
    nl_socket_modify_cb(handle, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, ignore_cb, (void*)"SEQ_CHECK");
    nl_socket_modify_cb(handle, NL_CB_SEND_ACK, NL_CB_CUSTOM, ignore_cb, (void*)"SEND_ACK");

    return handle;
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
        signal(SIGCHLD, stopMover);
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
                        memset(client_buf, '\0', CLIENT_BUFFER);
                    }
                }
            }
        }
    }
    // the client is done
    close(client); // close socket
    free(client_buf); // free buffer

    // stop mover
    stopMover();

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



