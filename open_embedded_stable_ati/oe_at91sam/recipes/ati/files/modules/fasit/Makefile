# Makefile for Action Target specific files

all: fasit_conn

fasit_conn: connection.cpp  epoll.cpp  fasit.cpp  fasit_tcp.cpp  tcp_client.cpp tcp_factory.cpp  timeout.cpp  timer.cpp  timers.cpp common.h  connection.h  fasit.h  fasit_tcp.h  tcp_client.h tcp_factory.h  timeout.h  timer.h  timers.h
	${CXX} ${CFLAGS} ${LDFLAGS} -g connection.cpp  epoll.cpp  fasit.cpp  fasit_tcp.cpp  tcp_client.cpp tcp_factory.cpp  timeout.cpp  timer.cpp  timers.cpp -o fasit_conn

clean:
	rm -rv fasit_conn
