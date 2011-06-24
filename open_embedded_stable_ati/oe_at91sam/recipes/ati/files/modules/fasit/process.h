#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <stdio.h>
#include "connection.h"


using namespace std;

// a minimal wrapper around Connection to allow for handling a process pipe rather than a socket
class Process : public Connection {
public:
   Process(FILE *pipe); // a new process always starts with a file stream
   virtual ~Process(); // closes, cleans up, etc.
   template <class Proc> static Proc *newProc(const char *cmd, bool readonly); // creates a new Process object (of the given class type) using the given command string

private :
   FILE *pipe; // file stream of process pipe

protected:
   virtual int handleWrite(const epoll_event *ev); // could be overwritten
   virtual int handleRead(const epoll_event *ev); // could be overwritten
};

// start a process that has no interaction and can not be cancelled
// will automatically delete on end of data
class BackgroundProcess : public Process {
public:
   BackgroundProcess(FILE *pipe); // create using BackgroundProcess::newProc()
   static void newProc(const char *cmd); // automatically sets the & in the correct place and starts the process

protected:
   int parseData(int rsize, const char *rbuf); // do nothing with the data, when finished, delete
};

#endif
