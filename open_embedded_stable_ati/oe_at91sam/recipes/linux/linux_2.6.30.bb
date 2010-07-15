require linux.inc
require linux-at91.inc

PR = "r1"

S = "${WORKDIR}/linux-${PV}"

SRC_URI = " \
	${KERNELORG_MIRROR}/pub/linux/kernel/v2.6/linux-${PV}.tar.bz2 \
	http://maxim.org.za/AT91RM9200/2.6/2.6.30-at91.patch.gz;patch=1 \
	file://defconfig \
	"

