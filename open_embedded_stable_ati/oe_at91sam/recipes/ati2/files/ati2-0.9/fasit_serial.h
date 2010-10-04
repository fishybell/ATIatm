#ifndef _FASIT_SERIAL_H_
#define _FASIT_SERIAL_H_

#include "fasit.h"
#include "serial.h"

// class for using specifically FASIT over a Serial radio
// parses Radio messages, creates TCP messages
class FASIT_Serial : public SerialConnection, public FASIT {
public :
   FASIT_Serial(char *fname);
   virtual ~FASIT_Serial();
   int parseData(int size, char *buf); // returns -1 if needs to be deleted afterwards
   virtual int validMessage(int *start, int* end); // does the buffer contain valid data? message number on yes, 0 on no; sets start and end for where in the buffer it is

   virtual int handleEvent(epoll_event *ev); // called when either ready to read or write; returns -1 if needs to be deleted afterwards

private :
   static void defHeader(int mnum, FASIT_header *fhdr); // sets the message number, correct ICD major and minor version numbers, and reserved for the given message number

   // individual message handlers, all return -1 if needs to be deleted afterwards
   // the message data itself is in the read buffer from start to end
   int handle_100(int start, int end);
   int handle_2000(int start, int end);
   int handle_2004(int start, int end);
   int handle_2005(int start, int end);
   int handle_2006(int start, int end);
   int handle_2100(int start, int end);
   int handle_2101(int start, int end);
   int handle_2111(int start, int end);
   int handle_2102(int start, int end);
   int handle_2114(int start, int end);
   int handle_2115(int start, int end);
   int handle_2110(int start, int end);
   int handle_2112(int start, int end);
   int handle_2113(int start, int end);
   int handle_63556(int start, int end);
   int handle_42966(int start, int end);
   int handle_43061(int start, int end);
   int handle_47157(int start, int end);
   int handle_16000(int start, int end);
   int handle_16001(int start, int end);
   int handle_16002(int start, int end);
   int handle_16003(int start, int end);
   int handle_16004(int start, int end);
   int handle_16005(int start, int end);
   int handle_16006(int start, int end);
   int handle_16007(int start, int end);
   int handle_16008(int start, int end);

   // for commands 2101, 43061, and 47157
   int handle_as_2101(struct FASIT_2101b bmsg, class FASIT_TCP *tcp);

   // for determining whether or not we're ignore all commands
   bool ignoreAll; // actually ignores all but the re-enable command
};

#endif
