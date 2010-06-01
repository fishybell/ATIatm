require psplash.inc
require psplash-ua.inc

ALTERNATIVE_PRIORITY = "20"

# You can create your own pslash-hand-img.h by doing
# ./make-image-header.sh <file>.png HAND
# and rename the resulting .h to pslash-hand-img.h (for the logo)
# respectively psplash-bar-img.h (BAR) for the bar.
# You might also want to patch the colors (see patch)

SRC_URI = "svn://svn.o-hand.com/repos/misc/trunk;module=psplash;proto=http \
          file://logo-math.patch;patch=1 \
          file://psplash-hand-img.h \
          file://psplash-bar-img.h \
          file://psplash-default \
          file://psplash-init"

SRC_URI_append_at91sam9261ek = " file://fb_bgr_color.patch;patch=1"
SRC_URI_append_at91sam9263ek = " file://fb_bgr_color.patch;patch=1"

PACKAGE_ARCH_at91sam9261ek  = "${MACHINE_ARCH}"
PACKAGE_ARCH_at91sam9263ek  = "${MACHINE_ARCH}"

S = "${WORKDIR}/psplash"

