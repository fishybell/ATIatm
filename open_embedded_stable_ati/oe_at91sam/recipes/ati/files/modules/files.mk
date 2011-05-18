# file declarations for the various directories

PWD := $(shell pwd)
CURD := $(shell basename `pwd`)
U_FLAGS :=-I/usr/local/var/oe/tmp/work/at91sam9g20ek_2mmc-angstrom-linux-gnueabi/linux-2.6.30-r1/linux-2.6.30/include -g

ifeq (${CURD},modules)
  FC_DIR := ${PWD}/fasit
  EC_DIR := ${PWD}/connector
  NL_DIR := ${PWD}
  CU_FLAGS := ${U_FLAGS}
  CC := arm-angstrom-linux-gnueabi-gcc
  CXX := arm-angstrom-linux-gnueabi-g++
else
  ifeq (${CURD},fasit)
    FC_DIR := ${PWD}
    EC_DIR := ${PWD}/../connector
    NL_DIR := ${PWD}/..
    CU_FLAGS := ${U_FLAGS}
    CC := arm-angstrom-linux-gnueabi-gcc
    CXX := arm-angstrom-linux-gnueabi-g++
  else
    ifeq (${CURD},connector)
      FC_DIR := ${PWD}/../fasit
      EC_DIR := ${PWD}
      NL_DIR := ${PWD}/..
      CU_FLAGS := ${U_FLAGS}
      CC := arm-angstrom-linux-gnueabi-gcc
      CXX := arm-angstrom-linux-gnueabi-g++
    else
      FC_DIR := ${PWD}/fasit
      EC_DIR := ${PWD}/connector
      NL_DIR := ${PWD}
      CU_FLAGS :=
    endif
  endif
endif

CI_FLAGS :=-I${FC_DIR} -I${EC_DIR} -I${NL_DIR}

FC_C_FILES := ${FC_DIR}/connection.cpp ${FC_DIR}/epoll.cpp ${FC_DIR}/fasit.cpp ${FC_DIR}/fasit_tcp.cpp ${FC_DIR}/mit_client.cpp ${FC_DIR}/nl_conn.cpp ${FC_DIR}/sit_client.cpp ${FC_DIR}/tcp_client.cpp ${FC_DIR}/tcp_factory.cpp ${FC_DIR}/timeout.cpp ${FC_DIR}/timer.cpp ${FC_DIR}/timers.cpp
FC_H_FILES := ${FC_DIR}/common.h ${FC_DIR}/connection.h ${FC_DIR}/fasit.h ${FC_DIR}/fasit_tcp.h ${FC_DIR}/mit_client.h ${FC_DIR}/nl_conn.h ${FC_DIR}/sit_client.h ${FC_DIR}/tcp_client.h ${FC_DIR}/tcp_factory.h ${FC_DIR}/timeout.h ${FC_DIR}/timer.h ${FC_DIR}/timers.h

EC_C_FILES := ${EC_DIR}/kernel_tcp.cpp ${EC_DIR}/epoll.cpp ${EC_DIR}/timers.cpp ${FC_DIR}/connection.cpp ${FC_DIR}/nl_conn.cpp ${FC_DIR}/tcp_factory.cpp ${FC_DIR}/timeout.cpp ${FC_DIR}/timer.cpp
EC_H_FILES := ${EC_DIR}/kernel_tcp.h ${EC_DIR}/timers.h ${FC_DIR}/common.h ${FC_DIR}/connection.h ${FC_DIR}/nl_conn.h ${FC_DIR}/tcp_factory.h ${FC_DIR}/timeout.h ${FC_DIR}/timer.h 


