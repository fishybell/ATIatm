# Makefile for the RF system
#
#   the root of the oe directory will probably have to change if your not me
#

#OE =/home/brynn/ActionTarget/oe/oe
OE = /usr/local/var/oe

SYSROOT=$(OE)/tmp/staging/armv5te-angstrom-linux-gnueabi
XGCC = $(OE)/tmp/cross/armv5te/bin/arm-angstrom-linux-gnueabi-gcc

HFILES = mcp.h fasit_c.h rf.h colors.h
MCFILES = serial.c timestamp.c htonll.c rf.c md5sum.c



#default: mcp RFmaster RFmaster.arm RFslave RFslave.arm RFdump RFdump.arm
default: test_md5sum qtest mcp RFmaster RFmaster.arm RFdump RFdump.arm RFslave.new RFslave.new.arm slaveboss.arm

qtest : qtest.c $(HFILES) $(MCFILES)
	gcc -lrt -std=gnu99 -o qtest qtest.c $(MCFILES)

test_md5sum : test_md5sum.c md5sum.c
	gcc -lrt -std=gnu99 -o test_md5sum test_md5sum.c md5sum.c

mcp : mcp.c minion_state.c minion.c $(HFILES) $(MCFILES)
	gcc -lrt -std=gnu99 -o mcp mcp.c minion_state.c minion.c $(MCFILES)

mcp.arm : mcp.c minion_state.c minion.c $(HFILES) $(MCFILES)
	$(XGCC) -D ATMEL_ARM --sysroot=$(SYSROOT) -lrt -std=gnu99 -o mcp.arm mcp.c minion_state.c minion.c $(MCFILES)

mcp.exe : mcp.c minion_state.c minion.c $(HFILES) $(MCFILES)
	gcc -lrt -std=gnu99 -o mcp.exe mcp.c minion_state.c minion.c $(MCFILES)

RFmaster : RFmaster.c $(HFILES) $(MCFILES)
	gcc -lrt -std=gnu99 -o RFmaster RFmaster.c $(MCFILES)

RFmaster.arm : RFmaster.c $(HFILES) $(MCFILES)
	$(XGCC) --sysroot=$(SYSROOT) -lrt -std=gnu99 -o RFmaster.arm RFmaster.c $(MCFILES)

RFslave.new : RFslave_main.c RFslave_data.c RFslave.h fasit_debug.c rf_debug.c fasit_debug.h rf_debug.h $(HFILES) $(MCFILES)
	gcc -lrt -std=gnu99 -o RFslave.new RFslave_main.c RFslave_data.c fasit_debug.c rf_debug.c $(MCFILES)

RFslave.new.arm : RFslave_main.c RFslave_data.c RFslave.h fasit_debug.c rf_debug.c fasit_debug.h rf_debug.h $(HFILES) $(MCFILES)
	$(XGCC) --sysroot=$(SYSROOT) -lrt -std=gnu99 -o RFslave.new.arm RFslave_main.c RFslave_data.c fasit_debug.c rf_debug.c $(MCFILES)

RFslave : RFslave.c $(HFILES) $(MCFILES)
	gcc -lrt -std=gnu99 -o RFslave RFslave.c $(MCFILES)

RFslave.arm : RFslave.c $(HFILES) $(MCFILES)
	$(XGCC) --sysroot=$(SYSROOT) -lrt -std=gnu99 -o RFslave.arm RFslave.c $(MCFILES)

slaveboss : slaveboss.c slaveboss_fasit.c slaveboss_rf.c eeprom.c fasit_debug.c rf_debug.c fasit_debug.h rf_debug.h $(HFILES) $(MCFILES) slaveboss.h
	gcc -lrt -std=gnu99 -o slaveboss fasit.c slaveboss.c slaveboss_fasit.c slaveboss_rf.c fasit_debug.c rf_debug.c eeprom.c $(MCFILES)

slavetest : slaveboss.h slavetest.c fasit_debug.c rf_debug.c fasit_debug.h rf_debug.h $(HFILES) $(MCFILES) slaveboss.h
	gcc -lrt -std=gnu99 -o slavetest slavetest.c fasit_debug.c rf_debug.c $(MCFILES)

slaveboss.arm : slaveboss.c slaveboss_fasit.c slaveboss_rf.c eeprom.c fasit_debug.c rf_debug.c fasit_debug.h rf_debug.h $(HFILES) $(MCFILES) slaveboss.h
	$(XGCC) --sysroot=$(SYSROOT) -lrt -std=gnu99 -o slaveboss.arm fasit.c slaveboss.c slaveboss_fasit.c slaveboss_rf.c eeprom.c fasit_debug.c rf_debug.c $(MCFILES)

RFdump.arm : RFdump.c $(HFILES) $(MCFILES)
	$(XGCC) --sysroot=$(SYSROOT) -lrt -std=gnu99 -o RFdump.arm RFdump.c $(MCFILES)

RFdump : RFdump.c $(HFILES) $(MCFILES)
	gcc -lrt -std=gnu99 -o RFdump RFdump.c $(MCFILES)

clean:
	$(RM) mcp *.o RFmaster RFmaster.arm RFslave RFslave.arm RFdump RFdump.arm RFslave.new RFslave.new.arm slavetest slavetest.arm slaveboss slaveboss.arm

