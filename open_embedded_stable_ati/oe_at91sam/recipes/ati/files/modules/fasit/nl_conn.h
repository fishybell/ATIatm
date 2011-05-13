#ifndef _NL_CONN_H_
#define _NL_CONN_H_

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <list>

using namespace std;

#include "connection.h"

// generic base class for handling asynchronous i/o for a given file descriptor
class NL_Conn : public Connection {
public:
   NL_Conn(struct nl_handle *handle, class Connection *client, int family); // don't call directly, call via newConn
   virtual ~NL_Conn(); // closes, cleans up, etc.
   template <class C_Conn, class C_Client> static C_Conn *newConn(C_Client *client); // creates a new NL_Conn object (of the given class type)

   void queueMsg(struct nl_msg *msg); // queue a message for sending
   void queueMsg(int cmd, int att_c, size_t att_t, void *attr); // helper for queueing message with arbitrary attribute
   void queueMsgU8(int cmd, int attr); // helper for queueing message with u8 attribute 
   void queueMsgU16(int cmd, int attr); // helper for queueing message with u16 attribute 

protected:
   virtual int handleWrite(const epoll_event *ev); // overwritten to handle netlink data
   virtual int handleRead(const epoll_event *ev); // overwritten to handle netlink data
   virtual int parseData(int, const char *) { return 0; } ; // must be defined for Connection, so I define it here
   virtual int parseData(struct nl_msg *msg) = 0; // must be defined in the final message handler

   struct nl_handle *handle; // the handle to the netlink connection (more than just an fd)
   class Connection *client; // calls various functions of client on handleRead()
   int family; // the family ID we talk to (ATI family)
   
private:
   static int ignore_cb(struct nl_msg *msg, void *arg) { return NL_OK; }; // netlink callback that always ignores the message
   static int parse_cb(struct nl_msg *msg, void *arg) { return static_cast<NL_Conn*>(arg)->parseData(msg); } // netlink callback to handle the message

   list<struct nl_msg*> outq; // queue for outgoing message (no incoming queue as only complete messages are received)

};

#endif
