#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

using namespace std;

#include "kernel_tcp.h"
#include "common.h"
#include "timers.h"


/***********************************************************
*                     Kernel_TCP Class                     *
***********************************************************/
Kernel_TCP::Kernel_TCP(int fd) : Connection(fd) {
FUNCTION_START("::Kernel_TCP(int fd) : Connection(fd)")
   // connect our netlink connection
   nl_conn = NL_Conn::newConn<Kern_Conn>(this);
   if (nl_conn == NULL) {
      deleteLater();
   }
FUNCTION_END("::Kernel_TCP(int fd) : Connection(fd)")
}

Kernel_TCP::~Kernel_TCP() {
FUNCTION_START("::~Kernel_TCP()")

FUNCTION_END("::~Kernel_TCP()")
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
   }
 
FUNCTION_INT("::parseData(struct nl_msg *msg)", 0)
   return 0;
}

