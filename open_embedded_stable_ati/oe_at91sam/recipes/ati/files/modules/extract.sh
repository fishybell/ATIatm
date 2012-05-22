#!/bin/sh

echo ""
echo "Self Extracting Firmware"
echo ""
SKIP=`awk '/^__TARFILE_FOLLOWS__/ { print NR + 2; exit 0; }' $0`

THIS=`pwd`/$0

echo "Extracting Files"
echo ""

tail -n +$SKIP $THIS | tar -xz

echo "Changing Owner and Group"
echo ""
chown root *
chgrp root *
chmod +x *
chown root ati2/files/*
chgrp root ati2/files/*
chmod +x ati2/files/*

echo "Copying files"
echo ""
mv *.arm /usr/bin
mv /usr/bin/bcast_client /usr/bin/bcast_client.old
mv bcast_client /usr/bin
mv /usr/bin/bcast_server /usr/bin/bcast_server.old
mv bcast_server /usr/bin
mv /usr/bin/bit_button /usr/bin/bit_button.old
mv bit_button /usr/bin
mv /usr/bin/event_conn /usr/bin/event_conn.old
mv event_conn /usr/bin
mv /usr/bin/fasit_conn /usr/bin/fasit_conn.old
mv fasit_conn /usr/bin
mv /usr/bin/user_conn /usr/bin/user_conn.old
mv user_conn /usr/bin
mv ati2/files/* /usr/bin
/usr/bin/be_major_version
/usr/bin/be_minor_version

echo "Finished Copying Files"
echo ""

echo "Restarting Device"
echo ""
sleep 1
init 6
exit 0

# DO NOT PUT ANYTHING AFTER THIS LINE, IF YOU DO THE EXTRACT WILL FAIL
__TARFILE_FOLLOWS__

