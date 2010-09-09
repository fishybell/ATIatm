# Makefile for Action Target specific files


radio_conv: connection.cpp  epoll.cpp  fasit.cpp  fasit_serial.cpp  fasit_tcp.cpp  serial.cpp  tcp_factory.cpp  timeout.cpp
	${CXX} ${CFLAGS} ${LDFLAGS} connection.cpp  epoll.cpp  fasit.cpp  fasit_serial.cpp  fasit_tcp.cpp  serial.cpp  tcp_factory.cpp  timeout.cpp -o radio_conv

all: radio_conv

clean:
	rm -rv radio_conv
