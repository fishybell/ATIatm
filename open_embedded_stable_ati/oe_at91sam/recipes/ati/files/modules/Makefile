obj-m := 	target.o \
			target_lifter_infantry.o \
			target_mover_infantry.o \
			target_muzzle_flash.o \
			target_miles_transmitter.o \
			target_thermal.o \
			target_battery.o \
			target_hit_mechanical.o \
			target_hit_miles.o \
			target_ses_interface.o \
			target_user_interface.o \
			target_mover_armor.o \
			target_lifter_armor.o \
			target_mover_position.o \
			target_mover_generic.o \
			netlink_provider.o \
			delay_printk.o \

PWD := $(shell pwd)
CURD := $(shell basename `pwd`)
#EXTRA_CFLAGS += -mfloat-abi=softfp -mfpu=vfp
CU_FLAGS :=-I/usr/local/var/oe/tmp/work/at91sam9g20ek_2mmc-angstrom-linux-gnueabi/linux-2.6.30-r1/linux-2.6.30/include

default: 
#	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
	make ARCH=arm CROSS_COMPILE=arm-angstrom-linux-gnueabi- -C $(KDIR) SUBDIRS=$(PWD)  modules
	
#export	EXTRA_CFLAGS

user_conn: user_conn.c
    ifeq ($(CURD),modules)
		arm-angstrom-linux-gnueabi-gcc ${CFLAGS} ${LDFLAGS} ${CU_FLAGS} -g -lnl user_conn.c -o user_conn
    else
		${CC} ${CFLAGS} ${LDFLAGS} user_conn.c -o user_conn
    endif

clean:
	$(RM) .*.cmd *.o *.ko -r .tmp* *.mod.c modules.order Module.symvers user_conn user_wait user_wait_nl
