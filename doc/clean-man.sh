#!/bin/sh

# Remove day of month to reduce git update spam.

for i
do
ed - $i << \!
g/^\.TH .* "PostgreSQL Tcl Interface Documentation"/s/n "[0-9][0-9]* /n "/
w
q
!
done

