#ifndef _KERNEL_TCP_H_
#define _KERNEL_TCP_H_

using namespace std;

#include "connection.h"
#include "common.h"
#include "nl_conn.h"
#include "netlink_user.h"

class Kernel_TCP : public Connection {
public :
   Kernel_TCP(int fd);
   virtual ~Kernel_TCP();
   virtual int parseData(int rsize, const char *rbuf);

protected:
   virtual bool hasPair() { return nl_conn != NULL;};

private:
   class Kern_Conn *nl_conn;
};

// for handling the kernel connection for the SIT
class Kern_Conn : public NL_Conn {
public:
   Kern_Conn(struct nl_handle *handle, class Kernel_TCP *client, int family); // don't call directly, call via NL_Conn::newConn
   virtual ~Kern_Conn(); // closes, cleans up, etc.
   virtual int parseData(struct nl_msg *msg); // call the correct handler in the Kernel_TCP

private:
   Kernel_TCP *kern_tcp;
};

#endif
