#!/bin/sh

echo ""
echo "Self Extracting Firmware"
echo ""
SKIP=`awk '/^__TARFILE_FOLLOWS__/ { print NR + 2; exit 0; }' $0`

THIS=`pwd`/$0

# stop everything we can to make stuff smoother
#/usr/bin/stop -- no, don't make smooth manually via mv blah blah.old...etc (stop can make movers crash sometimes)

echo "Extracting Files"
echo ""

tail -n +$SKIP $THIS | tar -xj

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
ls *.arm | while read i ; do
   mv /usr/bin/$i /usr/bin/$i.old
   mv $i /usr/bin
done
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
mv sysvinit/sysvinit/inittab /etc

echo "Finished Copying Files"
echo ""

#if [ "$(eeprom_rw read -addr 0x550 -size 3)" -lt 200 ] ; then
#   echo "Changing radio to maximum awesomeness"
#   echo ""
#
#   echo "255" | eeprom_rw write -addr 0x550 -size 3 -blank 4
#   echo "255" | eeprom_rw write -addr 0x554 -size 3 -blank 4
#   echo "25" | eeprom_rw write -addr 0x558 -size 2 -blank 2
#fi

echo "Restarting Device"
echo ""
sleep 5
init 6
exit 0

# DO NOT PUT ANYTHING AFTER THIS LINE, IF YOU DO THE EXTRACT WILL FAIL
__TARFILE_FOLLOWS__

