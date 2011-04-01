#ifndef DELAY_PRINTK_H
#define DELAY_PRINTK_H

// printk function that is callable from an interrupt
//   will not cause delays to other interrupts as the 
//   message is queued up and printed in a different
//   context
extern signed int delay_printk(const char *pFormat, ...);

#endif
