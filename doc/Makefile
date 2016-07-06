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
# $Id$
#


DOCBOOKDIR=/usr/local/share/sgml/docbook/dsssl/modular/catalog
STYLESHEET=stylesheet.dsl
SGMLSOURCE=libpgtcl.sgml

all:	html man
#
# Need to get pdfjadetex working
#
# all:	html libpgtcl.pdf

.PHONY: html man pdf clean

html:	html/HTML.index
html/HTML.index:	$(SGMLSOURCE)
	-rm html/*.html html/HTML.index
	-rmdir html
	-mkdir html
	cd html && openjade -V draft-mode -wall -wno-unused-param -wno-empty -D .. -c $(DOCBOOKDIR) -d $(STYLESHEET) -i output-html -t sgml $(SGMLSOURCE)
	-find html/ -type f -name "*.html" -exec tidy5 -quiet -wrap 2000 -modify \{\} \;

# docbook2X is written for version 5.0 of the DTD, so patch the SGML to claim it's 5.0
man:	$(SGMLSOURCE)
	-rm -rf man
	-mkdir man
	cp $(SGMLSOURCE) man/$(SGMLSOURCE)
	cd man && patch <../dtd-patch.txt $(SGMLSOURCE)
	cd man && docbook2man --string-param default-manpage-section=n $(SGMLSOURCE)
	rm man/$(SGMLSOURCE)*
	sh ./fix-fi.sh man/*


pdf:	libpgtcl.pdf
libpgtcl.pdf:	$(SGMLSOURCE)
	openjade -V draft-mode -D . -c $(DOCBOOKDIR) -d $(STYLESHEET) -t tex -V tex-backend -i output-print -V texpdf-output -o libpgtcl.tex-pdf $(SGMLSOURCE)
	pdfjadetex libpgtcl.tex-pdf
	rm -f libpgtcl.tex-pdf

clean:
	rm -f libpgtcl.aux libpgtcl.log libpgtcl.out libpgtcl.tex-pdf libpgtcl.fot
