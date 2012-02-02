# Makefile for the RF system
#
#   the root of the oe directory will probably have to change if your not me
#

OE =/home/brynn/ActionTarget/oe/oe

SYSROOT=$(OE)/tmp/staging/armv5te-angstrom-linux-gnueabi
XGCC = $(OE)/tmp/cross/armv5te/bin/arm-angstrom-linux-gnueabi-gcc

default: mcp RFmaster RFmaster.arm RFslave RFslave.arm

mcp : mcp.c minion.c mcp.h fasit_c.h
	gcc -lrt -std=gnu99 -o mcp mcp.c minion.c

RFmaster : RFmaster.c mcp.h fasit_c.h serial.c
	gcc -lrt -std=gnu99 -o RFmaster RFmaster.c serial.c

RFmaster.arm : RFmaster.c mcp.h fasit_c.h
	$(XGCC) --sysroot=$(SYSROOT) -lrt -std=gnu99 -o RFmaster.arm RFmaster.c serial.c

RFslave : RFslave.c mcp.h fasit_c.h serial.c
	gcc -lrt -std=gnu99 -o RFslave RFslave.c serial.c

RFslave.arm : RFslave.c mcp.h fasit_c.h serial.c
	$(XGCC) --sysroot=$(SYSROOT) -lrt -std=gnu99 -o RFslave.arm RFslave.c serial.c

clean:
	$(RM) mcp *.o RFmaster RFmaster.arm RFslave RFslave.arm

