#ifndef _TCP_CLIENT_H_
#define _TCP_CLIENT_H_

using namespace std;

#include "fasit.h"
#include "connection.h"
#include "common.h"
#include "fasit_tcp.h"

// class for FASIT client (auto reconnects)
// parses TCP messages
class TCP_Client : public FASIT_TCP {
public :
   TCP_Client(int fd, int tnum, bool armor);
   virtual ~TCP_Client();
   friend class FASIT_TCP;

   // cause a reconnect attempt in 10 seconds
   virtual bool reconnect();

   // delayed reconnect handler
   void handleReconnect();

private :

protected:
   // server instance
   FASIT_TCP *server;
   virtual bool hasPair() { return server != NULL;};
   virtual Connection *pair() { return (Connection*)server; }
};

#endif
