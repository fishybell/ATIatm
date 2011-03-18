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

PWD := $(shell pwd)
CURD := $(shell basename `pwd`)
#EXTRA_CFLAGS += -mfloat-abi=softfp -mfpu=vfp

default: 
#	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
	make ARCH=arm CROSS_COMPILE=arm-angstrom-linux-gnueabi- -C $(KDIR) SUBDIRS=$(PWD)  modules
	
#export	EXTRA_CFLAGS

user_conn: user_conn.c netlink_userspace.c
    ifeq ($(CURD),modules)
		arm-angstrom-linux-gnueabi-gcc ${CFLAGS} ${LDFLAGS} user_conn.c netlink_userspace.c -o user_conn
    else
		${CC} ${CFLAGS} ${LDFLAGS} user_conn.c netlink_userspace.c -o user_conn
    endif

user_wait: user_wait.c netlink_userspace.c
    ifeq ($(CURD),modules)
		arm-angstrom-linux-gnueabi-gcc ${CFLAGS} ${LDFLAGS} user_wait.c netlink_userspace.c -o user_wait
    else
		${CC} ${CFLAGS} ${LDFLAGS} user_wait.c netlink_userspace.c -o user_wait
    endif

clean:
	$(RM) .*.cmd *.o *.ko -r .tmp* *.mod.c modules.order Module.symvers user_conn user_wait
