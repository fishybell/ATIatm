# Makefile for the RF system

SYSROOT=/home/brynn/ActionTarget/oe/oe/tmp/staging/armv5te-angstrom-linux-gnueabi
XGCC = /home/brynn/ActionTarget/oe/oe/tmp/cross/armv5te/bin/arm-angstrom-linux-gnueabi-gcc

default: mcp RFmaster RFmaster.arm

mcp : mcp.c minion.c mcp.h fasit_c.h
	gcc -lrt -std=gnu99 -o mcp mcp.c minion.c

RFmaster : RFmaster.c mcp.h fasit_c.h
	gcc -lrt -std=gnu99 -o RFmaster RFmaster.c

RFmaster.arm : RFmaster.c mcp.h fasit_c.h
	$(XGCC) --sysroot=$(SYSROOT) -lrt -std=gnu99 -o RFmaster.arm RFmaster.c


clean:
	$(RM) mcp RFmaster *.o RFmaster.arm

