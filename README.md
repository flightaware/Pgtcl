Thank you for downloading Pgtcl, a package that adds PostgreSQL interface
extensions to the Tcl programming language... an open source project that's 
been in existence for more than ten years.

# CONFIGURING

Pgtcl is now Tcl Extension Architecture (TEA) compliant, shipping with
a standard "configure" script.  It no longer needs to reside in a specific
place within the Postgres source tree in order to build.  

For standard builds to put things in subdirectories of /usr/local, you
can often simply execute configure with no arguments at all...

    ./configure

The configure script will attempt to find where the Postgres includes and
libraries are using pg_config, a program built and installed as part of
Postgres.  Alternatively, you can specify a path to the Postgres 
include files using --with-postgres-include and to the Postgres libraries 
using --with-postgres-lib.  

If you had PostgreSQL installed into /usr/postgres and a Tcl build in
/usr/pptcl, you might use something like

    ./configure --prefix=/usr/pptcl

With this style of configure command, you'll need to make sure pg_config
(and the correct pg_config if you have postgres installed in multiple
places) is in the PATH.

Alternatively you can still explicitly specify where the Postgres includes
and libraries are found:

    ./configure --prefix=/usr/pptcl --with-postgres-include=/usr/postgres/include --with-postgres-lib=/usr/postgres/lib

The other configure parameters that may need tweaking are where Tcl's
includes and libraries (and tclConfig.sh) are.  Although normally they
will be in /usr/local/include and /usr/local/lib, in some cases they
may reside elsewhere.  If Tcl is built and installed from the FreeBSD ports 
tree, for example, they'll be in versioned subdirectories...

    ./configure  --with-tcl=/usr/local/lib/tcl8.4 --with-tclinclude=/usr/local/include/tcl8.4


# BUILDING

Do a `make`.  If all goes well, libpgtcl will be compiled and linked.

# INSTALLING

Do a `make install`

# USING IT

With version 1.4, Pgtcl is a standard package and can be loaded with
"package require" instead of the shared library load routine, "load".

Fire up your tclsh:

    tclsh8.5
    % package require Pgtcl
    1.4

It's a good idea to switch to using the "package require' instead of "load"
to pick up Pgtcl, because there will be additional Tcl code shipped in future 
versions of Pgtcl, and using "package require" will make that code available 
to your application.  Also it keeps you from hard-coding the path to the
library and hard-coding a dependency on a specific version.

IF IT COMPILES AND INSTALLS OK BUT "PACKAGE REQUIRE" DOESN'T WORK

...it probably didn't install into the search path Tcl uses to find
extensions.  You might have more than one Tclsh installed.  Try adding
a path to the parent directory of where the thing installed, for example,
if it installed into /opt/local/bin

    tclsh8.4
    % lappend auto_path /opt/local/lib
    ...
    % package require Pgtcl
    1.4

# CREDITS

Pgtcl was originally written by Jolly Chen.  Many people have contributed to 
the further development of Pgtcl over the years, including Randy Kunkee,
who added the channel handler code, among other things, and we intend to
identify the rest of them and give them credit as well.

Development and maintenance of Pgtcl since version 1.3 has been done by Brett 
Schwarz and Karl Lehenbauer.

# CHANGELOG

## VERSION 1.5

New options to pg_connect, -connhandle and -connlist

experimental -dict option in pg_result

Proper Tcl namespace support, in the ::pg namespace.

Connection handles and result handles are now executable commands in their
own right, while maintaining compatible with their former usage as strictly
handles.

Upgraded to TEA 3.1 compliant build.

Documentation overhauled and brought current with the code.

## VERSION 1.4

With version 1.4, Pgtcl has been internally overhauled and brought up to
date with the latest Tcl C-interface technology, while maintaining
nearly 100% compatibility with the pg_* Tcl interface provided by Pgtcl 1.3.

Just about every Tcl program that uses Pgtcl 1.3 will work without modification
under Pgtcl 1.4.

Version 1.4 was something of a transitional release, as pgtcl moved out the 
of core and into its this distribution.  Previously, for example, the
Pgtcl documentation resided with the rest of the PostgreSQL documentation,
and Pgtcl's source code accessed PostgreSQL's include files and libraries
in a fraternal manner that had to be divorced and reworked to use the same
APIs and build methods that any external application would use to build against
PostgreSQL's libpq C interface library.  

The Pgtcl documentation is now included with this release.  As building the
documentation requires a number of fairly major tools and packages, the
release also includes the docs prebuilt in HTML and PDF format.

## CHANGES

The main changes are:

 * All commands have now been converted to use Tcl 8-style Tcl objects.

   The result is a probable increase in performance in all routines, with
   potentially huge performance increases in pg_select and pg_execute when
   used with complex Tcl code bodies.

 * Also Tcl 7 is no longer supported.  (Surely you're not still
   using it, anyway, right?)

 * A new asynchronous interface has been added

   Requests can be issued to the backend without waiting for the
   results, allowing for user interfaces to still work while
   database requests are being processed, etc.
   Also, requests can now be cancelled while they're in progress.

 * pg_* call arguments are now checked much more rigorously.

   Code previously using atoi() for integer conversions now
   uses Tcl_GetIntFromObj, etc.

   pg_* calls with too many arguments were often accepted without
   complaint.  These now generate standard Tcl "wrong # args"
   messages, etc.

   Error reporting has been brought into more compliance with the
   Tcl way of doing things.

 * TEA-compliant build and install.

 * pg_exec now supports $-variable substitution.  This is a new
   feature of Postgres 7.4 that is now supported in Pgtcl.
   You can now say:

    pg_exec $conn {select * from foo where id = $1 and user = $2} $id $user

   And the values following the SQL statement are substituted positionally
   for $1, $2, etc, in the statement.

 * pg_exec_prepared allows execution of prepared SQL statements.

 * pg_sendquery_prepared allows asynchronous execution of prepared
   SQL statements.

Some programs that might have been working properly but had certain
syntatically incorrect pg_* commands will now fail until fixed.

pg_result -assign and pg_result -assignbyidx used to return the array
name, which was superfluous because the array name was specified on the
command line.  They now return nothing.  *** POTENTIAL INCOMPATIBILITY ***

## OLDER CHANGES

Here are some features that were added back in the 1998 - 1999 timeframe:

 * Postgres connections are a valid Tcl channel, and can therefore
   be manipulated by the interp command (ie. shared or transfered).
   A connection handle's results are transfered/shared with it.
   (Result handles are NOT channels, though it was tempting).  Note
   that a "close $connection" is now functionally identical to a
   "pg_disconnect $connection", although pg_connect must be used
   to create a connection.
   
 * Result handles are changed in format: ${connection}.<result#>.
   This just means for a connection 'pgtcl0', they look like pgtcl0.0,
   pgtcl0.1, etc.  Enforcing this syntax makes it easy to look up
   the real pointer by indexing into an array associated with the
   connection.

 * I/O routines are now defined for the connection handle.  I/O to/from
   the connection is only valid under certain circumstances: following
   the execution of the queries "copy <table> from stdin" or
   "copy <table> to stdout".  In these cases, the result handle obtains
   an intermediate status of "PGRES_COPY_IN" or "PGRES_COPY_OUT".  The
   programmer is then expected to use Tcl gets or read commands on the
   database connection (not the result handle) to extract the copy data.
   For copy outs, read until the standard EOF indication is encountered.
   For copy ins, puts a single terminator (\.).  The statement for this
   would be `puts $conn "\\."` or `puts $conn {\.}`
   In either case (upon detecting the EOF or putting the '\.', the status
   of the result handle will change to "PGRES_COMMAND_OK", and any further
   I/O attempts will cause a Tcl error.
