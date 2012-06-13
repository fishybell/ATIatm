#!/bin/sh
/usr/bin/dtxm_edit.arm -v f
if [ `/usr/bin/is_comm` == "radio" ] ; then
   killall RFslave.new.arm
   killall slaveboss.arm
fi
echo 'X' | /usr/bin/microcom -s 19200 /dev/ttyS1
sleep 1
echo 'X' | /usr/bin/microcom -s 19200 /dev/ttyS1
sleep 1
echo 'X' | /usr/bin/microcom -s 19200 /dev/ttyS1
sleep 1
echo 'X' | /usr/bin/microcom -s 19200 /dev/ttyS1
sleep 1
echo 'X' | /usr/bin/microcom -s 19200 /dev/ttyS1
sleep 1
if [ `/usr/bin/is_comm` == "radio" ] ; then
   init 6
fi
