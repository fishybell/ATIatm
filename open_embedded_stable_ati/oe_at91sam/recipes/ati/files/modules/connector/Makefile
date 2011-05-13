# Makefile for Action Target specific files

CURD := $(shell basename `pwd`)
CU_FLAGS :=-I/usr/local/var/oe/tmp/work/at91sam9g20ek_2mmc-angstrom-linux-gnueabi/linux-2.6.30-r1/linux-2.6.30/include

C_FILES := kernel_tcp.cpp epoll.cpp timers.cpp ../fasit/connection.cpp ../fasit/nl_conn.cpp ../fasit/tcp_factory.cpp ../fasit/timeout.cpp ../fasit/timer.cpp
H_FILES := kernel_tcp.h timers.h ../fasit/common.h ../fasit/connection.h ../fasit/nl_conn.h ../fasit/tcp_factory.h ../fasit/timeout.h ../fasit/timer.h 

all: event_conn

event_conn: ${C_FILES} ${H_FILES}
    ifeq ($(CURD),fasit)
		arm-angstrom-linux-gnueabi-g++ -DEVENT_CONN ${CFLAGS} ${LDFLAGS} -I../ -I../fasit/ -I../connector/ ${CU_FLAGS} -g -lnl ${C_FILES} -o event_conn
    else
		${CXX} ${CFLAGS} ${LDFLAGS} -DEVENT_CONN -I../ -I../fasit/ -I../connector/ -g -lnl ${C_FILES} -o event_conn
    endif

clean:
	rm -rv event_conn
