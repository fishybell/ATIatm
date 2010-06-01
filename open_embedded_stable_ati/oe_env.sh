export OE_DIR=${PWD}
export PATH=${OE_DIR}/bitbake/bin:${OE_DIR}/sam-ba:$PATH
export BBPATH=${OE_DIR}/oe_at91sam:${OE_DIR}/openembedded

if [ -z ${ORG_PATH} ] ; then
	ORG_PATH=${PATH}
	export ORG_PATH
fi

if [ -z ${ORG_LD_LIBRARY_PATH} ] ; then
	ORG_LD_LIBRARY_PATH=${LD_LIBRARY_PATH}
	export ORG_LD_LIBRARY_PATH
fi

LD_LIBRARY_PATH=
export PATH LD_LIBRARY_PATH BBPATH
export LANG=C
export BB_ENV_EXTRAWHITE="MACHINE DISTRO ANGSTROM_MODE"
 
sudo rmmod usbserial
sudo modprobe usbserial vendor=0x03eb product=0x6124
