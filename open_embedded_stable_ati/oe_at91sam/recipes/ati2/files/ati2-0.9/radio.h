#ifndef _RADIO_H_
#define _RADIO_H_

class Radio {
public :
   Radio(int fd); // BLOCKS: switches the radio on the given file descriptor to programming mode (takes at least 3 seconds)
   ~Radio(); // BLOCKS: returns the radio to modem mode (may take several seconds)

   int changeChannel(int channel); // channel 1 through 8 may be selected (returns 0 on success)

private :
   int fd; // connection to already active serial device
};

#endif
