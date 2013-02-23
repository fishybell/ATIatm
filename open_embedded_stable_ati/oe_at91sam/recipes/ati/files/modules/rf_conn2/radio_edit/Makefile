# Makefile for the radio programming system (currently only for the DTXM from Ritron)
#
#   the root of the oe directory will probably have to change if your not me
#

#OE =/home/brynn/ActionTarget/oe/oe
OE = /usr/local/var/oe

FASIT=../fasit
SYSROOT=$(OE)/tmp/staging/armv5te-angstrom-linux-gnueabi
XGCC = $(OE)/tmp/cross/armv5te/bin/arm-angstrom-linux-gnueabi-gcc

HFILES = $(FASIT)/mcp.h $(FASIT)/rf.h eeprom.h ../../defaults.h $(FASIT)/colors.h
MCFILES = $(FASIT)/serial.c $(FASIT)/timestamp.c $(FASIT)/htonll.c $(FASIT)/rf.c eeprom.c

default: dtxm_edit dtxm_edit.arm

dtxm_edit : dtxm_edit.c $(HFILES) $(MCFILES)
	gcc -lrt -I$(FASIT) -std=gnu99 -o dtxm_edit dtxm_edit.c $(MCFILES)

dtxm_edit.arm : dtxm_edit.c $(HFILES) $(MCFILES)
	$(XGCC) ${CFLAGS} ${LDFLAGS} --sysroot=$(SYSROOT) -I$(FASIT) -lrt -std=gnu99 -o dtxm_edit.arm dtxm_edit.c $(MCFILES)

clean:
	$(RM) dtxm_edit *.arm *.o

