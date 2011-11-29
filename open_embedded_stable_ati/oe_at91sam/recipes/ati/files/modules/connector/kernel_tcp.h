#ifndef _KERNEL_TCP_H_
#define _KERNEL_TCP_H_

using namespace std;

#include "connection.h"
#include "common.h"
#include "nl_conn.h"
#include "netlink_user.h"

// generic output event structure passed over tcp
#include "target_generic_output.h"
typedef struct kern_go_event {
   int start;
   GO_event_t event;
   int end;
} kern_go_event_t;

// command event structure passed over tcp
typedef struct kern_cmd_event {
   int start;
   cmd_event_t event;
   int end;
} kern_cmd_event_t;


extern volatile int KERN_ROLE; // the role this kernel handles

/***********************************************************
*     A pair of classes to move kernel events over tcp     *
***********************************************************/

// for handling the tcp connection
class Kernel_TCP : public Connection {
public :
   Kernel_TCP(int fd, int tnum); // instantiated as client
   Kernel_TCP(int fd);           // instantiated as server
   virtual ~Kernel_TCP();
   virtual int parseData(int rsize, const char *rbuf);

   void outgoingGOEvent(kern_go_event_t *event); // send a generic output event over tcp
   void outgoingCmdEvent(kern_cmd_event_t *event); // send a command event over tcp
   void sendRole(); // send the role to the opposite tcp connection

protected:
   virtual bool hasPair() { return kern_conn != NULL;};

private:
   int role; // the role this connection handles
   class Kern_Conn *kern_conn;
};

// for handling the kernel connection
class Kern_Conn : public NL_Conn {
public:
   Kern_Conn(struct nl_handle *handle, class Kernel_TCP *client, int family); // don't call directly, call via NL_Conn::newConn
   virtual ~Kern_Conn(); // closes, cleans up, etc.
   virtual int parseData(struct nl_msg *msg); // call the correct handler in the Kernel_TCP

   void incomingGOEvent(kern_go_event_t *event); // send a generic output event to the kernel

   void incomingCmdEvent(kern_cmd_event_t *event); // send a command event to the kernel (send data)

private:
   void outgoingCmdEvent(cmd_event_t *event); // send a command event to the kernel (send object)
   Kernel_TCP *kern_tcp;
};

#endif
