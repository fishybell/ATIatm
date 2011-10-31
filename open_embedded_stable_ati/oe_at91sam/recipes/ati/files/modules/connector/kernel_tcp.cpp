#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

using namespace std;

#include "kernel_tcp.h"
#include "common.h"
#include "timers.h"

// start/end magic for generic output event message
#define GO_START 0xABCD0123
#define GO_END 0x0123ABCD

// start/end magic for command event message
#define CMD_START 0xFEDC9876
#define CMD_END 0x6789CDEF

/***********************************************************
*                     Kernel_TCP Class                     *
***********************************************************/
Kernel_TCP::Kernel_TCP(int fd) : Connection(fd) {
FUNCTION_START("::Kernel_TCP(int fd) : Connection(fd)")
   // connect our netlink connection
   kern_conn = NL_Conn::newConn<Kern_Conn>(this);
   if (kern_conn == NULL) {
      deleteLater();
   } else {
      // send my role to the other side
      sendRole();
   }
FUNCTION_END("::Kernel_TCP(int fd) : Connection(fd)")
}

Kernel_TCP::Kernel_TCP(int fd, int tnum) : Connection(fd) {
FUNCTION_START("::Kernel_TCP(int fd, int tnum) : Connection(fd)")
   setTnum(tnum);

   // connect our netlink connection
   kern_conn = NL_Conn::newConn<Kern_Conn>(this);
   if (kern_conn == NULL) {
      deleteLater();
   } else {
      // send my role to the other side
      sendRole();
   }
FUNCTION_END("::Kernel_TCP(int fd, int tnum) : Connection(fd)")
}

Kernel_TCP::~Kernel_TCP() {
FUNCTION_START("::~Kernel_TCP()")

FUNCTION_END("::~Kernel_TCP()")
}

void Kernel_TCP::sendRole() {
FUNCTION_START("::sendRole()")

   // create "I am this role" message
   kern_cmd_event_t msg;
   memset(&msg, 0, sizeof(kern_cmd_event_t)); // everything 0...
   msg.event.role = KERN_ROLE;                // ... except role

   // send
   queueMsg(&msg, sizeof(kern_cmd_event_t));

FUNCTION_END("::sendRole()")
}

int Kernel_TCP::parseData(int size, const char *buf) {
FUNCTION_START("::parseData(int size, char *buf)")
   IMSG("TCP %i read %i bytes of data\n", fd, size)

   // check kernel pair
   if (!hasPair()) {
FUNCTION_INT("::parseData(int size, char *buf)", 0)
      return 0;
   }

   // check for a valid message
   while (size >= min(sizeof(kern_go_event_t), sizeof(kern_cmd_event_t))) { // msg at least as big as smallest event structure
      // map a trial generic output event structure to the receive buffer
      kern_go_event_t *go_trial = (kern_go_event_t*)buf;

      // map a trial command event structure to the receive buffer
      kern_cmd_event_t *cmd_trial = (kern_cmd_event_t*)buf;

      // check the start and end values
      if (size >= sizeof(kern_go_event_t) && go_trial->start == GO_START && go_trial->end == GO_END) { // message a valid go event?
         // send to kernel
         kern_conn->incomingGOEvent(go_trial);

         // move on to next object
         buf += sizeof(kern_go_event_t);
         size -= sizeof(kern_go_event_t);
      } else if (size >= sizeof(kern_cmd_event_t) && go_trial->start == CMD_START && go_trial->end == CMD_END) { // message a valid cmd event?
         // check to see if we're just getting the other side's kernel role
         if (cmd_trial->event.cmd == 0 && cmd_trial->event.payload_size == 0 && cmd_trial->event.attribute == 0) {
            // remember role
            role = cmd_trial->event.role;
         } else {
            // send to kernel
            kern_conn->incomingCmdEvent(cmd_trial);
         }

         // move on to next object
         buf += sizeof(kern_cmd_event_t);
         size -= sizeof(kern_cmd_event_t);
      } else {
         // move to next character in receive buffer
         buf++;
         size--;
      }
   }
FUNCTION_INT("::parseData(int size, char *buf)", 0)
   return 0;
}

// send a generic output event over tcp
void Kernel_TCP::outgoingGOEvent(kern_go_event_t *event) {
FUNCTION_START("::outgoingGOEvent(kern_go_event_t *event)")

   // just queue the message for sending
   queueMsg(event, sizeof(kern_go_event_t));

FUNCTION_END("::outgoingGOEvent(kern_go_event_t *event)")
}

// send a command event over tcp
void Kernel_TCP::outgoingCmdEvent(kern_cmd_event_t *event) {
FUNCTION_START("::outgoingCmdEvent(kern_cmd_event_t *event)")

   // check to see if we're attached to the correct role
   if (event->event.role == role) {
      // ...we are, queue the message for sending
      queueMsg(event, sizeof(kern_cmd_event_t));
   }

FUNCTION_END("::outgoingCmdEvent(kern_cmd_event_t *event)")
}

/***********************************************************
*                     Kern_Conn Class                      *
***********************************************************/
Kern_Conn::Kern_Conn(struct nl_handle *handle, Kernel_TCP *client, int family) : NL_Conn(handle, client, family) {
FUNCTION_START("::Kern_Conn(struct nl_handle *handle, TCP_Client *client, int family) : NL_Conn(handle, client, family)")

   kern_tcp = client;

FUNCTION_END("::Kern_Conn(struct nl_handle *handle, TCP_Client *client, int family) : NL_Conn(handle, client, family)")
}

Kern_Conn::~Kern_Conn() {
FUNCTION_START("::~Kern_Conn()")

FUNCTION_END("::~Kern_Conn()")
}

int Kern_Conn::parseData(struct nl_msg *msg) {
FUNCTION_START("::parseData(struct nl_msg *msg)")
   struct nlattr *attrs[NL_A_MAX+1];
   struct nlmsghdr *nlh = nlmsg_hdr(msg);
   struct genlmsghdr *ghdr = static_cast<genlmsghdr*>(nlmsg_data(nlh));
   struct nlattr *na;

   // parse message and call individual commands as needed
   switch (ghdr->cmd) {
      case NL_C_FAILURE:
         genlmsg_parse(nlh, 0, attrs, GEN_STRING_A_MAX, generic_string_policy);

         // TODO -- failure messages need decodable data
         if (attrs[GEN_STRING_A_MSG]) {
            char *data = nla_get_string(attrs[GEN_STRING_A_MSG]);
            IERROR("netlink failure attribute: %s\n", data)
         }

         break;
      case NL_C_BATTERY:
         genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

         // handle event from kernel
         if (attrs[GEN_INT8_A_MSG]) {
            u8 data = nla_get_u8(attrs[GEN_INT8_A_MSG]);
            if (data == BATTERY_SHUTDOWN) {
               // send SHUTDOWN message back down to kernel
               queueMsgU8(NL_C_BATTERY, BATTERY_SHUTDOWN); // shutdown command

               // send SHUTDOWN message on to other devices
               // create an event structure and pass to tcp handler for transmission
               kern_go_event_t event;
               event.start = GO_START;
               event.event = EVENT_SHUTDOWN;
               event.end = GO_END;
               kern_tcp->outgoingGOEvent(&event);
            }
         }
         break;
      case NL_C_EVENT:
         genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

         // handle generic output event from kernel
         if (attrs[GEN_INT8_A_MSG]) {
            // grab data
            u8 data = nla_get_u8(attrs[GEN_INT8_A_MSG]);
            // reflect back to kernel
            queueMsgU8(NL_C_EVENT_REF, data);
            // create an event structure and pass to tcp handler for transmission
            kern_go_event_t event;
            event.start = GO_START;
            event.event = (GO_event_t)data;
            event.end = GO_END;
            kern_tcp->outgoingGOEvent(&event);
         }
         break;
      case NL_C_CMD_EVENT:
         genlmsg_parse(nlh, 0, attrs, CMD_EVENT_A_MAX, cmd_event_policy);

         // handle command event from kernel
         na = attrs[CMD_EVENT_A_MSG];
         if (na) {
            cmd_event_t *data = (cmd_event_t*)nla_data(na);
            // create an event structure and pass to tcp handler for transmission
            kern_cmd_event_t event;
            event.start = CMD_START;
            event.event = *data; // copy data
            event.end = CMD_END;
            DCMSG(RED, "Received Command Event: %i %i %i %i", data->cmd, data->attribute, data->payload_size, data->role);
            for (int i=0; i<data->payload_size; i++) {
               DCMSG(RED, "Byte %i : %02X", i, data->payload[i]);
            }
            // handle with the appropriate role handler
            if (data->role == KERN_ROLE) { // is this kernel the right role?
               incomingCmdEvent(&event);
            } else { // maybe the tcp connection has the right role...
               kern_tcp->outgoingCmdEvent(&event);
            }
         }
         break;
      default:
         // reflect all other messages (only care about replies though) back to kernel
         genlmsg_parse(nlh, 0, attrs, 1, nl_attr_sizes[ghdr->cmd].policy); // use 1st attribute only
         na = attrs[1]; // use 1st attribute only
         if (na) {
            cmd_event_t msg; // encapsulate in command event
            memset(&msg, 0, sizeof(cmd_event_t)); // everything 0...
            msg.role = KERN_ROLE;
            msg.cmd = ghdr->cmd;
            int s = nl_attr_sizes[ghdr->cmd].size;
            if (s == -1) {
               s = nla_len(na);
            }
            msg.payload_size = min(16, s); // maximum of 16 for size
            msg.attribute = 1;
            memcpy(msg.payload, nla_data(na), msg.payload_size);
            outgoingCmdEvent(&msg);
         }
         break;
   }
 
FUNCTION_INT("::parseData(struct nl_msg *msg)", 0)
   return 0;
}

// send a generic output event to the kernel
void Kern_Conn::incomingGOEvent(kern_go_event_t *event) {
FUNCTION_START("::incomingGOEvent(kern_go_event_t *event)")

   // just queue the event for sending
   queueMsgU8(NL_C_EVENT, event->event);

FUNCTION_END("::incomingGOEvent(kern_go_event_t *event)")
}

// send a command event to the kernel
void Kern_Conn::incomingCmdEvent(kern_cmd_event_t *event) {
FUNCTION_START("::incomingCmdEvent(kern_cmd_event_t *event)")

    // queue message to kernel
    queueMsg(event->event.cmd, event->event.attribute, event->event.payload_size, event->event.payload); // pass structure without changing

FUNCTION_END("::incomingCmdEvent(kern_cmd_event_t *event)")
}

// send a command event to the kernel
void Kern_Conn::outgoingCmdEvent(cmd_event_t *event) {
FUNCTION_START("::outgoingCmdEvent(kern_cmd_event_t *event)")

    // queue message to kernel
    queueMsg(NL_C_CMD_EVENT, CMD_EVENT_A_MSG, sizeof(cmd_event_t), event);

FUNCTION_END("::outgoingCmdEvent(kern_cmd_event_t *event)")
}

