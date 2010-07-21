# Action Target, Inc. specific files

PR = "r0"

SRC_URI =  "file://*gpio*"
SRC_URI += "file://issue*"
SRC_URI += "file://ati2-0.9/*.c"
SRC_URI += "file://ati2-0.9/Makefile"
SRC_URI += "file://*.fw"

FILES_${PN} += "/lib/firmware/*"

do_compile () {
    oe_runmake
}

do_install () {
    install -m 755 -d ${D}/usr/bin
    install -m 755 -d ${D}/etc
    install -m 755 -d ${D}/lib/firmware
    install -m 755 ${WORKDIR}/*gpio* ${D}/usr/bin
    install -m 755 ${WORKDIR}/${P}/hello ${D}/usr/bin
#    install -m 755 ${WORKDIR}/${P}/readtty ${D}/usr/bin
    install -m 644 ${WORKDIR}/issue* ${D}/etc
    install -m 644 ${WORKDIR}/*.fw ${D}/lib/firmware
}

