#include <errno.h>

using namespace std;

#include "nl_conn.h"
#include "tcp_client.h"
#include "timers.h"

// for explicit declarations of template function
#ifdef FASIT_CONN
   #include "sit_client.h"
   #include "mit_client.h"
   #include "ses_client.h"
#endif

#ifdef EVENT_CONN
   #include "kernel_tcp.h"
#endif

NL_Conn::NL_Conn(struct nl_handle *handle, Connection *client, int family) : Connection(nl_socket_get_fd(handle)) {
FUNCTION_START("::NL_Conn(struct nl_handle *handle, Connection *client)");

   this->handle = handle;
   this->client = client;
   this->family = family;

    // Prepare handle to receive the answer by specifying the callback
    // function to be called for valid messages.
    nl_socket_modify_cb(this->handle, NL_CB_VALID, NL_CB_CUSTOM, parse_cb, (void*)this); // the arg pointer is our netlink handle
    nl_socket_modify_cb(this->handle, NL_CB_FINISH, NL_CB_CUSTOM, ignore_cb, (void*)"FINISH");
    nl_socket_modify_cb(this->handle, NL_CB_OVERRUN, NL_CB_CUSTOM, ignore_cb, (void*)"OVERRUN");
    nl_socket_modify_cb(this->handle, NL_CB_SKIPPED, NL_CB_CUSTOM, ignore_cb, (void*)"SKIPPED");
    nl_socket_modify_cb(this->handle, NL_CB_ACK, NL_CB_CUSTOM, ignore_cb, (void*)"ACK");
    nl_socket_modify_cb(this->handle, NL_CB_MSG_IN, NL_CB_CUSTOM, ignore_cb, (void*)"MSG_IN");
    nl_socket_modify_cb(this->handle, NL_CB_MSG_OUT, NL_CB_CUSTOM, ignore_cb, (void*)"MSG_OUT");
    nl_socket_modify_cb(this->handle, NL_CB_INVALID, NL_CB_CUSTOM, ignore_cb, (void*)"INVALID");
    nl_socket_modify_cb(this->handle, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, ignore_cb, (void*)"SEQ_CHECK");
    nl_socket_modify_cb(this->handle, NL_CB_SEND_ACK, NL_CB_CUSTOM, ignore_cb, (void*)"SEND_ACK");

FUNCTION_END("::NL_Conn(struct nl_handle *handle, Connection *client)");
}

NL_Conn::~NL_Conn() {
FUNCTION_START("::~NL_Conn()");

   // kill netlink connection
   if (handle) {
      nl_handle_destroy(handle);
   }
   handle = NULL;

   // make sure the client is dead as well
   if (client) {
      client->deleteLater();
   }
   client = NULL;

   // clean out queue
   list<struct nl_msg*>::iterator it;
   for (it = outq.begin(); it != outq.end(); it++) {
      // free non-sent messages
      nlmsg_free(*it);
   }

FUNCTION_END("::~NL_Conn()");
}

// the file descriptor is ready to give us data, read as much as possible (max of BUF_SIZE)
int NL_Conn::handleRead(const epoll_event *ev) {
FUNCTION_START("NL_Conn::handleRead(const epoll_event *ev)");

   // call the callback functions for this message now (if full message received)
   nl_recvmsgs_default(handle);

FUNCTION_INT("NL_Conn::handleRead(const epoll_event *ev)", 0);
   return 0;
}

// the file descriptor is ready to receive the data, send it on through
int NL_Conn::handleWrite(const epoll_event *ev) {
FUNCTION_START("NL_Conn::handleWrite(const epoll_event *ev)");

   // are we ready for message sending?
   if (handle == NULL || client == NULL) {
      makeWritable(false);
FUNCTION_INT("NL_Conn::handleWrite(const epoll_event *ev)", 0);
      return 0;
   }

   // do we have anything to write?
   if (outq.empty()) {
      makeWritable(false);
FUNCTION_INT("NL_Conn::handleWrite(const epoll_event *ev)", 0);
      return 0;
   }

   DCMSG( MAGENTA,"NL_Conn::handleWrite    is sending a NL message\n");
   // only send the first message in the queue
   struct nl_msg *msg = outq.front();
   int retval = nl_send_auto_complete(handle, msg); // send the message over the netlink handle

   // if we couldn't send because of 
   if (retval == EAGAIN || retval == EWOULDBLOCK) {
      // we're apparently busy, try again later
      new NLTimer(this, RETRYNETLINK);
      makeWritable(false); // hold off until the timer times out
      
      // leave at front of queue
FUNCTION_INT("NL_Conn::handleWrite(const epoll_event *ev)", 0);
      return 0;
   } else if (retval < 0) {
      // our netlink connection is broken, kill it
FUNCTION_INT("NL_Conn::handleWrite(const epoll_event *ev)", -1);
      return -1;
   }

   // free the message
   nlmsg_free(msg);

   // delete the front queue item
   outq.pop_front();

FUNCTION_INT("NL_Conn::handleWrite(const epoll_event *ev)", 0);
   return 0;
}

// add this message to the write buffer
// as this function does not actually write and will not cause the caller function to be
//   preempted, the caller may call this function multiple times to create a complete
//   message and be sure that the entire message is sent
void NL_Conn::queueMsg(struct nl_msg *msg) {
FUNCTION_START("NL_Conn::queueMsg(struct nl_msg *msg)");
   // don't queue a non message
   if (msg == NULL) {
FUNCTION_END("NL_Conn::queueMsg(struct nl_msg *msg)");
      return;
   }

   // push to the back of the queue
   outq.push_back(msg);

   // send the message later
   makeWritable(true);

FUNCTION_END("NL_Conn::queueMsg(struct nl_msg *msg)");
}

// helper for queueing message with arbitrary attribute
void NL_Conn::queueMsg(int cmd, int att_c, size_t att_t, void *attr) {
FUNCTION_START("NL_Conn::queueMsg(int cmd, int att_c, size_t att_t, void *attr)");

   // Create a new netlink message
   struct nl_msg *msg;
   msg = nlmsg_alloc();
   genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, cmd, 1);

   // add attribute
   nla_put(msg, att_c, att_t, attr);

   // queue message for sending later
   queueMsg(msg);

FUNCTION_END("NL_Conn::queueMsg(int cmd, int att_c, size_t att_t, void *attr)");
}

// helper for queueing message with u8 attribute 
void NL_Conn::queueMsgU8(int cmd, int attr) {
FUNCTION_START("NL_Conn::queueMsgU8(int cmd, int attr)");

   // Create a new netlink message
   struct nl_msg *msg;
   msg = nlmsg_alloc();
   genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, cmd, 1);

   // add attribute
   nla_put_u8(msg, GEN_INT8_A_MSG, attr);

   // queue message for sending later
   queueMsg(msg);

FUNCTION_END("NL_Conn::queueMsgU8(int cmd, int attr)");
}

// helper for queueing message with u16 attribute 
void NL_Conn::queueMsgU16(int cmd, int attr) {
FUNCTION_START("NL_Conn::queueMsgU16(int cmd, int attr)");
   // Create a new netlink message
   struct nl_msg *msg;
   msg = nlmsg_alloc();
   genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO, cmd, 1);

   // add attribute
   nla_put_u16(msg, GEN_INT16_A_MSG, attr);

   // queue message for sending later
   queueMsg(msg);
   DCMSG(GREEN, "queueMsgU16 attr: %i, msg: %s", attr, msg);

FUNCTION_END("NL_Conn::queueMsgU16(int cmd, int attr)");
}


template <class C_Conn, class C_Client>
C_Conn *NL_Conn::newConn(C_Client *client) {
FUNCTION_START("::newConn(C_Client *client)");
   C_Conn *conn = NULL;
   // allocate a netlink handle
   struct nl_handle *handle = nl_handle_alloc();
   if (handle == NULL) {
FUNCTION_HEX("::newConn(C_Client *client)", NULL);
      return NULL;
   }

   // set handle to join ATI group (for multicast messages)
   nl_join_groups(handle, ATI_GROUP);

   // Connect to generic netlink handle on kernel side
   if (genl_connect(handle) < 0) {
      nl_handle_destroy(handle);
FUNCTION_HEX("::newConn(C_Client *client)", NULL);
      return NULL;
   }
   setnonblocking(nl_socket_get_fd(handle), true); // socket

   // resolve ATI family name to family id
   int family = genl_ctrl_resolve(handle, "ATI");
   if (family < 0) {
      nl_handle_destroy(handle);
FUNCTION_HEX("::newConn(C_Client *client)", NULL);
      return NULL;
   }
   DMSG("Resolved ATI netlink family id as %i\n", family);

   // Create connection
   conn = new C_Conn(handle, client, family);

   // add to epoll
   if (!addToEPoll(nl_socket_get_fd(handle), conn)) {
       delete conn;
FUNCTION_HEX("::newConn(C_Client *client)", NULL);
       return NULL;
   }

   // return the result
FUNCTION_HEX("::newConn(C_Client *client)", conn);
   return conn;
}

// explicit declarations of newConn() template function
#ifdef FASIT_CONN
   template SIT_Conn *NL_Conn::newConn<SIT_Conn, SIT_Client>(SIT_Client *client);
   template MIT_Conn *NL_Conn::newConn<MIT_Conn, MIT_Client>(MIT_Client *client);
   template SES_Conn *NL_Conn::newConn<SES_Conn, SES_Client>(SES_Client *client);
#endif

#ifdef EVENT_CONN
   template Kern_Conn *NL_Conn::newConn<Kern_Conn, Kernel_TCP>(Kernel_TCP *client);
#endif

