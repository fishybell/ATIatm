#include "tcp_client.h"

#ifndef _FASIT_TCP_H_
#define _FASIT_TCP_H_

#include <map>

using namespace std;

// class for FASIT server
// parses TCP messages
class FASIT_TCP : public Connection, public FASIT {
public :
   FASIT_TCP(int fd, int tnum); // when initializing as a client
   FASIT_TCP(int fd);
   virtual ~FASIT_TCP();

   void newClient(); // creates a new TCP_Client object for this server instance
   virtual int parseData(int size, const char *buf); // returns -1 if needs to be deleted afterwards
   int validMessage(int *start, int *end); // does the buffer contain valid data? message number on yes, 0 on no; sets start and end for where in the buffer it is
   int getSequence() { return seq++; } // get the next sequence number
   void defHeader(int mnum, struct FASIT_header *fhdr); // fils out the header with basic information

   static void clearSubscribers(); // clears out all tcp connections on non-base station units

   // the client has been lost, kill this server instance
   void clientLost() { client = NULL; deleteLater(); };

protected:
   // the correspsonding client connection for this server instance
   class TCP_Client *client;

   // individual message handlers, all return -1 if the connectionneeds to be
   //   deleted afterwards
   // the message data itself is in the read buffer from start to end
   virtual int handle_100(int start, int end);
   virtual int handle_2000(int start, int end);
   virtual int handle_2004(int start, int end);
   virtual int handle_2005(int start, int end);
   virtual int handle_2006(int start, int end);
   virtual int handle_2100(int start, int end);
   virtual int handle_2101(int start, int end);
   virtual int handle_2111(int start, int end);
   virtual int handle_2102(int start, int end);
   virtual int handle_2114(int start, int end);
   virtual int handle_2115(int start, int end);
   virtual int handle_2110(int start, int end);
   virtual int handle_2112(int start, int end);
   virtual int handle_2113(int start, int end);
   virtual int handle_13110(int start, int end);
   virtual int handle_13112(int start, int end);
   virtual int handle_14110(int start, int end);
   virtual int handle_14112(int start, int end);
   virtual int handle_14400(int start, int end);
   virtual int handle_14401(int start, int end);

   // FASIT helper functions
   virtual int send_2101_ACK(FASIT_header *hdr, int response);

   // the correspsonding client connection for this server instance
   virtual bool hasPair() { return client != NULL; };
   virtual Connection *pair() { return (Connection*)client; }

   int seq; // incrementing sequence to send with each message
   struct FASIT_RESPONSE getResponse(int mnum); // get a FASIT_RESPONSE for the given message number
   void seqForResp(int mnum, int seq);
   map<int,int> respMap; // map of messages to sequences
};

#endif
