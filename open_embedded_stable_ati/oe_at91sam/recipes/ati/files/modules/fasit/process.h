#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <stdio.h>
#include "connection.h"


using namespace std;

// a minimal wrapper around Connection to allow for handling a process pipe rather than a socket
class Process : public Connection {
public:
   Process(FILE *pipe); // a new connection always starts with a file stream
   virtual ~Process(); // closes, cleans up, etc.

private :
   FILE *pipe; // file stream of process pipe

protected:
   virtual int handleWrite(const epoll_event *ev); // could be overwritten
   virtual int handleRead(const epoll_event *ev); // could be overwritten
};

#endif
