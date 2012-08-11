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
   #include "ses_client.h"
#endif

#ifdef EVENT_CONN
   #include "kernel_tcp.h"
#endif

#define MAX_CONN 1200

TCP_Factory::TCP_Factory(const char *destIP, int port) : Connection(0xDEADBEEF) { // always invalid fd will close with an error, but do so silently
FUNCTION_START("::TCP_Factory(const char *destIP, int port) : Connection(0xDEADBEEF)")
   memset(&server, 0, sizeof(server));
   server.sin_family = AF_INET;
   auto_ip = false;
   if (!inet_aton(destIP, &server.sin_addr)) {
      // failed to get IP from given string, automatically find vai multi-cast dns
      auto_ip = true;
      inet_aton("0.0.0.0", &server.sin_addr);
   }
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

// returns a connected and ready socket file descriptor for use in a client
int TCP_Factory::newClientSock() {
FUNCTION_START("::newClientSock()")
   // find server IP if auto_ip is set
   if (auto_ip) {
      // setup a readable command pipe (use multi-cast dns to find "server.local")
      FILE *pipe = NULL;
      if ( !(pipe = (FILE*)popen("avahi-resolve-address -n server.local | cut -f2", "r")) ) {
         IERROR("Problems with creating pipe");
FUNCTION_INT("::newClientSock()", -1)
         return -1;
      }

      // this will block, but we're only using this when we don't have a client, so it's okay
      char buf[BUF_SIZE+1];
      int rsize=0;
      rsize = fread(buf, sizeof(char), BUF_SIZE, pipe);
      int err = errno; // save errno

      // check that we read an IP
      if (rsize > 0) {
         if (!inet_aton(buf, &server.sin_addr)) {
            // didn't read, set back to bad address
            inet_aton("0.0.0.0", &server.sin_addr);
            return -1;
         }
         DCMSG(BLUE, "Auto-IP found address %s\n", inet_ntoa(server.sin_addr));
      } else {
         IERROR("Read error: %s\n", strerror(err))
FUNCTION_INT("::newClientSock()", -1)
         return -1;
      }
   }

   // create the TCP socket
   int sock;
   if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
FUNCTION_INT("::newClientSock()", -1)
      return -1;
   }
   IMSG("Created new TCP socket: %i\n", sock)

   // connect TCP socket to given IP and port
   DMSG("server: %s : %i\n", inet_ntoa(server.sin_addr), ntohs(server.sin_port));
   if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
      int err = errno; // save errno
      switch (err) {
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
FUNCTION_INT("::newClientSock()", -1)
      return -1;
   }
   setnonblocking(sock, true); // socket

FUNCTION_INT("::newClientSock", sock)
   return sock;
}

template <class TCP_Class>
TCP_Class *TCP_Factory::newConn() {
FUNCTION_START("::newConn()")

   // grab new socket file descriptor
   int sock = newClientSock();

   // create new TCP_Class (with predefined tnum)
   TCP_Class *tcp = new TCP_Class(sock, findNextTnum());
DMSG("Created new connection %i with tnum %i\n", tcp->getFD(), tcp->getTnum())

   // check to see if it connected and attempt adding to epoll
   if (sock == -1 || !addToEPoll(tcp->getFD(), tcp)) {
      // can this call reconnect?
      if (!tcp->reconnect()) {
         // no, delete now and return NULL
         delete tcp;
FUNCTION_HEX("::newConn()", NULL)
         return NULL;
      }
      // yes, it will do so at its own pace
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
   template SES_Client *TCP_Factory::newConn<SES_Client>();
#endif

#ifdef EVENT_CONN
   template Kernel_TCP *TCP_Factory::newConn<Kernel_TCP>();
#endif
