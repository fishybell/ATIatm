#include "radio.h"
#include "common.h"
#include <errno.h>
#include <unistd.h>

Radio::Radio(int fd) {
FUNCTION_START("::Radio(int fd)")
   this->fd = fd;

   // enter programming mode (three plusses with one second delay between each)
   for (int i=0; i<3; i++) {
      int ret = write(fd, "+", 1);
      switch (ret) {
         case 0 :
            i--;
            // fall through
         case 1 :
            sleep(1);
            break;
         default :
            if (errno == EAGAIN) {
               i--;
               sleep(1);
            }
            // if we're another error, the programming commands won't work anyway, so ignore and move on
            break;
      }
   }
   
FUNCTION_END("::Radio(int fd)")
}

Radio::~Radio() {
FUNCTION_START("::~Radio()")
   // exit programming mode (resets radio)
   write(fd, "*00\r", 4);
   sleep(1);
FUNCTION_END("::~Radio()")
}

int Radio::changeChannel(int channel) {
FUNCTION_START("::changeChannel(int channel")

   // check for valid channel
   if (channel < 1 || channel > 8) {
FUNCTION_INT("::changeChannel(int channel", -1)
      return -1;
   }

   // turn on software selectable channels
   const char *cmd1 = "*0202B520\r"; // write (*02) address 0x02B5 with 0x20
   int ret;
   do {
      ret = write(fd, cmd1, 10);
   } while (ret == -1 && errno == EAGAIN); // retry writing
   if (ret != 10) {
FUNCTION_INT("::changeChannel(int channel", -1)
      return -1;
   }

   // TODO -- write to EEROM instead of RAM and read result to verify

   // change channels
   char cmd2[11] = "*0202B800\r"; // write (*02) address 0x02B8 with 0x00
   cmd2[8] += (channel - 1); // convert to ascii version of zero based hex number
   do {
      ret = write(fd, cmd2, 10);
   } while (ret == -1 && errno == EAGAIN); // retry writing
   if (ret != 10) {
FUNCTION_INT("::changeChannel(int channel", -1)
      return -1;
   }

   // TODO -- write to EEROM instead of RAM and read result to verify

FUNCTION_INT("::changeChannel(int channel", 0)
   return 0;
}
