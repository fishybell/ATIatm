DESCRIPTION = "ATI Target Modules"
LICENSE = "unknown"
PR = "r4"
RDEPENDS = "kernel (${KERNEL_VERSION})"
DEPENDS = "virtual/kernel linux libnl"

#        file://ati-1.0.tar.gz  \
#        file://start_target_modules.sh \
SRC_URI = " \
		file://asoundrc \
		file://modules/Makefile \
		file://modules/lifter.c \
		file://modules/lifter.h \
		file://modules/target_battery.c \
		file://modules/target_battery.h \
		file://modules/target.c \
		file://modules/target.h \
		file://modules/target_generic_output.c \
		file://modules/target_generic_output.h \
		file://modules/target_hardware.h \
		file://modules/target_hit_mechanical.c \
		file://modules/target_hit_mechanical.h \
		file://modules/target_hit_miles.c \
		file://modules/target_hit_miles.h \
		file://modules/target_lifter_armor.c \
		file://modules/target_lifter_armor.h \
		file://modules/target_lifter_infantry.c \
		file://modules/target_lifter_infantry.h \
		file://modules/target_miles_transmitter.c \
		file://modules/target_miles_transmitter.h \
		file://modules/target_mover_armor.c \
		file://modules/target_mover_armor.h \
		file://modules/target_mover_generic.c \
		file://modules/target_mover_generic.h \
		file://modules/target_mover_infantry.c \
		file://modules/target_mover_infantry.h \
		file://modules/target_mover_position.c \
		file://modules/target_mover_position.h \
		file://modules/target_muzzle_flash.c \
		file://modules/target_muzzle_flash.h \
		file://modules/target_thermal.c \
		file://modules/target_thermal.h \
		file://modules/target_ses_interface.c \
		file://modules/target_ses_interface.h \
		file://modules/target_user_interface.c \
		file://modules/target_user_interface.h \
		file://modules/netlink_kernel.h \
		file://modules/netlink_user.h \
		file://modules/netlink_shared.h \
		file://modules/netlink_provider.c \
		file://modules/bit_button.c \
		file://modules/user_conn.c \
		file://modules/delay_printk.c \
		file://modules/delay_printk.h \
          "
SRCNAME = "${PN}"
#S = "${WORKDIR}/${SRCNAME}-${PV}/modules"
S = "${WORKDIR}/modules"

inherit module

#oe_runmake 'MODPATH=${D}${base_libdir}/modules/${KERNEL_VERSION}/kernel/drivers/ecu' \

do_compile() {
    oe_runmake user_conn bit_button
	unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS CC LD CPP
        oe_runmake 'KERNEL_SOURCE=${STAGING_KERNEL_DIR}' \
	    'KDIR=${STAGING_KERNEL_DIR}' \
	    'KERNEL_VERSION=${KERNEL_VERSION}' \
	    'CC=${KERNEL_CC}' \
	    'LD=${KERNEL_LD}' \
        default 
}

#_INSTALL_DIR = "${base_libdir}/modules/${KERNEL_VERSION}/kernel/drivers/test"
TARGET_MODULES_INSTALL_DIR = "/home/root"
ROOT_USER_INSTALL_DIR = "/home/root"

do_install(){
    install -d ${D}${TARGET_MODULES_INSTALL_DIR}
    install -d ${D}/etc
    install -d ${D}/usr/bin
    install -m 0644 ${S}/*.ko ${D}${TARGET_MODULES_INSTALL_DIR}
#    install -m 0755 ${WORKDIR}/start_target_modules.sh ${D}${ROOT_USER_INSTALL_DIR}/start_target_modules.sh
    install -m 0644 ${WORKDIR}/asoundrc ${D}/etc/asound.conf
    install -m 0755 ${S}/user_conn ${D}/usr/bin
}

PACKAGE_ARCH = "at91sam9g20ek_2mmc"
PACKAGES = "${PN}"
FILES_${PN} = "${TARGET_MODULES_INSTALL_DIR}"
FILES_${PN} += "/etc/asound.conf"
FILES_${PN} += "/usr/bin/user_conn"



