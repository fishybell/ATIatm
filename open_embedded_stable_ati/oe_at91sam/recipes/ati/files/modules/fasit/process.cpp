using namespace std;

#include "process.h"


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

DCMSG(GREEN,"fd %i read %i bytes:\n", fd, rsize);
PRINT_HEXB(buf, rsize);

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

   DCMSG(BLUE,"fd %i wrote %i bytes with 'fwrite(fwbuf, sizeof(char), fwsize, pipe)': ", fd, s);
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

