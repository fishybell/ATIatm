# Makefile for Action Target specific files


radio_conv: connection.cpp  epoll.cpp  fasit.cpp  fasit_serial.cpp  fasit_tcp.cpp  radio.cpp  serial.cpp  tcp_factory.cpp  timeout.cpp common.h  connection.h  fasit.h  fasit_serial.h  fasit_tcp.h  radio.h  serial.h  tcp_factory.h  timeout.h
	${CXX} ${CFLAGS} ${LDFLAGS} -g connection.cpp  epoll.cpp  fasit.cpp  fasit_serial.cpp  fasit_tcp.cpp  radio.cpp  serial.cpp  tcp_factory.cpp  timeout.cpp -o radio_conv

all: radio_conv

clean:
	rm -rv radio_conv
