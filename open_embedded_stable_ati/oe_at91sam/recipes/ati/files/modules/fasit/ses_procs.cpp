#include <errno.h>

using namespace std;

#include "common.h"
#include "ses_procs.h"
#include "ses_client.h"

#define CMD_BUFFER_SIZE 1024

// define static members
bool PlayProcess::stop = false; // stop value

/***********************************************************
*                     PlayProcess Class                    *
***********************************************************/
PlayProcess::PlayProcess(FILE *pipe) : Process(pipe) {
FUNCTION_START("::PlayProcess(FILE *pipe)");

   // don't do anything

FUNCTION_END("::PlayProcess(FILE *pipe)");
}

// starts playing track and loops X times
void PlayProcess::playTrack(const char *track, unsigned int loop) {

   PlayProcess *play;
   char *cmd = new char[CMD_BUFFER_SIZE];


   // create the play command with the correct repeat values
   if (loop <= NO_LOOP) {
      // don't repeat
      DCMSG(BLUE, "Playing track <<%s>> without repeat", track);
      snprintf(cmd, CMD_BUFFER_SIZE, "esddsp madplay -m %s", track);
   } else if (loop == INFINITE_LOOP) {
      // repeat forever
      DCMSG(BLUE, "Playing track <<%s>> with infinite repeat", track);
      snprintf(cmd, CMD_BUFFER_SIZE, "esddsp madplay -m -r %s", track);
   } else {
      // repeat "loop" times
      DCMSG(BLUE, "Playing track <<%s>> with %i repeats", track, loop);
      snprintf(cmd, CMD_BUFFER_SIZE, "esddsp madplay -m -r%i %s", track, loop);
   }

   // start playback
   play = Process::newProc<PlayProcess>(cmd, true); // read only

   // set cmd and loop values in play process
   play->loop = loop;
   play->cmd = cmd;
   delete [] cmd;
}

// do nothing with the data, when finished, delete
int PlayProcess::parseData(int rsize, const char *rbuf) {
FUNCTION_START("::parseData(int rsize, const char *rbuf)");
   // ignore data
   int retval = 0;
   if (stop) {
      DCMSG(RED, "Stopping playback");
      retval = -1;
   }
FUNCTION_INT("::parseData(int rsize, const char *rbuf)", retval);
   return retval;
}

