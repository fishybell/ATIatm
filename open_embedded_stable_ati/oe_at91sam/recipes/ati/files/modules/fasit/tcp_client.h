#ifndef _TCP_CLIENT_H_
#define _TCP_CLIENT_H_

using namespace std;

#include <list>
#include <map>
#include "fasit.h"
#include "connection.h"
#include "common.h"
#include "fasit_tcp.h"

// class for FASIT client
// parses TCP messages
class TCP_Client : public FASIT_TCP {
public :
   TCP_Client(int fd, int tnum);
   virtual ~TCP_Client();
   friend class FASIT_TCP;

private :
   // individual message handlers, all return -1 if the connectionneeds to be
   //   deleted afterwards
   // the message data itself is in the read buffer from start to end
   int handle_100(int start, int end);
   int handle_2000(int start, int end);
   int handle_2004(int start, int end);
   int handle_2005(int start, int end);
   int handle_2006(int start, int end);
   int handle_2100(int start, int end);
   int handle_2101(int start, int end);
   int handle_2111(int start, int end);
   int handle_2102(int start, int end);
   int handle_2114(int start, int end);
   int handle_2115(int start, int end);
   int handle_2110(int start, int end);
   int handle_2112(int start, int end);
   int handle_2113(int start, int end);

   // server instance
   FASIT_TCP *server;
};

#endif
