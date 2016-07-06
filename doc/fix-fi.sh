#!/bin/sh

for i
do
ed - $i << \!
g/.\.fi$/s/\.fi/\
.fi/
w
q
!
done
