require libmatchbox.inc
PR = "r7"

SRC_URI = "\
  http://projects.o-hand.com/matchbox/sources/${PN}/${PV}/${PN}-${PV}.tar.gz \
  file://16bppfixes-2.patch;patch=1 \
  file://configure_fixes.patch;patch=1 \
  file://reset-sigchld.patch;patch=1 \
  file://check.m4\
"

do_configure_prepend () {
	cp ${WORKDIR}/check.m4 ${S}/
}
