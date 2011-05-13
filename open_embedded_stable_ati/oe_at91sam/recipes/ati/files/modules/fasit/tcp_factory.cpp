#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>

using namespace std;

#include "tcp_factory.h"
#include "timers.h"
#include "common.h"

// for explicit declarations of template function
#ifdef FASIT_CONN
   #include "tcp_client.h"
   #include "sit_client.h"
   #include "mit_client.h"
#endif

#ifdef EVENT_CONN
   #include "kernel_tcp.h"
#endif

#define MAX_CONN 1200

TCP_Factory::TCP_Factory(const char *destIP, int port) : Connection(0xDEADBEEF) { // always invalid fd will close with an error, but do so silently
FUNCTION_START("::TCP_Factory(const char *destIP, int port) : Connection(0xDEADBEEF)")
   memset(&server, 0, sizeof(server));
   server.sin_family = AF_INET;
   inet_aton(destIP, &server.sin_addr);
   server.sin_port = htons(port);
DMSG("Created addr with ip %s, (%08x) port %i\n", inet_ntoa(server.sin_addr), server.sin_addr, port);

   setTnum(UNASSIGNED); // so unassigned tnums point to this as the source

FUNCTION_END("::TCP_Factory(const char *destIP, int port) : Connection(0xDEADBEEF)")
}

TCP_Factory::~TCP_Factory() {
FUNCTION_START("::~TCP_Factory()")
FUNCTION_END("::~TCP_Factory()")
}

// look for the lowest available tnum
int TCP_Factory::findNextTnum() {
FUNCTION_START("::findNextTnum()")
   // loop through all known tcp connections finding the lowest unused tnum
   __uint16_t lowTnum = 0;
   Connection *tcp = TCP_Client::getFirst();
   while (tcp != NULL) {
      lowTnum = max(lowTnum, tcp->getTnum());
      tcp = tcp->getNext();
   }
   if (lowTnum > 0 && TCP_Client::findByTnum(lowTnum - 1) == NULL) {
      // there exists a hole in the tnum list, fill it from the top down
      lowTnum--;
   } else {
      // there are no holes in the list, the next tnum is the lowest available
      lowTnum++;
   }
FUNCTION_INT("::findNextTnum()", lowTnum)
   return lowTnum;
}

template <class TCP_Class>
TCP_Class *TCP_Factory::newConn() {
FUNCTION_START("::newConn()")
   int sock;

   // create the TCP socket
   if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
FUNCTION_HEX("::newConn()", NULL)
      return NULL;
   }
   IMSG("Created new TCP socket: %i\n", sock)

   // connect TCP socket to given IP and port
   DMSG("server: %s : %i\n", inet_ntoa(server.sin_addr), ntohs(server.sin_port));
   if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
      switch (errno) {
         case EACCES : IERROR("errno: EACCES\n") break;
         case EPERM : IERROR("errno: EPERM\n") break;
         case EADDRINUSE : IERROR("errno: EADDRINUSE\n") break;
         case EAFNOSUPPORT : IERROR("errno: EAFNOSUPPORT\n") break;
         case EAGAIN : IERROR("errno: EAGAIN\n") break;
         case EALREADY : IERROR("errno: EALREADY\n") break;
         case EBADF : IERROR("errno: EBADF\n") break;
         case ECONNREFUSED : IERROR("errno: ECONNREFUSED\n") break;
         case EFAULT : IERROR("errno: EFAULT\n") break;
         case EINPROGRESS : IERROR("errno: EINPROGRESS\n") break;
         case EINTR : IERROR("errno: EINTR\n") break;
         case EISCONN : IERROR("errno: ISCONN\n") break;
         case ENETUNREACH : IERROR("errno: ENETUNREACH\n") break;
         case ENOTSOCK : IERROR("errno: ENOTSOCK\n") break;
         case ETIMEDOUT : IERROR("errno: ETIMEDOUT\n") break;
      }
FUNCTION_HEX("::newConn()", NULL)
      return NULL;
   }
   setnonblocking(sock);

   // create new TCP_Class (with predefined tnum) and to our epoll list
   TCP_Class *tcp = new TCP_Class(sock, findNextTnum());
DMSG("Created new connection %i with tnum %i\n", tcp->getFD(), tcp->getTnum())
   if (!addToEPoll(tcp->getFD(), tcp)) {
      delete tcp;
FUNCTION_HEX("::newConn()", NULL)
      return NULL;
   }

   // return the result
FUNCTION_HEX("::newConn()", tcp)
   return tcp;
}

// explicit declarations of newConn() template function
#ifdef FASIT_CONN
   template TCP_Client *TCP_Factory::newConn<TCP_Client>();
   template SIT_Client *TCP_Factory::newConn<SIT_Client>();
   template MIT_Client *TCP_Factory::newConn<MIT_Client>();
#endif

#ifdef EVENT_CONN
   template Kernel_TCP *TCP_Factory::newConn<Kernel_TCP>();
#endif
