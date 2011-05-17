#ifndef _KERNEL_TCP_H_
#define _KERNEL_TCP_H_

using namespace std;

#include "connection.h"
#include "common.h"
#include "nl_conn.h"
#include "netlink_user.h"

// event structure passed over tcp
#include "target_generic_output.h"
typedef struct kern_event {
   int start;
   GO_event_t event;
   int end;
} kern_event_t;

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

   void outgoingEvent(kern_event_t *event); // send an event over tcp

protected:
   virtual bool hasPair() { return kern_conn != NULL;};

private:
   class Kern_Conn *kern_conn;
};

// for handling the kernel connection
class Kern_Conn : public NL_Conn {
public:
   Kern_Conn(struct nl_handle *handle, class Kernel_TCP *client, int family); // don't call directly, call via NL_Conn::newConn
   virtual ~Kern_Conn(); // closes, cleans up, etc.
   virtual int parseData(struct nl_msg *msg); // call the correct handler in the Kernel_TCP

   void incomingEvent(kern_event_t *event); // send an event to the kernel

private:
   Kernel_TCP *kern_tcp;
};

#endif
