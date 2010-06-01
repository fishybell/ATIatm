SRCREV_pn-tslib := "84"

SRC_URI = "svn://svn.berlios.de/tslib/trunk;module=tslib"
S = "${WORKDIR}/tslib"
PV = "1.0+svnr${SRCREV}"
DEFAULT_PREFERENCE = "99"

include tslib.inc
