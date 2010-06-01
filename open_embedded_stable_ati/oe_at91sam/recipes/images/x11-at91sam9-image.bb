#Angstrom X11 at91sam9 image

XSERVER ?= ""
	
ANGSTROM_EXTRA_INSTALL += " \
	gpe-irc \
	gpe-sketchbook \
	gpe-filemanager \
	gpe-tetris \
	gpe-go \
	gpe-calendar \
	gpe-contacts \
	gpe-edit \
	gpe-gallery \
	gpe-calculator \
	gpe-clock \
	gpe-terminal \
	gpe-watch \
	matchbox-panel-hacks \
	gpe-scap \
	gpe-windowlist \
	gpe-mixer \
	usbview \
	mplayer \
	thttpd \
	madplay \
	alsa-utils-aplay \
	alsa-utils-amixer \
	iperf \
	dosfstools \
	mtd-utils \
#	nbench-byte \
	gpe-mini-browser \
	pointercal \
	"

export IMAGE_BASENAME = "x11-at91sam9-image"

DEPENDS = "task-base"
IMAGE_INSTALL = "\
    ${XSERVER} \
    task-base-extended \
    angstrom-x11-base-depends \
    angstrom-gpe-task-base \
    angstrom-gpe-task-settings \
    ${ANGSTROM_EXTRA_INSTALL}"

IMAGE_PREPROCESS_COMMAND = "create_etc_timestamp"

#zap root password for release images
ROOTFS_POSTPROCESS_COMMAND += '${@base_conditional("DISTRO_TYPE", "release", "zap_root_password; ", "",d)}'
ROOTFS_POSTPROCESS_COMMAND += "set_image_autologin; "

#we dont need the kernel in the image
ROOTFS_POSTPROCESS_COMMAND += "rm -f ${IMAGE_ROOTFS}/boot/*Image*; "

inherit image
