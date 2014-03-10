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


main(int argc, char ** argv){
   int nl_cmd;
   int family;
   struct nl_handle *g_handle;
   struct nl_msg *msg;
   msg = nlmsg_alloc();

    g_handle = nl_handle_alloc();


    // Connect to generic netlink handle on kernel side
    genl_connect(g_handle);


    // Ask kernel to resolve family name to family id
    family = genl_ctrl_resolve(g_handle, "ATI");



   nl_cmd = NL_C_EXPOSE;
   genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, nl_cmd, 1);

   nla_put_u8(msg, GEN_INT8_A_MSG, EXPOSE); // expose command
            
   // Send message over netlink handle
   nl_send_auto_complete(g_handle, msg);

   // Free message
   nlmsg_free(msg);

   nl_handle_destroy(g_handle);
}
