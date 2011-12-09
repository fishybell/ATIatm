#Angstrom bootstrap at91sam9 image

IMAGE_PREPROCESS_COMMAND = "create_etc_timestamp"

ANGSTROM_EXTRA_INSTALL += " \
	usbview \
	mplayer \
	thttpd \
	alsa-utils-aplay \
	alsa-utils-amixer \
	iperf \
	dosfstools \
	mtd-utils \
	gdb \
	tcl \
    madplay \
    osso-esd \
	avahi-utils \
	libavahi-client \
	"

DEPENDS = "task-base-extended \
           at91bootstrap \
	   libnl \
	   ati ati2\
           compat-wireless \
	   tcl \
    madplay \
    osso-esd \
    ncurses \
           ${@base_contains("MACHINE_FEATURES", "screen", "psplash-zap", "",d)} \
	   "

IMAGE_INSTALL = "task-base-extended \
	   ${ANGSTROM_EXTRA_INSTALL} \
           kernel-modules \
	   libnl \
	   ati ati2\
           compat-wireless \
           lame \
           ncurses-terminfo \
	   ${@base_contains("MACHINE_FEATURES", "screen", "psplash-zap", "",d)} \
	   "

export IMAGE_BASENAME = "console-image"
IMAGE_LINGUAS = ""

#we dont need the kernel in the image
ROOTFS_POSTPROCESS_COMMAND += "rm -f ${IMAGE_ROOTFS}/boot/*Image*; "

inherit image

