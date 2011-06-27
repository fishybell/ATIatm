#include <errno.h>

using namespace std;

#include "common.h"
#include "ses_procs.h"
#include "ses_client.h"

#include "target_ses_interface.h"

#define CMD_BUFFER_SIZE 1024

// define static members
bool PlayProcess::stop = false; // stop value
bool RecordProcess::stop = false; // stop value
bool RecordProcess::started = false; // started value
bool EncodeProcess::stop = false; // stop value
bool EncodeProcess::started = false; // started value

/***********************************************************
*                     PlayProcess Class                    *
***********************************************************/
PlayProcess::PlayProcess(FILE *pipe) : Process(pipe) {
FUNCTION_START("::PlayProcess(FILE *pipe)");

   // not stopped anymore
   stop = false;

FUNCTION_END("::PlayProcess(FILE *pipe)");
}

// starts playing track and loops X times
void PlayProcess::playTrack(const char *track, unsigned int loop) {
FUNCTION_START("::playTrack(const char *track, unsigned int loop)");

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

   // set loop in play process
   play->loop = loop;
   delete [] cmd;
FUNCTION_END("::playTrack(const char *track, unsigned int loop)");
}

// stops playback of all running play processes
void PlayProcess::StopPlayback() {
FUNCTION_START("::StopPlayback()");
   stop = true;
   BackgroundProcess::newProc("killall -9 -q madplay"); // kills all running play processes
FUNCTION_END("::StopPlayback()");
}

// do nothing with the data, when stopped, delete
int PlayProcess::parseData(int rsize, const char *rbuf) {
FUNCTION_START("::parseData(int rsize, const char *rbuf)");
   // ignore data
   int retval = 0;
   if (stop) { // won't necessary get here, if process stops on its own from killall first for example
      DCMSG(RED, "Stopping playback");
      retval = -1;
   }
FUNCTION_INT("::parseData(int rsize, const char *rbuf)", retval);
   return retval;
}


/***********************************************************
*                    RecordProcess Class                   *
***********************************************************/
RecordProcess::RecordProcess(FILE *pipe) : Process(pipe) {
FUNCTION_START("::RecordProcess(FILE *pipe)");

   // ready for track
   track = new char[CMD_BUFFER_SIZE];

   // not stopped anymore
   stop = false;
   started = true;

FUNCTION_END("::RecordProcess(FILE *pipe)");
}

RecordProcess::~RecordProcess() {
FUNCTION_START("::~RecordProcess()");

   // the record process closed, either due to stop or due to end of recording
   if (stop) {
      // on stop delete any unencoded tracks
      char *cmd = new char[CMD_BUFFER_SIZE];
      snprintf(cmd, CMD_BUFFER_SIZE, "rm -f %s.wav", track); // remove only the unencoded portion
      BackgroundProcess::newProc(cmd); // removes track in background
      delete [] cmd;
   } else {
      // not stopped, encode now
      EncodeProcess::encodeTrack(track, client);
   }

   // we're no longer running
   started = false;
   delete [] track;

FUNCTION_END("::~RecordProcess()");
}

// starts recording track
void RecordProcess::recordTrack(const char *track, class SES_Client *client) {
FUNCTION_START("::recordTrack(const char *track, class SES_Client *client)");

   RecordProcess *record;
   char *cmd = new char[CMD_BUFFER_SIZE];


   // create the record command with the correct .wav extension
   DCMSG(BLUE, "Recording track <<%s.wav>>", track);
   snprintf(cmd, CMD_BUFFER_SIZE, "arecord -f cd -c 1 %s.wav", track);

   // start recording
   record = Process::newProc<RecordProcess>(cmd, true); // read only

   // set track in record process
   strncpy(record->track, track, CMD_BUFFER_SIZE);
   record->client = client;
   delete [] cmd;
FUNCTION_END("::recordTrack(const char *track, class SES_Client *client)");
}

// stops recording of all running record processes, starts encoding
void RecordProcess::StartEncoding() {
FUNCTION_START("::StartEncoding()");
   DCMSG(RED, "Starting encoding?")
   BackgroundProcess::newProc("killall -q arecord"); // kills all running record processes (as nicely as possible)
   // when process closes, the pipe closes, then ~RecordProcess() will be called, which starts actuall recording
FUNCTION_END("::StartEncoding()");
}

// stops recording of all running record processes, and doesn't encode
void RecordProcess::StopRecording() {
FUNCTION_START("::StopRecording()");
   stop = true;
   BackgroundProcess::newProc("killall -9 -q arecord"); // kills all running record processes
FUNCTION_END("::StopRecording()");
}

// do nothing with the data, when stopped, delete
int RecordProcess::parseData(int rsize, const char *rbuf) {
FUNCTION_START("::parseData(int rsize, const char *rbuf)");
   // ignore data
   int retval = 0;
   if (stop) { // won't necessary get here, if process stops on its own from killall first for example
      DCMSG(RED, "Stopping recording");
      retval = -1;
   }
FUNCTION_INT("::parseData(int rsize, const char *rbuf)", retval);
   return retval;
}


/***********************************************************
*                    EncodeProcess Class                   *
***********************************************************/
EncodeProcess::EncodeProcess(FILE *pipe) : Process(pipe) {
FUNCTION_START("::EncodeProcess(FILE *pipe)");

   // ready for track
   track = new char[CMD_BUFFER_SIZE];

   // not stopped anymore
   stop = false;
   started = true;

FUNCTION_END("::EncodeProcess(FILE *pipe)");
}

EncodeProcess::~EncodeProcess() {
FUNCTION_START("::~EncodeProcess()");

   // when finished (or stopped) delete any unencoded tracks
   char *cmd = new char[CMD_BUFFER_SIZE];
   snprintf(cmd, CMD_BUFFER_SIZE, "rm -f %s.wav", track); // remove only the unencoded portion
   BackgroundProcess::newProc(cmd); // removes track in background
   delete [] cmd;

   // tell client we're done
   client->doMode(MODE_REC_DONE);

   // we're no longer running
   started = false;
   delete [] track;

FUNCTION_END("::~EncodeProcess()");
}

// starts encoding track
void EncodeProcess::encodeTrack(const char *track, class SES_Client *client) {
FUNCTION_START("::encodeTrack(const char *track, class SES_Client *client)");

   EncodeProcess *encode;
   char *cmd = new char[CMD_BUFFER_SIZE];

   // create the encode command with the correct .wav extension
   DCMSG(BLUE, "Encoding track <<%s.wav>> to <<%s>>", track, track);
   snprintf(cmd, CMD_BUFFER_SIZE, "lame %s.wav %s 2>&1", track, track);

   // start encoding
   encode = Process::newProc<EncodeProcess>(cmd, true); // read only

   // set track in encode process
   strncpy(encode->track, track, CMD_BUFFER_SIZE);
   encode->client = client;
   delete [] cmd;
FUNCTION_END("::encodeTrack(const char *track, class SES_Client *client)");
}

// stops encoding of all running encode processes, and doesn't encode
void EncodeProcess::StopEncoding() {
FUNCTION_START("::StopEncoding()");
   stop = true;
   BackgroundProcess::newProc("killall -9 -q lame"); // kills all running encode processes
FUNCTION_END("::StopEncoding()");
}

// do nothing with the data, when stopped, delete
int EncodeProcess::parseData(int rsize, const char *rbuf) {
FUNCTION_START("::parseData(int rsize, const char *rbuf)");
   // ignore data
   int retval = 0;
   if (stop) { // won't necessary get here, if process stops on its own from killall first for example
      DCMSG(RED, "Stopping encoding");
      retval = -1;
   }
FUNCTION_INT("::parseData(int rsize, const char *rbuf)", retval);
   return retval;
}

