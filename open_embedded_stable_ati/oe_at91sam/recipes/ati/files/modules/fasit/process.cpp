#include <errno.h>

using namespace std;

#include "common.h"
#include "process.h"

#define CMD_BUFFER_SIZE 1024

/***********************************************************
*                       Process Class                      *
***********************************************************/
Process::Process(FILE *pipe) : Connection(fileno(pipe)) {
FUNCTION_START("::Process(FILE *pipe)")

   // set local member
   this->pipe = pipe;

FUNCTION_END("::Process(FILE *pipe)")
}

Process::~Process() {
FUNCTION_START("::~Process()")

   // calling close instead of pclose should be fine, so let parent class do the work

FUNCTION_END("::~Process()")
}

// the file stream is ready to give us data, read as much as possible (max of BUF_SIZE)
int Process::handleRead(const epoll_event *ev) {
FUNCTION_START("Process::handleRead(const epoll_event *ev)");
   char buf[BUF_SIZE+1];
   int rsize=0;
   rsize = fread(buf, sizeof(char), BUF_SIZE, pipe);

DCMSG(BLUE,"pipe 0x%08x read %i bytes:\n", pipe, rsize);
DCMSG(BLUE, "<<<<<<<\n%s\n>>>>>>>\n", buf);

   DCOLOR(black) ;
   if (rsize == -1) {
      IERROR("Read error: %s\n", strerror(errno))
   }
   if (rsize > 0) {
      int ret = parseData(rsize, buf);
FUNCTION_INT("Process::handleRead(const epoll_event *ev)", ret);
      return ret;
   } else if (rsize != -1 || (rsize == -1 && errno != EAGAIN)) {
      // the process has closed, schedule the deletion by returning -1
FUNCTION_INT("Process::handleRead(const epoll_event *ev)", -1);
      return -1;
   }
FUNCTION_INT("Process::handleRead(const epoll_event *ev)", 0);
   return 0;
}

// the file stream is ready to receive the data, send it on through
int Process::handleWrite(const epoll_event *ev) {
FUNCTION_START("Process::handleWrite(const epoll_event *ev)");
   if (wbuf.empty()) {
      // we only send data, or listen for writability, if we have something to write
      makeWritable(false);
      FUNCTION_INT("Process::handleWrite(const epoll_event *ev)", 0);
      return 0;
   }

   // grab first items from write buffer lists (in the back, to treat it as a queue)
   char *fwbuf = wbuf.back();
   int fwsize = wsize.back();

   // assume they're gone until proven otherwise
   wbuf.pop_back();
   wsize.pop_back();

   // write all the data we can
   int s = fwrite(fwbuf, sizeof(char), fwsize, pipe);

   // did it fail?
   if (s <= 0) {
      // failed, push back onto back of list
      wbuf.push_back(fwbuf);
      wsize.push_back(fwsize);
      newMsg = true; // set this message as a discrete message
      if (s == 0 || errno == EAGAIN) {
         FUNCTION_INT("Process::handleWrite(const epoll_event *ev)", 0);
         return 0;
      } else {
         FUNCTION_INT("Process::handleWrite(const epoll_event *ev)", -1);
         return -1;
      }
   }

   DCMSG(BLUE,"pipe 0x%08x wrote %i bytes with 'fwrite(fwbuf, sizeof(char), fwsize, pipe)': ", pipe, s);
   CPRINT_HEXB(BLUE,fwbuf, s);
   DCOLOR(black);

   // copy what we did write to the "last write buffer"
   if (lwbuf != NULL) { delete [] lwbuf; } // clear out old buffer
   lwsize = s;
   lwbuf = new char[lwsize];
   memcpy(lwbuf, fwbuf, lwsize);

   if (s < fwsize) {
      // create a new, smaller write buffer if we didn't write everything
      char *tbuf = new char[(fwsize - s)];
      memcpy(tbuf, fwbuf + (sizeof(char) * s), fwsize - s);
      delete [] fwbuf;
      fwbuf = tbuf;
      fwsize -= s;
      // push remainder to back of list
      wbuf.push_back(fwbuf);
      wsize.push_back(fwsize);
      newMsg = true; // set the remainder as a discrete message
   } else {
      // everything was written, clear write buffer
      fwsize = 0;
      delete [] fwbuf;
   }

   // success
FUNCTION_INT("Process::handleWrite(const epoll_event *ev)", 0)
   return 0;
}

template <class Proc>
Proc *Process::newProc(const char *cmd, bool readonly) {
FUNCTION_START("::newProc(const char *cmd, bool readonly)")
   Proc *proc = NULL;
   FILE *pipe = NULL;
   int fd = 0;

   DCMSG(RED, "Starting process <<%s>>", cmd);
   // open pipe to process using the given command
   if ( !(pipe = (FILE*)popen(cmd,readonly?"r":"w")) ) {
      perror("Problems with creating pipe");
FUNCTION_HEX("::newProc(const char *cmd, bool readonly)", NULL)
      return NULL;
   }
   fd = fileno(pipe);

   // Create process object to track process progress
   proc = new Proc(pipe);

   // add to epoll
   if (!addToEPoll(fd, proc)) {
       delete proc;
FUNCTION_HEX("::newProc(const char *cmd, bool readonly)", NULL)
       return NULL;
   }

   // return the result
FUNCTION_HEX("::newProc(const char *cmd, bool readonly)", proc)
   return proc;
}

/***********************************************************
*                  BackgroundProcess Class                 *
***********************************************************/
BackgroundProcess::BackgroundProcess(FILE *pipe) : Process(pipe) {
FUNCTION_START("::BackgroundProcess(FILE *pipe)");

   // don't do anything

FUNCTION_END("::BackgroundProcess(FILE *pipe)");
}

void BackgroundProcess::newProc(const char *cmd) {

   // look to see if we need to add an ampersand
   bool needAmp = true;
   for (int i=0; i<CMD_BUFFER_SIZE && cmd[i] != '\0'; i++) {
      if (cmd[i] == '&') {
         needAmp = false;
      }
   }

   // run the process with or without ampersand
   if (needAmp) {
      // put in ampersand to make process run in background
      char *newCmd = new char[CMD_BUFFER_SIZE];
      snprintf(newCmd, CMD_BUFFER_SIZE, "%s &", cmd);
      Process::newProc<BackgroundProcess>(newCmd, true); // read only, discard return value
      delete [] newCmd;
   } else {
      // run as-is
      Process::newProc<BackgroundProcess>(cmd, true); // read only, discard return value
   }
}

// do nothing with the data, when finished, delete
int BackgroundProcess::parseData(int rsize, const char *rbuf) {
FUNCTION_START("::parseData(int rsize, const char *rbuf)");
   // ignore data
   DCMSG(RED, "Deleting background process") ;
FUNCTION_INT("::parseData(int rsize, const char *rbuf)", -1);
   return -1; // delete after first read (running in background, won't care if pipe is lost?)
}


// explicit declarations of newProc() template function
template BackgroundProcess *Process::newProc<BackgroundProcess>(const char *cmd, bool readonly);

