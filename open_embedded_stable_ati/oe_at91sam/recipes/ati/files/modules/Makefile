obj-m := 	target.o \
			target_lifter_infantry.o \
			target_muzzle_flash.o \
			target_miles_transmitter.o \
			target_thermal.o \
			target_battery.o \
			target_hit_mechanical.o \
			target_hit_miles.o \
			target_ses_interface.o \
			target_user_interface.o \
			target_lifter_armor.o \
			target_mover_generic.o \
			netlink_provider.o \
			delay_printk.o \
			lifter.o \
			mover.o \
			target_generic_output.o \
			target_hit_poll.o \

CURD := $(shell basename `pwd`)
ifeq (${CURD},modules)
  include files.mk
endif

BFILES :=bit_button user_conn fasit_conn event_conn bcast_server bcast_client
default: ${BFILES}
	make ARCH=arm CROSS_COMPILE=arm-angstrom-linux-gnueabi- -C ${KDIR} SUBDIRS=${PWD}  modules
	
user_conn: user_conn.c netlink_shared.h netlink_user.h
	${CC} ${CFLAGS} ${LDFLAGS} ${CU_FLAGS} -lnl user_conn.c -o user_conn

bit_button: bit_button.c netlink_shared.h netlink_user.h
	${CC} ${CFLAGS} ${LDFLAGS} ${CU_FLAGS} -lnl bit_button.c -o bit_button

fasit_conn: ${FC_C_FILES} ${FC_H_FILES}
	${CXX} ${CFLAGS} ${LDFLAGS} -DFASIT_CONN ${CI_FLAGS} ${CU_FLAGS} -lnl ${FC_C_FILES} -o fasit_conn

event_conn: ${EC_C_FILES} ${EC_H_FILES}
	${CXX} ${CFLAGS} ${LDFLAGS} -DEVENT_CONN ${CI_FLAGS} ${CU_FLAGS} -lnl ${EC_C_FILES} -o event_conn

bcast_client: ${EC_DIR}/bcast_client.c ${EC_DIR}/bcast.h
	${CC} ${CFLAGS} ${LDFLAGS} ${CU_FLAGS} -I${EC_DIR} ${EC_DIR}/bcast_client.c -o bcast_client

bcast_server: ${EC_DIR}/bcast_server.c ${EC_DIR}/bcast.h
	${CC} ${CFLAGS} ${LDFLAGS} ${CU_FLAGS} -I${EC_DIR} ${EC_DIR}/bcast_server.c -o bcast_server

clean:
	${RM} .*.cmd *.o *.ko -r .tmp* *.mod.c modules.order Module.symvers ${BFILES}
	${MAKE} -C fasit clean
	${MAKE} -C connector clean

