using namespace std;

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include "tcp_factory.h"
#include "timers.h"
#include "serial.h"
#include "common.h"

#define MAX_CONN 1200

FASIT_TCP_Factory::FASIT_TCP_Factory(char *destIP, int port) : Connection(0xDEADBEEF) { // always invalid fd will close with an error, but do so silently
FUNCTION_START("::FASIT_TCP_Factory(char *destIP, int port) : Connection(0xDEADBEEF)")
   memset(&server, 0, sizeof(server));
   server.sin_family = AF_INET;
   inet_aton(destIP, &server.sin_addr);
   server.sin_port = htons(port);
DMSG("Created addr with ip %s, (%08x) port %i\n", inet_ntoa(server.sin_addr), server.sin_addr, port);

   setTnum(UNASSIGNED); // so unassigned tnums point to this as the source

   // schedule the resubscribe message
   new Resubscribe(RESUBSCRIBE);

FUNCTION_END("::FASIT_TCP_Factory(char *destIP, int port) : Connection(0xDEADBEEF)")
}

FASIT_TCP_Factory::~FASIT_TCP_Factory() {
FUNCTION_START("::~FASIT_TCP_Factory()")
FUNCTION_END("::~FASIT_TCP_Factory()")
}

// tell all downrange units to resubscribe
void FASIT_TCP_Factory::SendResubscribe() {
FUNCTION_START("::Send_Resubscribe")
   ATI_16008 msg;
   msg.sequence = 0;
   ATI_header hdr = FASIT::createHeader(16008, BASE_STATION, &msg, sizeof(ATI_16008));
   SerialConnection::queueMsgAll(&hdr, sizeof(ATI_header));
   SerialConnection::queueMsgAll(&msg, sizeof(ATI_16008));
FUNCTION_END("::Send_Resubscribe")
}


FASIT_TCP *FASIT_TCP_Factory::newConn() {
FUNCTION_START("::newConn()")
   int sock;

   // create the TCP socket
   if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
FUNCTION_HEX("::newConn()", NULL)
      return NULL;
   }
   IMSG("Created new TCP socket: %i\n", sock)

   // connect TCP socket to given IP and port
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
HERE
   setnonblocking(sock);

   // create new FASIT_TCP (with predefined tnum) and to our epoll list
   struct epoll_event ev;
   memset(&ev, 0, sizeof(ev));
   FASIT_TCP *tcp = new FASIT_TCP(sock, findNextTnum());
DMSG("Created new connection %i with tnum %i\n", tcp->getFD(), tcp->getTnum())
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

// look for the lowest available tnum
int FASIT_TCP_Factory::findNextTnum() {
FUNCTION_START("::findNextTnum()")
   // loop through all known tcp connections finding the lowest unused tnum
   __uint16_t lowTnum = 0;
   FASIT_TCP *tcp = FASIT_TCP::getFirst();
   while (tcp != NULL) {
      lowTnum = max(lowTnum, tcp->getTnum());
      tcp = tcp->getNext();
   }
   if (lowTnum > 0 && FASIT_TCP::findByTnum(lowTnum - 1) == NULL) {
      // there exists a hole in the tnum list, fill it from the top down
      lowTnum--;
   } else {
      // there are no holes in the list, the next tnum is the lowest available
      lowTnum++;
   }
FUNCTION_INT("::findNextTnum()", lowTnum)
   return lowTnum;
}
