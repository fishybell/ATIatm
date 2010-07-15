DESCRIPTION = "Latest wireless drivers"
HOMEPAGE = "http://wireless.kernel.org/en/users/Download"
SECTION = "kernel/modules"
LICENSE = "GPL"
RDEPENDS = "wireless-tools"

SRC_URI = "http://wireless.kernel.org/download/compat-wireless-2.6/compat-wireless-${PV}.tar.bz2 \
	  file://config.patch;patch=1"

inherit module

PR = "r0"

EXTRA_OEMAKE = "KLIB_BUILD=${STAGING_KERNEL_DIR} KLIB=${D}"

LDFLAGS = "-L${STAGING}/${BUILD_SYS}/usr/lib -rpath-link ${STAGING}/${BUILD_SYS}/usr/lib --hash-style=gnu"


do_configure_append() {
	sed -i "s#@./scripts/update-initramfs## " Makefile
}

FILES_${PN} += "/usr/sbin/* \
                /usr/lib/compat-wireless/* \
                /lib/udev/* \
                /lib/udev/rules.d/* \
               "

do_install() {
	install -m 750 -d ${D}/usr/sbin
	oe_runmake DEPMOD=echo DESTDIR="${D}" INSTALL_MOD_PATH="${D}" install-modules
	oe_runmake DEPMOD=echo DESTDIR="${D}" INSTALL_MOD_PATH="${D}" install-scripts
}

do_configure() {
        cd ${S}
#        ./scripts/driver-select ath
}

