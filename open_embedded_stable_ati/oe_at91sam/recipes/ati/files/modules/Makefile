obj-m := 	target.o \
			target_lifter_infantry.o \
			target_mover_infantry.o \
			target_muzzle_flash.o \
			target_miles_transmitter.o \
			target_thermal.o \
			target_battery.o \
			target_hit_mechanical.o \
			target_hit_miles.o \
			target_user_interface.o \
			target_mover_armor.o \
			target_lifter_armor.o \

PWD := $(shell pwd)
#EXTRA_CFLAGS += -mfloat-abi=softfp -mfpu=vfp

default:
#	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
	make ARCH=arm CROSS_COMPILE=arm-angstrom-linux-gnueabi- -C $(KDIR) SUBDIRS=$(PWD)  modules
	
#export	EXTRA_CFLAGS
	
clean:
	$(RM) .*.cmd *.o *.ko -r .tmp* *.mod.c modules.order Module.symvers
