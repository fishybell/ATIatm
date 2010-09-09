using namespace std;

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "tcp_factory.h"
#include "common.h"

#define MAX_CONN 1200

FASIT_TCP_Factory::FASIT_TCP_Factory(char *destIP, int port) : Connection(0xDEADBEEF) { // always invalid fd will close with an error, but do so silently
   memset(&server, 0, sizeof(server));
   server.sin_family = AF_INET;
   server.sin_addr.s_addr = inet_addr(destIP);
   server.sin_port = port;

   // this is incrimented before use
   last_tnum = 0;

   setTnum(UNASSIGNED); // so unassigned tnums point to this as the source
}

FASIT_TCP_Factory::~FASIT_TCP_Factory() {
}

FASIT_TCP *FASIT_TCP_Factory::newConn() {
   int sock;

   // create the TCP socket
   if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
      return NULL;
   }

   // connect TCP socket to given IP and port
   if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
      return NULL;
   }
   setnonblocking(sock);

   // create new FASIT_TCP (with predefined tnum) and to our epoll list
   struct epoll_event ev;
   memset(&ev, 0, sizeof(ev));
   FASIT_TCP *tcp = new FASIT_TCP(sock, ++last_tnum);
   ev.events = EPOLLIN;
   ev.data.ptr = (void*)tcp;
   if (epoll_ctl(efd, EPOLL_CTL_ADD, sock, &ev) < 0) {
      delete tcp;
      return NULL;
   }

   // return the result
   return tcp;
}
