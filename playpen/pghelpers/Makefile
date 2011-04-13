#
#
# $Id$
#

FILES= postgres-helpers.tcl
INSTALLDIR= /usr/local/lib/sc_postgres

all:	pkgIndex.tcl

pkgIndex.tcl:	$(FILES)
	echo 'pkg_mkIndex -verbose -- .' | tclsh8.5


install:	all
	mkdir -p $(INSTALLDIR)
	cp $(FILES) pkgIndex.tcl $(INSTALLDIR)

clean:
	rm -f pkgIndex.tcl
