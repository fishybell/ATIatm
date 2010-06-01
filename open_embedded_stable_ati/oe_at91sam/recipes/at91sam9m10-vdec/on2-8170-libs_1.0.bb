# This is the support library for Hantro X170 video decoder.
DESCRIPTION = "Library for Hantro X170 video decoder"
SECTION = "libs"
PRIORITY = "optional"

COMPATIBLE_MACHINE = (at91sam9m10ekes|at91sam9m10g45ek)

PR = "r1"
PROVIDES += "hantro-libs"
SRC_URI ="ftp://ftp.linux4sam.org/pub/demo/linux4sam_1.9/codec/on2-8170-libs-1.0.tar.gz"

inherit autotools
S = ${WORKDIR}/on2-8170-libs-${PV}

FILES_${PN} = " \
	${libdir}/*.so \
	"

FILES_${PN}-dev = " \
	${libdir}/*.so \
	${libdir}/*.a  \
	${libdir}/*.la \
	${includedir}/*.h  \
	"


SRC_URI_append = ""

do_stage() {
	oe_libinstall -s -a -C ${S}   libdwlx170   ${STAGING_LIBDIR}
	oe_libinstall -s -a -C ${S}   libdecx170h  ${STAGING_LIBDIR}
	oe_libinstall -s -a -C ${S}   libx170j     ${STAGING_LIBDIR}
	oe_libinstall -s -a -C ${S}   libdecx170m2 ${STAGING_LIBDIR}
	oe_libinstall -s -a -C ${S}   libdecx170m  ${STAGING_LIBDIR}
	oe_libinstall -s -a -C ${S}   libdecx170p  ${STAGING_LIBDIR}
	oe_libinstall -s -a -C ${S}   libdecx170v  ${STAGING_LIBDIR}
	
	install -m 0644 ${S}/*.h ${STAGING_INCDIR}/
}

do_install() {
	oe_libinstall -s  -C ${S}  libdwlx170   ${D}/${libdir}/
	oe_libinstall -s  -C ${S}  libdecx170h  ${D}/${libdir}/
	oe_libinstall -s  -C ${S}  libx170j     ${D}/${libdir}/
	oe_libinstall -s  -C ${S}  libdecx170m2 ${D}/${libdir}/
	oe_libinstall -s  -C ${S}  libdecx170m  ${D}/${libdir}/
	oe_libinstall -s  -C ${S}  libdecx170p  ${D}/${libdir}/
	oe_libinstall -s  -C ${S}  libdecx170v  ${D}/${libdir}/
}
