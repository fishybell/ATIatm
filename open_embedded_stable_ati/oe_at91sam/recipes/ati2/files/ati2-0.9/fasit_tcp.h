#ifndef _FASIT_TCP_H_
#define _FASIT_TCP_H_

#include "fasit.h"
#include "connection.h"
#include <vector>
#include <list>
#include <map>
#include "common.h"

// class for using specifically FASIT over TCP
// parses TCP messages, creates Radio messages
class FASIT_TCP : public Connection, public FASIT {
public :
   FASIT_TCP(int fd);
   FASIT_TCP(int fd, int tnum);
   virtual ~FASIT_TCP();
   int parseData(int size, char *buf); // returns -1 if needs to be deleted afterwards
   virtual int validMessage(int *start, int *end); // does the buffer contain valid data? message number on yes, 0 on no; sets start and end for where in the buffer it is
   struct FASIT_RESPONSE getResponse(int mnum); // get a FASIT_RESPONSE for the given message number
   int getSequence() { return seq++; } // get the next sequence number

   // for handling of the potentially multi-message 2100 message
   static void Bake_2100();

   // for handling of the super-compact 2102 message
   int getMoveReq() { return moveReq; }
   void setMoveReq(int moveReq) { this->moveReq = moveReq; }
   struct FASIT_2102h getHitReq();
   void setHitReq(struct FASIT_2102h hitReq) { this->hitReq = hitReq; }

   // for handling of the multi-part 2006 message
   struct FASIT_2006b get2006base() {return zbase;}
   void set2006base(struct FASIT_2006b base) {zbase = base;};
   vector<struct FASIT_2006z> *get2006zones() {return &zones;}; // returns pointer so the caller can add to list or loop over list
   void clearZones() {zones.clear();};

   virtual int handleEvent(epoll_event *ev); // called when either ready to read or write; returns -1 if needs to be deleted afterwards
   
private :
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

   int seq; // incrimenting sequence to send with each message
   void seqForResp(int mnum, int seq);
   map<int,int> respMap; // map of messages to sequences

   // for handling of the super-compact 2102 message
   int moveReq; // requested move direction
   struct FASIT_2102h hitReq; // requested hit sensor configuration

   // for handling of the multi-part 2006 message
   struct FASIT_2006b zbase;
   vector<struct FASIT_2006z> zones;

   // for handling of the potentially multi-message 2100 message
   static list<ATI_2100m> commandList; // for keeping the order of messages
   static multimap<ATI_2100m, int, struct_comp<struct ATI_2100m> > commandMap; // for keeping track of all destinations
};

#endif
