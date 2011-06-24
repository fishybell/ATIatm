#ifndef _SES_PROCS_H_
#define _SES_PROCS_H_

using namespace std;

#include "process.h"

// starts playback, ignores input, but is able to be deleted during runtime
class PlayProcess : public Process {
public:
   PlayProcess(FILE *pipe); // create using PlayProcess::playTrack()
   static void playTrack(const char *track, unsigned int loop); // starts playing and loops X times
   static void StopPlayback() {stop = true;}; // stops playback of all running play processes

protected:
   static bool stop;
   int parseData(int rsize, const char *rbuf); // do nothing with the data, but watch for stop
   const char *cmd;
   unsigned int loop;
};

#endif
