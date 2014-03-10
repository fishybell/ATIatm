#!/bin/sh

# Use be_static to set the IP address to correctly
# be used in this file.
# See /etc/network/interfaces file for eth0 and eth0:0

# if no arguments fail, avahi will assign address
if [ $# == 0 ]; then
   exit 1
fi

add=`is_static`
if [ "$add" == "0.0.0.0" -o "$add" == "" -o "$add" == "dhcp" -o "$add" == "DHCP" ]; then
# if we want to be static, we need an address
   if [ "$1" == "static" ]; then
      exit 1
   fi
else
# if we want to use dhcp, do not have an address
   if [ "$1" == "dhcp" ]; then
      exit 1
   fi
fi
exit 0
