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
#
# $Id$
#


DOCBOOKDIR=/usr/local/share/sgml/docbook/dsssl/modular/catalog
STYLESHEET=stylesheet.dsl
SGMLSOURCE=libpgtcl.sgml

all:	libpgtcl.pdf

dohtml:
	-rm html/*.html
	-openjade -V draft-mode -wall -wno-unused-param -wno-empty -D . -c $(DOCBOOKDIR) -d $(STYLESHEET) -i output-html -t sgml $(SGMLSOURCE)
	-mkdir html
	mv *.html html

libpgtcl.pdf:	$(SGMLSOURCE)
	openjade -V draft-mode -D . -c $(DOCBOOKDIR) -d $(STYLESHEET) -t tex -V tex-backend -i output-print -V texpdf-output -o libpgtcl.tex-pdf $(SGMLSOURCE)
	pdfjadetex libpgtcl.tex-pdf
	rm -f libpgtcl.tex-pdf

clean:
	rm -f libpgtcl.aux libpgtcl.log libpgtcl.out HTML.index libpgtcl.tex-pdf libpgtcl.fot
