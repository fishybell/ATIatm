DESCRIPTION = "U-boot bootloader OS env. access tools"
SECTION = "bootloaders"
PRIORITY = "optional"
LICENSE = "GPL"
DEPENDS = "mtd-utils"
PR = "r8"

SRC_URI = "ftp://ftp.denx.de/pub/u-boot/u-boot-${PV}.tar.bz2 \
          file://fw_env.config \
          file://001-u-boot-1.3.4-exp.4.patch;patch=1\
          file://002-watchdog.patch;patch=1\
          file://003-watchdog.patch;patch=1\
          file://004-quick.patch;patch=1\
          file://005-mac.patch;patch=1\
          file://006-fw-env.patch;patch=1\
          "


S = "${WORKDIR}/u-boot-${PV}"

EXTRA_OEMAKE = "CROSS_COMPILE=${TARGET_PREFIX}"
TARGET_LDFLAGS = ""
FILESDIR = "${@os.path.dirname(bb.data.getVar('FILE',d,1))}/u-boot-${PV}"

do_configure() {
        :
}

do_compile () {
        oe_runmake Sandpoint8240_config
        oe_runmake tools
}

do_install () {
        install -d      ${D}/sbin
        install -d      ${D}${sysconfdir}
        install -m 644 ${WORKDIR}/fw_env.config ${D}${sysconfdir}/fw_env.config
        install -m 755 ${S}/tools/env/fw_printenv ${D}/sbin/fw_printenv
        install -m 755 ${S}/tools/env/fw_printenv ${D}/sbin/fw_setenv
}

