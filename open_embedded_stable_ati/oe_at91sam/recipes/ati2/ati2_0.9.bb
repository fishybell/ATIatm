# Action Target, Inc. specific files

PR = "r0"

SRC_URI = "file://issue*"
SRC_URI += "file://passwd"
#SRC_URI +=  "file://*gpio*"
SRC_URI += "file://ati2-0.9/*.cpp"
SRC_URI += "file://ati2-0.9/*.c"
SRC_URI += "file://ati2-0.9/*.h"
SRC_URI += "file://ati2-0.9/at91lib"
SRC_URI += "file://ati2-0.9/eeprom"
SRC_URI += "file://ati2-0.9/Makefile"
SRC_URI += "file://*.fw"
#SRC_URI += "file://*.tcl"
SRC_URI += "file://*.bin"
SRC_URI += "file://start_up"
SRC_URI += "file://start"
SRC_URI += "file://stop"
SRC_URI += "file://restart"
SRC_URI += "file://interfaces"
SRC_URI += "file://load_network"
SRC_URI += "file://load_modules"
SRC_URI += "file://unload"
SRC_URI += "file://be_*"
SRC_URI += "file://is_*"
SRC_URI += "file://EIM"
SRC_URI += "file://SIT"
SRC_URI += "file://SAT"
SRC_URI += "file://MIT"
SRC_URI += "file://MAT"
SRC_URI += "file://TTMT"
SRC_URI += "file://SES"
SRC_URI += "file://BASE"
SRC_URI += "file://HHC"
SRC_URI += "file://none"
#SRC_URI += "file://fasit/*.py"
SRC_URI += "file://fasit/sounds/*.mp3"
#SRC_URI += "file://fasit/sounds/*.wav"
#SRC_URI += "file://fasit/sounds/*.ogg"
#SRC_URI += "file://fasit/sounds/*.aif"

FILES_${PN} += "/lib/firmware/*"
FILES_${PN} += "/etc/init.d/*"
#FILES_${PN} += "/etc/passwd/*"
FILES_${PN} += "/etc/rcS.d/*"
FILES_${PN} += "/etc/network/*"
FILES_${PN} += "/home/root/*"
#FILES_${PN} += "/home/root/fasit/*"
FILES_${PN} += "/home/root/sounds/*"

do_compile () {
    oe_runmake
}

do_install () {
    install -m 755 -d ${D}/usr/bin
    install -m 755 -d ${D}/etc/init.d
#	 install -m 755 -d ${D}/etc/passwd
    install -m 755 -d ${D}/etc/rcS.d
    install -m 755 -d ${D}/etc/network
    install -m 755 -d ${D}/home/root/sounds
    install -m 755 -d ${D}/lib/firmware
#    install -m 755 ${WORKDIR}/*gpio* ${D}/usr/bin
    install -m 755 ${WORKDIR}/${P}/radio_conv ${D}/usr/bin
    install -m 755 ${WORKDIR}/${P}/eeprom_rw ${D}/usr/bin
    install -m 644 ${WORKDIR}/issue* ${D}/etc
	install -m 644 ${WORKDIR}/passwd ${D}/etc
#    install -m 644 ${WORKDIR}/fasit/*.py ${D}/home/root/fasit
    install -m 644 ${WORKDIR}/fasit/*.mp3 ${D}/home/root/sounds
    install -m 755 ${WORKDIR}/start_up ${D}/etc/init.d
    install -m 755 ${WORKDIR}/start ${D}/usr/bin
    install -m 755 ${WORKDIR}/stop ${D}/usr/bin
    install -m 755 ${WORKDIR}/restart ${D}/usr/bin
    install -m 644 ${WORKDIR}/load_network ${D}/usr/bin
    install -m 755 ${WORKDIR}/load_modules ${D}/usr/bin
    install -m 755 ${WORKDIR}/unload ${D}/usr/bin
    install -m 755 ${WORKDIR}/is_* ${D}/usr/bin
    install -m 755 ${WORKDIR}/be_* ${D}/usr/bin
    install -m 644 ${WORKDIR}/SIT ${D}/usr/bin
    install -m 644 ${WORKDIR}/SAT ${D}/usr/bin
    install -m 644 ${WORKDIR}/MIT ${D}/usr/bin
    install -m 644 ${WORKDIR}/MAT ${D}/usr/bin
    install -m 644 ${WORKDIR}/SES ${D}/usr/bin
    install -m 644 ${WORKDIR}/BASE ${D}/usr/bin
    install -m 644 ${WORKDIR}/HHC ${D}/usr/bin
    install -m 644 ${WORKDIR}/none ${D}/usr/bin
#    install -m 755 ${WORKDIR}/*.tcl ${D}/usr/bin
    install -m 644 ${WORKDIR}/*.fw ${D}/lib/firmware
    install -m 644 ${WORKDIR}/*.bin ${D}/lib/firmware
    install -m 644 ${WORKDIR}/interfaces ${D}/etc/network
    cd ${D}/etc/rcS.d
    ln -s ../init.d/start_up S98start_up
    date > ${D}/etc/builddate
}

