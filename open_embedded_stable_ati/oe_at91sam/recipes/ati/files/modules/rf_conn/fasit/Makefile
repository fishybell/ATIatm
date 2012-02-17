# Makefile for the RF system
#
#   the root of the oe directory will probably have to change if your not me
#

OE =/home/brynn/ActionTarget/oe/oe

SYSROOT=$(OE)/tmp/staging/armv5te-angstrom-linux-gnueabi
XGCC = $(OE)/tmp/cross/armv5te/bin/arm-angstrom-linux-gnueabi-gcc

HFILES = mcp.h fasit_c.h
MCFILES = serial.c timestamp.c htonll.c rf.c


default: mcp RFmaster RFmaster.arm RFslave RFslave.arm RFdump.arm

mcp : mcp.c minion.c $(HFILES) $(MCFILES)
	gcc -lrt -std=gnu99 -o mcp mcp.c minion.c $(MCFILES)

RFmaster : RFmaster.c $(HFILES) $(MCFILES)
	gcc -lrt -std=gnu99 -o RFmaster RFmaster.c $(MCFILES)

RFmaster.arm : RFmaster.c $(HFILES) $(MCFILES)
	$(XGCC) --sysroot=$(SYSROOT) -lrt -std=gnu99 -o RFmaster.arm RFmaster.c $(MCFILES)

RFslave : RFslave.c $(HFILES) $(MCFILES)
	gcc -lrt -std=gnu99 -o RFslave RFslave.c $(MCFILES)

RFslave.arm : RFslave.c $(HFILES) $(MCFILES)
	$(XGCC) --sysroot=$(SYSROOT) -lrt -std=gnu99 -o RFslave.arm RFslave.c $(MCFILES)

RFdump.arm : RFdump.c $(HFILES) $(MCFILES)
	$(XGCC) --sysroot=$(SYSROOT) -lrt -std=gnu99 -o RFdump.arm RFdump.c $(MCFILES)

clean:
	$(RM) mcp *.o RFmaster RFmaster.arm RFslave RFslave.arm RFdump.arm

