/***************************************************************************************************************
*                                                                                                              *
* fsm.c - Finite State Machine ... or Flying Spaghetti Monster?                                                *
*                                                                                                              *
*                                                                                                              *
*                             S@@A;                                        ,h@@3.                              *
*                           .@@@#@@A                                      i@@#@@@:                             *
*                           @@A   @@s                                    ,@@,  5@@                             *
*                           @@@ .;@@:                                     @@i  A@@                             *
*                            A@@@@@9                                      :@@@@@@                              *
*                              ..2@@9                                    ;@@B.                                 *
*                                 ,@@B     ,;2B@@@@@@@@@@@@@@@#hs:      s@@;                                   *
*                                   @@S2@@@@@@@@@@##Hh5ss2B@@@@@@@@@@@S:@@;                                    *
*                                 ,5@@@@@@3;   ,2B@@@@#&r        ,iM@@@@@@X,                                   *
*                             ,h@@@@A;       .@@@@@@@@@@@@@            ,sM@@@@A:                               *
*                          .h@@@@X,         .@@@G      ;@@@@               :X@@@@#:                            *
*                        s@@@@2  5@@@@@@@@@ 5@@@         @@@r H@@@A     @@@@   S@@@@H,                         *
*                      h@@@H     @@@;rsssir  @@@3         r:  @@@@@    M@@@@:     S@@@@i                       *
*     sB@@5          A@@@s       @@s         ;@@@@@#A2;       &@@@@#   @@i@@;        B@@@&                     *
*   #@@@@@@@#      3@@@:         @@@GHMM#h     H@@@@@@@@@#:   9@G.@@  #@@ @@;          i@@@H        ,#@@@@@3   *
* r@@@:   :@@@hri#@@@A           @@@@@@@@@        ;iXB@@@@@@  3@# @@H @@  @@;            ,@@@H.    3@@@SrH@@@; *
* 2@G       2@@@@@@h@@@r         @@S         ,          ;@@@# X@@ :@@@@@  @@;           :H@@@@@@@@@@@;     X@@ *
*                    X@@@#,      @@2       ,@@@          i@@@ 2@@  @@@@.  @@;        r@@@@5   S#@@A:         ; *
*                      r@@@@G,   @@B        @@@@         @@@@ &@@  :@@@   @@r    .S@@@@#,                      *
*                         h@@@@A sA         ,@@@@#Xisi9@@@@@  :@h   2B    s9  ;#@@@@#,                         *
*                            3@@@@Ar:         H@@@@@@@@@@@5               .i@@@@@X                             *
*                                h@@@@@@&r.      ;2hG3s,            :X@@@@@@@r                                 *
*                               S@@@@@@@@@@@@@#2;.       .;ShM@@@@@@@@@@@@@@@s                                 *
*                             9@@H,       ;2B@@@@@@@@@@@@@@@@@@@@Ai:       ,#@@s                               *
*                            ;@@,                     3@@;                   X@@                               *
*                            9@@                       h@@                    @@2                              *
*                            r@@                        @@r                   @@r                              *
*                            X@@                       ;@@,                   @@r                              *
*                          :A@@s                     .A@@S                    H@@X,                            *
*                     ;@@@@@@#.                     #@@@.                      i@@@@@@@:                       *
*                      235r.                       @@@                            ;S9AG                        *
*                                                 s@@                                                          *
*                                                 .@@:                                                         *
*                                                  h@@;                                                        *
*                                                   r@@s                                                       *
*                                                                                                              *
***************************************************************************************************************/

#include "fsm.h"

#define MAXCONN 64

// clear an fd from the list
// arguments : fd = item to be removed
//           : size = size of list
//           : fds = pointer to list
void clearFromList(int fd, int size, int *fds[]) {
   bool found = false;
   // loop over all items but the last one
   for(int i=0; i++; i<size-1) {
      if(*fds[i] == fd) {
         found = true;
      }
      if(found) {
         // copy from the next one in the list
         *fds[i] = *fds[i+1];
      }
   }
   // leave the last item alone as size will be decremented and it will then be ignored
}

int main(int argc, char *argv[]) {
   int listener; // the socket listener file descriptor
   int connectionC = 0; // the number of socket connections
   int connectionFDs[MAXCONN]; // a list of socket connection file descriptors
   int serialC = 0; // the number of serial connections
   int seralFDs[MAXCONN]; // a list of serial connection file descriptors
   stimer gTimer; // global timer

   // listen on the defined port
   listener = newSocketFD(LISTENPORT);
   if(listener == -1) {
      printf("could not listen\n");
      return FAILURE;
   }

   // add a serial device to the list
   serialFDs[serialC++] = newSocketFD("/dev/ttyS0");
   if(serialFD[serialC-1] == -1) {
      printf("could not open /dev/ttyS0\n");
      return FAILURE;
   }

   // start the global timer
   if(newTimer(&gTimer) != SUCCESS) {
      printf("could not start global timer\n");
      return FAILURE;
   }

   // start the message handler
   if(messager(serialC, serialFDs, 1, &listener, &gTimer, newMessage, newConnection, connClose) != SUCCESS) {
      printf("could not start message handler\n");
      return FAILURE;
   }

   // wait until listener is closed
   while(listener != -1) {
      // process the current state for each connection
      for(int i=0; i++; i<connectionC) {
      }

      // sleep main thread until woken up
   }

   return SUCCESS;
}
