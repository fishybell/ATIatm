require e2fsprogs-libs.inc

PR = "r1"

SRC_URI = "${SOURCEFORGE_MIRROR}/e2fsprogs/e2fsprogs-libs-${PV}.tar.gz \
	   file://mkinstalldirs.patch;patch=1 \
	   file://nodocs.patch;patch=1 \
	  "

DEPENDS = "gettext-native"

EXTRA_OECONF=" --enable-elf-shlibs "

do_compile_prepend () {
	find ./ -print|xargs chmod u=rwX
	( cd util; ${BUILD_CC} subst.c -o subst )
}
