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

PWD := $(shell pwd)
CURD := $(shell basename `pwd`)
#EXTRA_CFLAGS += -mfloat-abi=softfp -mfpu=vfp
CU_FLAGS :=-I/usr/local/var/oe/tmp/work/at91sam9g20ek_2mmc-angstrom-linux-gnueabi/linux-2.6.30-r1/linux-2.6.30/include

default: bit_button user_conn
#	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
	make ARCH=arm CROSS_COMPILE=arm-angstrom-linux-gnueabi- -C $(KDIR) SUBDIRS=$(PWD)  modules
	
#export	EXTRA_CFLAGS

user_conn: user_conn.c netlink_shared.h netlink_user.h
    ifeq ($(CURD),modules)
		arm-angstrom-linux-gnueabi-gcc ${CFLAGS} ${LDFLAGS} ${CU_FLAGS} -g -lnl user_conn.c -o user_conn
    else
		${CC} ${CFLAGS} ${LDFLAGS} -lnl user_conn.c -o user_conn
    endif

bit_button: bit_button.c netlink_shared.h netlink_user.h
    ifeq ($(CURD),modules)
		arm-angstrom-linux-gnueabi-gcc ${CFLAGS} ${LDFLAGS} ${CU_FLAGS} -g -lnl bit_button.c -o bit_button
    else
		${CC} ${CFLAGS} ${LDFLAGS} -lnl bit_button.c -o bit_button
    endif

clean:
	$(RM) .*.cmd *.o *.ko -r .tmp* *.mod.c modules.order Module.symvers user_conn bit_button
