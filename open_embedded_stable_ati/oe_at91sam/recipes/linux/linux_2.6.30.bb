require linux.inc
require linux-at91.inc

PR = "r1"

S = "${WORKDIR}/linux-${PV}"

SRC_URI = " \
	${KERNELORG_MIRROR}/pub/linux/kernel/v2.6/linux-${PV}.tar.bz2 \
	http://maxim.org.za/AT91RM9200/2.6/2.6.30-at91.patch.gz;patch=1 \
	file://defconfig \
	"
SRC_URI_append ?= " \
    file://ati/0001-removed-at91sam9g20ek-board-inits-of-leds-and-button.patch;patch=1 \
	file://ati/0002-removed-adding-of-usb-device-to-platform.patch;patch=1 \
    "

