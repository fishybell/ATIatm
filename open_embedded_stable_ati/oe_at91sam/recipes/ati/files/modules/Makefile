obj-m := 	target.o \
			eeprom_settings.o \
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
			mover_ttmt.o \
			target_lifter_ttmt.o \
			target_mover_ttmt.o \
			target_generic_output.o \
			target_hit_poll.o \
			target_mover_hit_poll.o \
			nchs.o \
			scenario.o \

CURD := $(shell basename `pwd`)
ifeq (${CURD},modules)
  include files.mk
endif

BFILES :=bit_button user_conn fasit_conn event_conn bcast_server bcast_client expose conceal
default: ${BFILES} rf
	make ARCH=arm CROSS_COMPILE=arm-angstrom-linux-gnueabi- -C ${KDIR} SUBDIRS=${PWD}  modules
	
conceal: conceal.c netlink_shared.h netlink_user.h
	${CC} ${CFLAGS} ${LDFLAGS} ${CU_FLAGS} -lnl conceal.c -o conceal

expose: expose.c netlink_shared.h netlink_user.h
	${CC} ${CFLAGS} ${LDFLAGS} ${CU_FLAGS} -lnl expose.c -o expose

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

atifirmware: 
	date +mver=\"%Y%m%d\" > ../../../ati2/files/be_minor_version
	cat ../../../ati2/files/make_minor_version >> ../../../ati2/files/be_minor_version
	tar -cvjf atifirmware.tar --exclude=be_major_version *.ko *.arm ${BFILES} \
			../../../ati2/files/be_* \
			../../../ati2/files/has_* \
			../../../ati2/files/is_* \
			../../../ati2/files/load_* \
			../../../ati2/files/unload \
			../../../ati2/files/fixhost \
			../../../ati2/files/start_up \
			../../../ati2/files/start \
			../../../ati2/files/stop \
			../../../ati2/files/restart \
			../../../ati2/files/MAT \
			../../../ati2/files/MATOLD \
			../../../ati2/files/MIT \
			../../../ati2/files/MITV \
			../../../ati2/files/MITP \
			../../../ati2/files/SAT \
			../../../ati2/files/SES \
			../../../ati2/files/SIT \
			../../../ati2/files/EIM \
			../../../ati2/files/BASE \
			../../../ati2/files/HHC \
			../../../ati2/files/SITMT \
			../../../ati2/files/TTMT \
			../../../ati2/files/atiradiotest.sh \
			../../../ati2/files/evaluateip.sh \
			../../../ati2/files/networking \
			../../../ati2/files/K40networking \
			../../../ati2/files/interfaces \
			../../../sysvinit/sysvinit/inittab
	cat extract.sh atifirmware.tar > atifirmware.sh
	chmod +x atifirmware.sh

atibobber: 
	date +mver=\"%Y%m%d\" > ../../../ati2/files/be_minor_version
	cat ../../../ati2/files/make_minor_version >> ../../../ati2/files/be_minor_version
	tar -cvjf atibobber.tar --exclude=be_major_version *.ko *.arm ${BFILES} \
			./cgi \
			../../../ati2/files/be_* \
			../../../ati2/files/has_* \
			../../../ati2/files/is_* \
			../../../ati2/files/load_* \
			../../../ati2/files/unload \
			../../../ati2/files/fixhost \
			../../../ati2/files/start_up \
			../../../ati2/files/start \
			../../../ati2/files/stop \
			../../../ati2/files/restart \
			../../../ati2/files/MAT \
			../../../ati2/files/MATOLD \
			../../../ati2/files/MIT \
			../../../ati2/files/MITV \
			../../../ati2/files/MITP \
			../../../ati2/files/SAT \
			../../../ati2/files/SES \
			../../../ati2/files/SIT \
			../../../ati2/files/EIM \
			../../../ati2/files/BASE \
			../../../ati2/files/HHC \
			../../../ati2/files/SITMT \
			../../../ati2/files/TTMT \
			../../../ati2/files/atiradiotest.sh \
			../../../ati2/files/evaluateip.sh \
			../../../ati2/files/networking \
			../../../ati2/files/thttpd \
			../../../ati2/files/K40networking \
			../../../ati2/files/interfaces \
			../../../sysvinit/sysvinit/inittab
	cat extractbobber.sh atibobber.tar > atibobber.sh
	chmod +x atibobber.sh

rf:
	${MAKE} -C rf_conn/fasit
	${MAKE} -C rf_conn/radio_edit
	cp -v rf_conn/fasit/*.arm .
	cp -v rf_conn/radio_edit/*.arm .

clean:
	${RM} *.arm .*.cmd *.o *.ko -r .tmp* *.mod.c modules.order Module.symvers atifirmware.tar atifirmware.sh ${BFILES}
	${MAKE} -C fasit clean
	${MAKE} -C connector clean
	${MAKE} -C rf_conn/fasit clean
	${MAKE} -C rf_conn/radio_edit clean

