using namespace std;

#include "fasit.h"
#include "common.h"
/*
// disable DEBUG
#ifdef DEBUG
#undef DEBUG
#define DEBUG 0
#endif

// disable TRACE
#ifdef TRACE
#undef TRACE
#define TRACE 0
#endif
*/
int FASIT::messageSeq = 0;

FASIT::FASIT() {
FUNCTION_START("::FASIT()")
   rbuf = NULL;
   rsize = 0;
FUNCTION_END("::FASIT()")
}

FASIT::~FASIT() {
FUNCTION_START("::~FASIT()")
   // free the read buffer
   if (rbuf) {
      delete [] rbuf;
   }
FUNCTION_END("::~FASIT()")
}

void FASIT::addToBuffer(int rsize, const char *rbuf) {
FUNCTION_START("::addToBuffer(int rsize, const char *rbuf)")
   if (this->rsize > 0) {
      // this->rbuf exists? add new data
      char *tbuf = new char[(this->rsize + rsize)];
      memcpy(tbuf, this->rbuf, this->rsize);
      memcpy(tbuf + (sizeof(char) * this->rsize), rbuf, rsize);
      delete [] this->rbuf;
      this->rbuf = tbuf;
      this->rsize += rsize;
   } else {
      // empty this-> rbuf? copy parameters
      this->rbuf = new char[rsize];
      memcpy(this->rbuf, rbuf, rsize);
      this->rsize = rsize;
   }
FUNCTION_END("::addToBuffer(int rsize, const char *rbuf)")
}

// we don't worry about clearing the data before a valid message, just up to the end
void FASIT::clearBuffer(int end) {
FUNCTION_START("::clearBuffer(int end)")
DMSG("clearing rbuf to %i of %i\n", end, rsize)
   if (end >= rsize) {
      // clear the entire buffer
      delete [] rbuf;
      rbuf = NULL;
      rsize = 0;
   } else {
      // clear out everything up to and including end
DMSG("new buffer of %i bytes\n", rsize - end)
      char *tbuf = new char[(rsize - end)];
      memcpy(tbuf, rbuf + (sizeof(char) * end), rsize - end);
      delete [] rbuf;
      rbuf = tbuf;
      rsize -= end;
DMSG("ending with %i left\n", rsize)
   }
FUNCTION_END("::clearBuffer(int end)")
}

