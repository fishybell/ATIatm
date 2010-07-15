DESCRIPTION = "ATI Target Modules"
LICENSE = "unknown"
PR = "r4"
RDEPENDS = "kernel (${KERNEL_VERSION})"
DEPENDS = "virtual/kernel"

#        file://ati-1.0.tar.gz  \
SRC_URI = " \
		file://modules/Makefile \
		file://modules/target_battery.c \
		file://modules/target_battery.h \
		file://modules/target.c \
		file://modules/target.h \
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
		file://modules/target_mover_infantry.c \
		file://modules/target_mover_infantry.h \
		file://modules/target_muzzle_flash.c \
		file://modules/target_muzzle_flash.h \
		file://modules/target_sound.c \
		file://modules/target_sound.h \
		file://modules/target_thermal.c \
		file://modules/target_thermal.h \
		file://modules/target_user_interface.c \
		file://modules/target_user_interface.h \
        file://start_target_modules.sh \
          "
SRCNAME = "${PN}"
#S = "${WORKDIR}/${SRCNAME}-${PV}/modules"
S = "${WORKDIR}/modules"

inherit module

#oe_runmake 'MODPATH=${D}${base_libdir}/modules/${KERNEL_VERSION}/kernel/drivers/ecu' \

do_compile() {
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
    install -m 0644 ${S}/target_battery.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target_hit_mechanical.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target_hit_miles.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target_lifter_armor.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target_lifter_infantry.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target_miles_transmitter.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target_mover_infantry.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target_muzzle_flash.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target_sound.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target_thermal.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target_user_interface.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0755 ${WORKDIR}/start_target_modules.sh ${D}${ROOT_USER_INSTALL_DIR}/start_target_modules.sh
    install -m 0755 ${WORKDIR}/asoundrc ${D}${ROOT_USER_INSTALL_DIR}/.asoundrc
}

PACKAGE_ARCH = "at91sam9g20ek"
PACKAGES = "${PN}"
FILES_${PN} = "${TARGET_MODULES_INSTALL_DIR}"



