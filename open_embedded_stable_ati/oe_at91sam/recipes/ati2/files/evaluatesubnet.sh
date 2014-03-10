#!/bin/sh

add=`is_subnet`
if [ "$add" == "0.0.0.0" -o "$add" == "" ]; then
   echo -e "255.255.252.0"
else
   echo -e $add
fi
exit 0
