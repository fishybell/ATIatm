#include "radio.h"
#include "common.h"
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
   // TODO -- exit programming mode
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
   char *cmd1 = "*0202B520\n"; // write (*02) address 0x02B5 with 0x20
   int ret;
   do {
      ret = write(fd, cmd1, 10);
   } while (ret == -1 && errno == EAGAIN); // retry writing
   if (ret != 10) {
FUNCTION_INT("::changeChannel(int channel", -1)
      return -1;
   }

   // ignore answer

   // change channels
   char *cmd2 = "*0202B800\n"; // write (*02) address 0x02B8 with 0x00
   cmd2[8] += (channel - 1); // convert to ascii version of zero based hex number
   int ret;
   do {
      ret = write(fd, cmd2, 10);
   } while (ret == -1 && errno == EAGAIN); // retry writing
   if (ret != 10) {
FUNCTION_INT("::changeChannel(int channel", -1)
      return -1;
   }

   // ignore answer

FUNCTION_INT("::changeChannel(int channel", 0)
   return 0;
}
