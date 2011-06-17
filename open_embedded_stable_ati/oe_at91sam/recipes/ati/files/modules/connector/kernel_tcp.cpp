#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

using namespace std;

#include "kernel_tcp.h"
#include "common.h"
#include "timers.h"

#define START 0xABCD0123
#define END 0x0123ABCD

/***********************************************************
*                     Kernel_TCP Class                     *
***********************************************************/
Kernel_TCP::Kernel_TCP(int fd) : Connection(fd) {
FUNCTION_START("::Kernel_TCP(int fd) : Connection(fd)")
   // connect our netlink connection
   kern_conn = NL_Conn::newConn<Kern_Conn>(this);
   if (kern_conn == NULL) {
      deleteLater();
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
   }
FUNCTION_END("::Kernel_TCP(int fd, int tnum) : Connection(fd)")
}

Kernel_TCP::~Kernel_TCP() {
FUNCTION_START("::~Kernel_TCP()")

FUNCTION_END("::~Kernel_TCP()")
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
   while (size >= sizeof(kern_event_t)) {
      // map a trial event structure to the receive buffer
      kern_event_t *trial = (kern_event_t*)buf;

      // check the start and end values
      if (trial->start == START && trial->end == END) {
         // send to kernel
         kern_conn->incomingEvent(trial);

         // move on to next object
         buf += sizeof(kern_event_t);
         size -= sizeof(kern_event_t);
      } else {
         // move to next character in receive buffer
         buf++;
         size--;
      }
   }
FUNCTION_INT("::parseData(int size, char *buf)", 0)
   return 0;
}

// send an event over tcp
void Kernel_TCP::outgoingEvent(kern_event_t *event) {
FUNCTION_START("::outgoingEvent(kern_event_t *event)")

   // just queue the message for sending
   queueMsg(event, sizeof(kern_event_t));

FUNCTION_END("::outgoingEvent(kern_event_t *event)")
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
               kern_event_t event;
               event.start = START;
               event.event = EVENT_SHUTDOWN;
               event.end = END;
               kern_tcp->outgoingEvent(&event);
            }
         }
         break;
      case NL_C_EVENT:
         genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);

         // handle event from kernel
         if (attrs[GEN_INT8_A_MSG]) {
            u8 data = nla_get_u8(attrs[GEN_INT8_A_MSG]);
            // create an event structure and pass to tcp handler for transmission
            kern_event_t event;
            event.start = START;
            event.event = (GO_event_t)data;
            event.end = END;
            kern_tcp->outgoingEvent(&event);
         }
         break;
      default:
         DMSG("Ignoring NL command %i\n", ghdr->cmd);
         break;
   }
 
FUNCTION_INT("::parseData(struct nl_msg *msg)", 0)
   return 0;
}

// send an event to the kernel
void Kern_Conn::incomingEvent(kern_event_t *event) {
FUNCTION_START("::incomingEvent(kern_event_t *event)")

   // just queue the event for sending
   queueMsgU8(NL_C_EVENT, event->event);

FUNCTION_END("::incomingEvent(kern_event_t *event)")
}
