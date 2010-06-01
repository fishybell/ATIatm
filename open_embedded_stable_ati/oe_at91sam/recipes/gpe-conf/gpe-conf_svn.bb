SRCREV_pn-gpe-conf := "10074"

DEFAULT_PREFERENCE = "99"

DESCRIPTION = "Configuration applets for GPE"
SECTION = "gpe"
PRIORITY = "optional"
LICENSE = "GPL"

DEPENDS = "gtk+ libgpewidget libxsettings libxsettings-client xst xset gpe-login gpe-icons"
RDEPENDS_${PN} = "xst tzdata xset gpe-login gpe-icons"
RDEPENDS_gpe-conf-panel = "gpe-conf"

inherit autotools gpe

PV = "0.2.8+svnr${SRCREV}"
PR = "r2"

SRC_URI = "${GPE_SVN}"
S = "${WORKDIR}/${PN}"


PACKAGES = "${PN}-dbg gpe-conf gpe-conf-panel"

FILES_${PN} = "${sysconfdir} ${bindir} ${datadir}/pixmaps \
                ${datadir}/applications/gpe-conf-* ${datadir}/gpe/pixmaps \
                ${datadir}/gpe-conf"
FILES_gpe-conf-panel = "${datadir}/applications/gpe-conf.desktop"

