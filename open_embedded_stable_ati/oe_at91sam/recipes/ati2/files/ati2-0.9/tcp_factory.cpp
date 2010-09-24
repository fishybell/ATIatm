using namespace std;

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "tcp_factory.h"
#include "common.h"
#include "errno.h"

#define MAX_CONN 1200

FASIT_TCP_Factory::FASIT_TCP_Factory(char *destIP, int port) : Connection(0xDEADBEEF) { // always invalid fd will close with an error, but do so silently
FUNCTION_START("::FASIT_TCP_Factory(char *destIP, int port) : Connection(0xDEADBEEF)")
   memset(&server, 0, sizeof(server));
   server.sin_family = AF_INET;
   inet_aton(destIP, &server.sin_addr);
   server.sin_port = htons(port);
DMSG("Created addr with ip %s, (%08x) port %i\n", inet_ntoa(server.sin_addr), server.sin_addr, port);

   // this is incrimented before use
   last_tnum = 0;

   setTnum(UNASSIGNED); // so unassigned tnums point to this as the source
FUNCTION_END("::FASIT_TCP_Factory(char *destIP, int port) : Connection(0xDEADBEEF)")
}

FASIT_TCP_Factory::~FASIT_TCP_Factory() {
FUNCTION_START("::~FASIT_TCP_Factory()")
FUNCTION_END("::~FASIT_TCP_Factory()")
}

FASIT_TCP *FASIT_TCP_Factory::newConn() {
FUNCTION_START("::newConn()")
   int sock;

   // create the TCP socket
   if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
FUNCTION_HEX("::newConn()", NULL)
      return NULL;
   }

   // connect TCP socket to given IP and port
   if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
      switch (errno) {
         case EACCES : DMSG("errno: EACCES\n") break;
         case EPERM : DMSG("errno: EPERM\n") break;
         case EADDRINUSE : DMSG("errno: EADDRINUSE\n") break;
         case EAFNOSUPPORT : DMSG("errno: EAFNOSUPPORT\n") break;
         case EAGAIN : DMSG("errno: EAGAIN\n") break;
         case EALREADY : DMSG("errno: EALREADY\n") break;
         case EBADF : DMSG("errno: EBADF\n") break;
         case ECONNREFUSED : DMSG("errno: ECONNREFUSED\n") break;
         case EFAULT : DMSG("errno: EFAULT\n") break;
         case EINPROGRESS : DMSG("errno: EINPROGRESS\n") break;
         case EINTR : DMSG("errno: EINTR\n") break;
         case EISCONN : DMSG("errno: ISCONN\n") break;
         case ENETUNREACH : DMSG("errno: ENETUNREACH\n") break;
         case ENOTSOCK : DMSG("errno: ENOTSOCK\n") break;
         case ETIMEDOUT : DMSG("errno: ETIMEDOUT\n") break;
      }
FUNCTION_HEX("::newConn()", NULL)
      return NULL;
   }
HERE
   setnonblocking(sock);

   // create new FASIT_TCP (with predefined tnum) and to our epoll list
   struct epoll_event ev;
   memset(&ev, 0, sizeof(ev));
   FASIT_TCP *tcp = new FASIT_TCP(sock, ++last_tnum);
DMSG("Created new connection %i with tnum %i\n", tcp-getFD(), tcp->getTnum())
   ev.events = EPOLLIN;
   ev.data.ptr = (void*)tcp;
   if (epoll_ctl(efd, EPOLL_CTL_ADD, sock, &ev) < 0) {
      delete tcp;
FUNCTION_HEX("::newConn()", NULL)
      return NULL;
   }

   // return the result
FUNCTION_HEX("::newConn()", tcp)
   return tcp;
}
