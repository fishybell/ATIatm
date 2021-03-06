LICENSE = "MIT"
DEPENDS = "compositeproto damageproto fixesproto recordproto resourceproto scrnsaverproto xineramaproto videoproto xextproto xproto \
	libxau libxext libxdmcp libxfont libxrandr tslib virtual/libx11 xtrans"

PROVIDES = "virtual/xserver"
PACKAGES =+ "xserver-kdrive-fbdev \
             xserver-kdrive-sdl \
             xserver-kdrive-fake \
             xserver-kdrive-xephyr \
             xserver-kdrive-epson \
             xserver-kdrive-ati \
             xserver-kdrive-chips \
             xserver-kdrive-i810 \
             xserver-kdrive-mach64 \
             xserver-kdrive-mga \
             xserver-kdrive-neomagic \
             xserver-kdrive-nvidia \
             xserver-kdrive-pm2 \
             xserver-kdrive-r128 \
             xserver-kdrive-smi \
             xserver-kdrive-vesa \
             xserver-kdrive-via \
             xserver-kdrive-w100 \	
 	"

SECTION = "x11/base"
DESCRIPTION = "X server from freedesktop.org"
DESCRIPTION_xserver-kdrive-fbdev = "X server from freedesktop.org, supporting generic framebuffer devices"
DESCRIPTION_xserver-kdrive-fake = "Fake X server"
DESCRIPTION_xserver-kdrive-xephyr = "X server in an X window"
DESCRIPTION_xserver-kdrive-epson = "X server from freedesktop.org, supporting Epson S1D13806 devices"
DESCRIPTION_xserver-kdrive-sdl = "X server from freedesktop.org, SDL version"
DESCRIPTION_xserver-kdrive-w100 = "X server from freedesktop.org, supporting the ATI imageon w100 chipset"
DESCRIPTION_xserver-kdrive-ati = "X server from freedesktop.org, supporting ATI devices"
DESCRIPTION_xserver-kdrive-chips = "X server from freedesktop.org, supporting Chips chipsets"
DESCRIPTION_xserver-kdrive-i810 = "X server from freedesktop.org, supporting Intel chipsets"
DESCRIPTION_xserver-kdrive-imageon = "X server from freedesktop.org, supporting the ATI imageon 100 chipset"
DESCRIPTION_xserver-kdrive-mach64 = "X server from freedesktop.org, supporting mach64 chipsets"
DESCRIPTION_xserver-kdrive-mga = "X server from freedesktop.org, supporting mga chipsets"
DESCRIPTION_xserver-kdrive-neomagic = "X server from freedesktop.org, supporting neomagic chipsets"
DESCRIPTION_xserver-kdrive-nvidia = "X server from freedesktop.org, supporting nvidia devices"
DESCRIPTION_xserver-kdrive-pm2 = "X server from freedesktop.org, supporting Permedia2 chipsets"
DESCRIPTION_xserver-kdrive-r128 = "X server from freedesktop.org, supporting rage128 chipsets"
DESCRIPTION_xserver-kdrive-smi = "X server from freedesktop.org, supporting Silicon Motion devices"
DESCRIPTION_xserver-kdrive-vesa = "X server from freedesktop.org, supporting any card with a VESA, VGA or even EGA BIOS. x86 hardware only"
DESCRIPTION_xserver-kdrive-via = "X server from freedesktop.org, supporting VIA chipsets"

PACKAGES =+ "xserver-security-policy"
FILES_xserver-security-policy += "${libdir}/xserver/SecurityPolicy"
RRECOMMENDS_${PN} += "xserver-security-policy"

FILES_xserver-kdrive-fbdev = "${bindir}/Xfbdev"
FILES_xserver-kdrive-fake = "${bindir}/Xfake"
FILES_xserver-kdrive-xephyr = "${bindir}/Xephyr"
FILES_xserver-kdrive-epson = "${bindir}/Xepson"
FILES_xserver-kdrive-sdl = "${bindir}/Xsdl"
FILES_xserver-kdrive-w100 = "${bindir}/Xw100"
FILES_xserver-kdrive-ati = "${bindir}/Xati"
FILES_xserver-kdrive-chips = "${bindir}/Xchips"
FILES_xserver-kdrive-i810 = "${bindir}/Xi810"
FILES_xserver-kdrive-imageon = "${bindir}/Ximageon"
FILES_xserver-kdrive-mach64 = "${bindir}/Xmach64"
FILES_xserver-kdrive-mga = "${bindir}/Xmga"
FILES_xserver-kdrive-neomagic = "${bindir}/Xneomagic"
FILES_xserver-kdrive-nvidia = "${bindir}/Xnvidia"
FILES_xserver-kdrive-pm2 = "${bindir}/Xpm2"
FILES_xserver-kdrive-r128 = "${bindir}/Xr128"
FILES_xserver-kdrive-smi = "${bindir}/Xsmi"
FILES_xserver-kdrive-vesa = "${bindir}/Xvesa"
FILES_xserver-kdrive-via = "${bindir}/Xvia"

SRC_URI ?= "${XORG_MIRROR}/individual/xserver/xorg-server-${PV}.tar.bz2 \
	${KDRIVE_COMMON_PATCHES}"
KDRIVE_COMMON_PATCHES = " \
	file://kmode.patch;patch=1 \
	file://disable-apm.patch;patch=1 \
	file://no-serial-probing.patch;patch=1 \
	file://fbdev-not-fix.patch;patch=1  \
	file://optional-xkb.patch;patch=1 \
	file://enable-tslib.patch;patch=1 \
	file://kmode-palm.patch;patch=1 \
	"

SRC_URI_append_mnci   = " file://onlyfb.patch;patch=1"
SRC_URI_append_poodle = " file://xserver-kdrive-poodle.patch;patch=1"
SRC_URI_append_qemux86 = " file://xserver-kdrive-poodle.patch;patch=1"
PACKAGE_ARCH_poodle = "${MACHINE_ARCH}"
PACKAGE_ARCH_qemux86 = "${MACHINE_ARCH}"

inherit autotools pkgconfig

ARM_INSTRUCTION_SET = "arm"

EXTRA_OECONF = "--enable-composite --enable-kdrive \
                --disable-dga --disable-dri --disable-xinerama \
                --disable-xf86misc --disable-xf86vidmode \
                --disable-xorg --disable-xorgcfg \
                --disable-xkb --disable-xnest --disable-xvfb \
                --disable-xevie --disable-xprint --disable-xtrap \
                --disable-dmx ${W100_OECONF} \
                --with-default-font-path=built-ins \
                --enable-tslib --enable-xcalibrate \
                ac_cv_file__usr_share_sgml_X11_defs_ent=no"

do_configure_prepend() {
    sed -i -e 's/tslib-0.0/tslib-1.0/' ${S}/configure.ac
}
