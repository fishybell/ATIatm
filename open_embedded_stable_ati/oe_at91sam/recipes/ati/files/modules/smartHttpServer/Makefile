# Makefile for the RF system
#
#   the root of the oe directory will probably have to change if your not me
#

#OE =/home/brynn/ActionTarget/oe/oe
OE = /home/randyn/Angstrom/oe

SYSROOT=$(OE)/tmp/staging/armv5te-angstrom-linux-gnueabi
XGCC = $(OE)/tmp/cross/armv5te/bin/arm-angstrom-linux-gnueabi-g++

default: smartHttpServer smartHttpServer.arm

smartHttpServer : smartHttpServer.c
	g++ -lrt ${CFLAGS} -o smartHttpServer smartHttpServer.c

smartHttpServer.arm : smartHttpServer.c
	$(XGCC) -DBOARDSERIAL ${CFLAGS} ${LDFLAGS} --sysroot=$(SYSROOT) -lrt -o smartHttpServer.arm smartHttpServer.c

getDatabaseData.arm : getDatabaseData.c ../smartController/atiSqlite.cpp ../smartController/atiSqlite.h
	$(XGCC) -DBOARDSERIAL ${CFLAGS} ${LDFLAGS} --sysroot=$(SYSROOT) -lrt -o getDatabaseData.arm getDatabaseData.c ../smartController/atiSqlite.cpp -lsqlite3

bobberUpdate.arm : bobberUpdate.c
	$(XGCC) -DBOARDSERIAL ${CFLAGS} ${LDFLAGS} --sysroot=$(SYSROOT) -lrt -o bobberUpdate.arm bobberUpdate.c
	cp bobberUpdate.arm ../cgi/www/cgi-bin

smartController.arm : smartController.c atiTarget.cpp atiTarget.h atiSqlite.cpp atiSqlite.h atiGroup.cpp atiGroup.h $(HFILES) $(MCFILES)
	$(XGCC) -DBOARDSERIAL ${CFLAGS} ${LDFLAGS} --sysroot=$(SYSROOT) -lrt -o smartController.arm smartController.c atiTarget.cpp atiSqlite.cpp atiGroup.cpp -lsqlite3

clean:
	$(RM) *.o smartHttpServer *.arm getDatabaseData

