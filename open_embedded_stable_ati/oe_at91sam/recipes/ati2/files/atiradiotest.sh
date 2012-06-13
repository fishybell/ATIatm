/usr/bin/dtxm_edit.arm -v f
killall RFslave.new.arm
killall slaveboss.arm
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
