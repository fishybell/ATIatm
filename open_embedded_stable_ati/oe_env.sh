if [ `id -g` != 206 ] ; then
    echo "Changed groups to ENG, please re-source this file"
    newgrp eng
    echo "Group restored"
fi
export OE_DIR=${PWD}
export PATH=/usr/local/bin/bitbake/bin:$PATH
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

if [ ! -f .cvsignore ] ; then
    echo ".cvsignore" > .cvsignore
fi

for i in tmp oe_sources openembedded ; do
    if [ ! -h $i ] ; then
        echo "creating $i link"
        ln -s /usr/local/var/oe/$i $i
        echo "$i" >> .cvsignore
    fi
done

if [ ! -d flash ] ; then
    mkdir flash
    if [ ! -f .cvsignore ] ; then
        touch .cvsignore
    fi
    echo "flash" >> .cvsignore
fi
