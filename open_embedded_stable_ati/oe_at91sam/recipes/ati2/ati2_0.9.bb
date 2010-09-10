# Action Target, Inc. specific files

PR = "r0"

SRC_URI =  "file://*gpio*"
SRC_URI += "file://issue*"
SRC_URI += "file://ati2-0.9/*.cpp"
SRC_URI += "file://ati2-0.9/*.h"
SRC_URI += "file://ati2-0.9/Makefile"
SRC_URI += "file://*.fw"
SRC_URI += "file://inittab"
SRC_URI += "file://SIT"
SRC_URI += "file://LSAT"
SRC_URI += "file://HSAT"
SRC_URI += "file://MIT"
SRC_URI += "file://MAT"

FILES_${PN} += "/lib/firmware/*"

do_compile () {
    oe_runmake
}

do_install () {
    install -m 755 -d ${D}/usr/bin
    install -m 755 -d ${D}/etc
    install -m 755 -d ${D}/lib/firmware
    install -m 755 ${WORKDIR}/*gpio* ${D}/usr/bin
    install -m 755 ${WORKDIR}/${P}/radio_conv ${D}/usr/bin
    install -m 644 ${WORKDIR}/issue* ${D}/etc
    install -m 644 ${WORKDIR}/inittab ${D}/etc
    install -m 755 ${WORKDIR}/SIT ${D}/usr/bin
    install -m 755 ${WORKDIR}/LSAT ${D}/usr/bin
    install -m 755 ${WORKDIR}/HSAT ${D}/usr/bin
    install -m 755 ${WORKDIR}/MIT ${D}/usr/bin
    install -m 755 ${WORKDIR}/MAT ${D}/usr/bin
    install -m 644 ${WORKDIR}/*.fw ${D}/lib/firmware
}

