#
# Makefile for making html and pdf documentation (etc) of the libpgtcl 
# interface.
#
# This requires all that complicated docbook sgml stuff, plus tex if you
# want to generate postscript or pdf.  You need at least
# openjade and pdfjadetex to run it, plus all the stuff they need.
#
# There is a lot of useful information on
#
#     http://www.postgresql.org/docs/7.3/static/doc-toolsets.html
#
# The man page generation requires docbook2X
#
#	http://docbook2x.sourceforge.net/
#


STYLESHEET=stylesheet.dsl
SGMLSOURCE=libpgtcl.sgml

all:	html man pdf

.PHONY: html man pdf clean

html:	html/HTML.index
html/HTML.index:	$(SGMLSOURCE)
	-rm -rf html
	-mkdir html
	cd html && docbook2html -s /usr/share/xml/docbook/schema -i output-html -d ../$(STYLESHEET) ../$(SGMLSOURCE)
	-find html/ -type f -name "*.html" -exec tidy -quiet -wrap 2000 -modify \{\} \;
	sh ./clean-html.sh html/*

man:	$(SGMLSOURCE)
	-rm -rf man
	-mkdir man
	cp $(SGMLSOURCE) man/$(SGMLSOURCE)
	cd man && docbook2man -V default-manpage-section=n $(SGMLSOURCE)
	rm man/$(SGMLSOURCE)*
	find man -name \*\. -exec mv {} {}n \;
	sh ./fix-fi.sh man/*
	sh ./clean-man.sh man/*


# Install Debian packages docbook-utils and ghostscript
pdf:	libpgtcl.pdf
libpgtcl.pdf:	$(SGMLSOURCE)
	rm -f libpgtcl.ps libpgtcl.pdf
	docbook2ps -V default-manpage-section=n $(SGMLSOURCE)
	ps2pdf libpgtcl.ps

clean:
	rm -rf man html libpgtcl.aux libpgtcl.log libpgtcl.out libpgtcl.tex-pdf libpgtcl.fot

all-man-docker:
	mkdir -p docker-context
	cd docker-context && docker build -t pgtcl-doc -f ../Dockerfile .
	docker run --rm -it -v $$PWD:/root/doc pgtcl-doc  make all
	docker rmi pgtcl-doc
