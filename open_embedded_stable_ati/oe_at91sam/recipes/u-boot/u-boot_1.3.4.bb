require u-boot.inc

DEFAULT_PREFERENCE = "1"

PR = "r0"

SRC_URI = "ftp://ftp.denx.de/pub/u-boot/u-boot-${PV}.tar.bz2\
          file://001-u-boot-1.3.4-exp.4.patch;patch=1\
          "

