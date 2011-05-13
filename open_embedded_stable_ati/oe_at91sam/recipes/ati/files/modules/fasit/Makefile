# Makefile for Action Target specific files

CURD := $(shell basename `pwd`)
CU_FLAGS :=-I/usr/local/var/oe/tmp/work/at91sam9g20ek_2mmc-angstrom-linux-gnueabi/linux-2.6.30-r1/linux-2.6.30/include

C_FILES := connection.cpp epoll.cpp fasit.cpp fasit_tcp.cpp mit_client.cpp nl_conn.cpp sit_client.cpp tcp_client.cpp tcp_factory.cpp timeout.cpp timer.cpp timers.cpp
H_FILES := common.h connection.h fasit.h fasit_tcp.h mit_client.h nl_conn.h sit_client.h tcp_client.h tcp_factory.h timeout.h timer.h timers.h 

all: fasit_conn

fasit_conn: ${C_FILES} ${H_FILES}
    ifeq ($(CURD),fasit)
		arm-angstrom-linux-gnueabi-g++ -DFASIT_CONN ${CFLAGS} ${LDFLAGS} -I../ ${CU_FLAGS} -g -lnl ${C_FILES} -o fasit_conn
    else
		${CXX} ${CFLAGS} ${LDFLAGS} -DFASIT_CONN -I../ -g -lnl ${C_FILES} -o fasit_conn
    endif

clean:
	rm -rv fasit_conn
