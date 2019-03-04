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
	-rm -f html/*.html html/HTML.index
	-rmdir html
	-mkdir html
	cd html && docbook2html -i output-html -d ../$(STYLESHEET) ../$(SGMLSOURCE)
	-find html/ -type f -name "*.html" -exec tidy5 -quiet -wrap 2000 -modify \{\} \;
	sh ./clean-html.sh html/*

# docbook2X is written for version 5.0 of the DTD, so patch the SGML to claim it's 5.0
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
	rm -rf libpgtcl.ps libpgtcl.pdf
	docbook2ps -V default-manpage-section=n $(SGMLSOURCE)
	ps2pdf libpgtcl.ps

clean:
	rm -f libpgtcl.aux libpgtcl.log libpgtcl.out libpgtcl.tex-pdf libpgtcl.fot

all-man-docker:
	mkdir docker-context
	cd docker-context && docker build -t pgtcl-doc -f ../Dockerfile .
	docker run --rm -it -v $$PWD:/root/doc pgtcl-doc  make all
	docker rmi pgtcl-doc
