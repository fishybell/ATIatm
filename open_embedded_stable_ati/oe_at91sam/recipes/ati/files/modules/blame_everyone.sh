#!/bin/bash

f=cvs.blame.all

cvs blame > $f 2>&1
cvs blame connector >> $f 2>&1
cvs blame fasit >> $f 2>&1

for i in johnyate shelly.b randy.ne brad.tut brynn nathanb ; do
   echo -n "$i: "
   grep $i $f | wc -l
done

rm $f
