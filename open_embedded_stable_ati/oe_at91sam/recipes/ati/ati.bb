DESCRIPTION = "ATI Target Modules"
LICENSE = "unknown"
PR = "r4"
RDEPENDS = "kernel (${KERNEL_VERSION})"
DEPENDS = "virtual/kernel linux libnl"

#        file://ati-1.0.tar.gz  \
#        file://start_target_modules.sh \
SRC_URI = " \
		file://asoundrc \
		file://modules "

SRCNAME = "${PN}"
#S = "${WORKDIR}/${SRCNAME}-${PV}/modules"
S = "${WORKDIR}/modules"

inherit module

do_compile() {
    oe_runmake clean
    oe_runmake user_conn bit_button event_conn fasit_conn bcast_server bcast_client
	unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS CC LD CPP
        oe_runmake 'KERNEL_SOURCE=${STAGING_KERNEL_DIR}' \
	    'KDIR=${STAGING_KERNEL_DIR}' \
	    'KERNEL_VERSION=${KERNEL_VERSION}' \
	    'CC=${KERNEL_CC}' \
	    'LD=${KERNEL_LD}' \
        default 
}

TARGET_MODULES_INSTALL_DIR = "/home/root"
ROOT_USER_INSTALL_DIR = "/home/root"

do_install(){
    install -d ${D}${TARGET_MODULES_INSTALL_DIR}
    install -d ${D}/etc
    install -d ${D}/usr/bin
    install -m 0644 ${S}/*.ko ${D}${TARGET_MODULES_INSTALL_DIR}
    install -m 0755 ${S}/*.arm ${D}/usr/bin
    install -m 0644 ${WORKDIR}/asoundrc ${D}/etc/asound.conf
    install -m 0755 ${S}/user_conn ${D}/usr/bin
    install -m 0755 ${S}/bit_button ${D}/usr/bin
    install -m 0755 ${S}/fasit_conn ${D}/usr/bin
    install -m 0755 ${S}/event_conn ${D}/usr/bin
    install -m 0755 ${S}/bcast_server ${D}/usr/bin
    install -m 0755 ${S}/bcast_client ${D}/usr/bin
}

PACKAGE_ARCH = "at91sam9g20ek_2mmc"
PACKAGES = "${PN}"
FILES_${PN} = "${TARGET_MODULES_INSTALL_DIR}"
FILES_${PN} += "/etc/asound.conf"
FILES_${PN} += "/usr/bin/user_conn"
FILES_${PN} += "/usr/bin/bit_button"
FILES_${PN} += "/usr/bin/event_conn"
FILES_${PN} += "/usr/bin/fasit_conn"
FILES_${PN} += "/usr/bin/bcast_server"
FILES_${PN} += "/usr/bin/bcast_client"



