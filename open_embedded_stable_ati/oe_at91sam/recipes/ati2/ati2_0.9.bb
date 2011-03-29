# Action Target, Inc. specific files

PR = "r0"

SRC_URI =  "file://*gpio*"
SRC_URI += "file://issue*"
SRC_URI += "file://ati2-0.9/*.cpp"
SRC_URI += "file://ati2-0.9/*.c"
SRC_URI += "file://ati2-0.9/*.h"
SRC_URI += "file://ati2-0.9/*.hh"
SRC_URI += "file://ati2-0.9/at91lib"
SRC_URI += "file://ati2-0.9/eeprom"
SRC_URI += "file://ati2-0.9/Makefile"
SRC_URI += "file://*.fw"
SRC_URI += "file://*.tcl"
SRC_URI += "file://*.bin"
SRC_URI += "file://start_up"
SRC_URI += "file://interfaces"
SRC_URI += "file://be_*"
SRC_URI += "file://is_*"
SRC_URI += "file://SIT"
SRC_URI += "file://LSAT"
SRC_URI += "file://HSAT"
SRC_URI += "file://MIT"
SRC_URI += "file://MAT"
SRC_URI += "file://SES"
SRC_URI += "file://BASE"
SRC_URI += "file://fasit/*.py"
SRC_URI += "file://fasit/sounds/*.mp3"
#SRC_URI += "file://fasit/sounds/*.wav"
#SRC_URI += "file://fasit/sounds/*.ogg"
#SRC_URI += "file://fasit/sounds/*.aif"

FILES_${PN} += "/lib/firmware/*"
FILES_${PN} += "/etc/init.d/*"
FILES_${PN} += "/etc/rcS.d/*"
FILES_${PN} += "/etc/network/*"
FILES_${PN} += "/home/root/*"
FILES_${PN} += "/home/root/fasit/*"
FILES_${PN} += "/home/root/fasit/sounds/*"

do_compile () {
    oe_runmake
}

do_install () {
    install -m 755 -d ${D}/usr/bin
    install -m 755 -d ${D}/etc/init.d
    install -m 755 -d ${D}/etc/rcS.d
    install -m 755 -d ${D}/etc/network
    install -m 755 -d ${D}/home/root/fasit/sounds
    install -m 755 -d ${D}/lib/firmware
    install -m 755 ${WORKDIR}/*gpio* ${D}/usr/bin
    install -m 755 ${WORKDIR}/${P}/radio_conv ${D}/usr/bin
    install -m 755 ${WORKDIR}/${P}/eeprom_rw ${D}/usr/bin
    install -m 644 ${WORKDIR}/issue* ${D}/etc
    install -m 644 ${WORKDIR}/fasit/*.py ${D}/home/root/fasit
    install -m 644 ${WORKDIR}/fasit/*.mp3 ${D}/home/root/fasit/sounds
    install -m 755 ${WORKDIR}/start_up ${D}/etc/init.d
    install -m 755 ${WORKDIR}/is_* ${D}/usr/bin
    install -m 755 ${WORKDIR}/be_* ${D}/usr/bin
    install -m 755 ${WORKDIR}/SIT ${D}/usr/bin
    install -m 755 ${WORKDIR}/LSAT ${D}/usr/bin
    install -m 755 ${WORKDIR}/HSAT ${D}/usr/bin
    install -m 755 ${WORKDIR}/MIT ${D}/usr/bin
    install -m 755 ${WORKDIR}/MAT ${D}/usr/bin
    install -m 755 ${WORKDIR}/SES ${D}/usr/bin
    install -m 755 ${WORKDIR}/BASE ${D}/usr/bin
    install -m 755 ${WORKDIR}/*.tcl ${D}/usr/bin
    install -m 644 ${WORKDIR}/*.fw ${D}/lib/firmware
    install -m 644 ${WORKDIR}/*.bin ${D}/lib/firmware
    install -m 644 ${WORKDIR}/interfaces ${D}/etc/network
    cd ${D}/etc/rcS.d
    ln -s ../init.d/start_up S98start_up
}

