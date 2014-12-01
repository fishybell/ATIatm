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
TARVAL=$?
if [ $TARVAL -ne 0 ]
then
   echo "Bad TAR file ... Contact Software Team"
   echo -n "Install FAILED ... Press any key"
   read -sn 1
   exit 1
else
   echo "Extract Successful ..."
fi

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
if [ -e dtxm_edit.arm ]
then
    ls *.arm | while read i ; do
       mv /usr/bin/$i /usr/bin/$i.old
       mv $i /usr/bin
    done
fi
if [ -e ./bcast_client ]
then
   mv /usr/bin/bcast_client /usr/bin/bcast_client.old
   mv bcast_client /usr/bin
fi
if [ -e ./bcast_server ]
then
   mv /usr/bin/bcast_server /usr/bin/bcast_server.old
   mv bcast_server /usr/bin
fi
if [ -e ./bit_button ]
then
   mv /usr/bin/bit_button /usr/bin/bit_button.old
   mv bit_button /usr/bin
fi
if [ -e ./event_conn ]
then
   mv /usr/bin/event_conn /usr/bin/event_conn.old
   mv event_conn /usr/bin
fi
if [ -e ./fasit_conn ]
then
   mv /usr/bin/fasit_conn /usr/bin/fasit_conn.old
   mv fasit_conn /usr/bin
fi
if [ -e ./user_conn ]
then
   mv /usr/bin/user_conn /usr/bin/user_conn.old
   mv user_conn /usr/bin
fi
if [ -e ./expose ]
then
   mv /usr/bin/expose /usr/bin/expose.old
   mv expose /usr/bin
fi
if [ -e ./conceal ]
then
   mv /usr/bin/conceal /usr/bin/conceal.old
   mv conceal /usr/bin
fi
if [ -e ./cgi/www/cgi-bin/bobberUpdate.arm ]
then
   mv /srv/www/cgi-bin/bobberUpdate.cgi /srv/www/cgi-bin/bobberUpdate.old
   cp ./cgi/www/cgi-bin/bobberUpdate.arm /srv/www/cgi-bin/bobberUpdate.cgi
   chmod u+s /srv/www/cgi-bin/bobberUpdate.cgi
fi
mv ati2/files/fixhost /etc/init.d
mv ati2/files/start_up /etc/init.d
mv ati2/files/networking /etc/init.d
mv ati2/files/thttpd /etc/init.d
mv ati2/files/K40networking /etc/rc6.d
mv ati2/files/interfaces /etc/network
mv ati2/files/* /usr/bin
mv sysvinit/sysvinit/inittab /etc

cp cgi/www/index.html /srv/www
cp cgi/www/ajax.js /srv/www
cp cgi/www/cgi-bin/expose.cgi /srv/www/cgi-bin
cp cgi/www/cgi-bin/conceal.cgi /srv/www/cgi-bin
chmod u+s /srv/www/cgi-bin/expose.cgi
chmod u+s /srv/www/cgi-bin/conceal.cgi

echo "Finished Copying Files"
echo ""

echo "Setting Bobbing values"
echo ""
/usr/bin/be_bobber 192.168.1.121 255.255.255.0 4 255 1 4 2 10 30


# uncomment this block to change radio settings
#if [ "$(eeprom_rw read -addr 0x550 -size 3)" -lt 240 ] ; then
#   echo "Changing radio to maximum awesomeness"
#   echo ""
#
#   echo "140.000" | eeprom_rw write -addr 0x500 -size 8 -blank 0x40
#   echo "255" | eeprom_rw write -addr 0x550 -size 3 -blank 4
#   echo "255" | eeprom_rw write -addr 0x554 -size 3 -blank 4
#   echo "25" | eeprom_rw write -addr 0x558 -size 2 -blank 2
#fi

# uncomment this block to change a SIT on a MIT to be a SIT on a MIT correctly
#if [ "$(is_board)" == "SIT" ] ; then
#   echo "Fixing SIT locality"
#   be_local
#   echo -n "auto" | /usr/bin/eeprom_rw write -addr 0xC0 -size 5 -blank 0x3F
#fi

# uncomment this block to change a MAT to a MATOLD
#if [ "$(is_board)" == "MAT" ] ; then
#   echo "Old-ifying MAT"
#   be_MATOLD
#fi

echo "Restarting Device"
echo ""
reboot
exit 0

# DO NOT PUT ANYTHING AFTER THIS LINE, IF YOU DO THE EXTRACT WILL FAIL
__TARFILE_FOLLOWS__

