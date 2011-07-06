#ifndef _SES_PROCS_H_
#define _SES_PROCS_H_

using namespace std;

#include "process.h"

// starts playback, ignores input, is able to be stopped during runtime
class PlayProcess : public Process {
public:
   PlayProcess(FILE *pipe); // create using PlayProcess::playTrack()
   static void playTrack(const char *track, unsigned int loop); // starts playing and loops X times
   static void StopPlayback(); // stops playback of all running play processes

protected:
   static bool stop;
   int parseData(int rsize, const char *rbuf); // do nothing with the data, but watch for stop
   unsigned int loop;
};

// starts recording, ignores input, is able to be stopped during runtime
// NOTE : only create one process at a time, undefined results if multiple are running
class RecordProcess : public Process {
public:
   RecordProcess(FILE *pipe); // create using RecordProcess::recordTrack()
   virtual ~RecordProcess();
   static void recordTrack(const char *track, class SES_Client *client); // starts recording
   static void StartEncoding(); // stops recording of running record process, and starts encoding
   static void StopRecording(); // stops recording of running record process, doesn't encode
   static bool isRecording() {return started;} ; // returns true if recording

protected:
   static bool stop;
   static bool started;
   int parseData(int rsize, const char *rbuf); // do nothing with the data, but watch for stop
   char *track;
   class SES_Client *client;
};

// starts encoding, ignores input, is able to be stopped during runtime
// NOTE : only create one process at a time, undefined results if multiple are running
class EncodeProcess : public Process {
public:
   EncodeProcess(FILE *pipe); // create using EncodeProcess::encodeTrack()
   virtual ~EncodeProcess();
   static void encodeTrack(const char *track, class SES_Client *client); // starts encoding
   static void StopEncoding(); // stops encoding of running encode process
   static bool isEncoding() {return started;} ; // returns true if encoding

protected:
   static bool stop;
   static bool started;
   int parseData(int rsize, const char *rbuf); // do nothing with the data, but watch for stop
   char *track;
   class SES_Client *client;
};

// starts streaming, ignores input, is able to be stopped during runtime
// NOTE : only create one process at a time, undefined results if multiple are running
// NOTE : cannot play audio tracks during streaming process, recording capabilities during streaming are undefined
class StreamProcess : public Process {
public:
   StreamProcess(FILE *pipe); // create using StreamProcess::streamURI() or StreamProcess::changeURI()
   virtual ~StreamProcess();
   static void streamURI(const char *uri); // starts streaming
   static void changeURI(const char *uri); // stops current stream and starts new one
   static void StopStreaming(); // stops streaming of running stream process
   static bool isStreaming() {return started;} ; // returns true if streaming

protected:
   static int stop; // 0 = not stopped, 1 = stopped, 2 = changing
   static bool started;
   int parseData(int rsize, const char *rbuf); // do nothing with the data, but watch for stop
   char *uri;
};
#endif
