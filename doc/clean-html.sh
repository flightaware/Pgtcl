#!/bin/sh

# Remove gratuitous changed lines to cut down on spam in git diffs.

for i
do
ed - $i << \!
/<meta name="creation" content=/d
w
q
.
!
done
