# Makefile for Action Target specific files

all: radio_conv eeprom_rw

i2c_test: i2c_test.c
	${CC} ${CFLAGS} ${LDFLAGS} i2c_test.c -o i2c_test

eeprom_rw: eeprom.c
	${CC} ${CFLAGS} ${LDFLAGS} eeprom.c -o eeprom_rw

radio_conv: connection.cpp  epoll.cpp  fasit.cpp  fasit_serial.cpp  fasit_tcp.cpp  radio.cpp  serial.cpp  tcp_factory.cpp  timeout.cpp  timer.cpp  timers.cpp common.h  connection.h  fasit.h  fasit_serial.h  fasit_tcp.h  radio.h  serial.h  tcp_factory.h  timeout.h  timer.h  timers.h
	${CXX} ${CFLAGS} ${LDFLAGS} -g connection.cpp  epoll.cpp  fasit.cpp  fasit_serial.cpp  fasit_tcp.cpp  radio.cpp  serial.cpp  tcp_factory.cpp  timeout.cpp  timer.cpp  timers.cpp -o radio_conv

clean:
	rm -rv radio_conv eeprom_rw
