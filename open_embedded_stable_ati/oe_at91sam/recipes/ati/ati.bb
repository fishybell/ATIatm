DESCRIPTION = "ATI Target Modules"bit
LICENSE = "unknown"
PR = "r4"
RDEPENDS = "kernel (${KERNEL_VERSION})"
DEPENDS = "virtual/kernel"

SRC_URI = " \
        file://ati-1.0.tar.gz  \
		file://start_target_modules.sh \
          "

SRCNAME = "${PN}"
S = "${WORKDIR}/${SRCNAME}-${PV}"

inherit module

#oe_runmake 'MODPATH=${D}${base_libdir}/modules/${KERNEL_VERSION}/kernel/drivers/ecu' \

do_compile() {
	unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS CC LD CPP
        oe_runmake 'KERNEL_SOURCE=${STAGING_KERNEL_DIR}' \
	    'KDIR=${STAGING_KERNEL_DIR}' \
	    'KERNEL_VERSION=${KERNEL_VERSION}' \
	    'CC=${KERNEL_CC}' \
	    'LD=${KERNEL_LD}' \
        build 
}

#_INSTALL_DIR = "${base_libdir}/modules/${KERNEL_VERSION}/kernel/drivers/test"
TARGET_MODULES_INSTALL_DIR = "/home/root"

do_install(){
    install -d ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0644 ${S}/target_lifter_infantry.ko ${D}${TARGET_MODULES_INSTALL_DIR}

    install -m 0755 ${WORKDIR}/start_target_modules.sh ${D}${TARGET_MODULES_INSTALL_DIR}/start_target_modules.sh
}

PACKAGE_ARCH = "at91sam9g20ek"
PACKAGES = "${PN}"
FILES_${PN} = "${TARGET_MODULES_INSTALL_DIR}"



