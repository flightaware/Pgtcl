![Linux CI](https://github.com/flightaware/Pgtcl/workflows/Linux%20CI/badge.svg)
![Mac CI](https://github.com/flightaware/Pgtcl/workflows/Mac%20CI/badge.svg)

Thank you for downloading Pgtcl, a package that adds PostgreSQL interface extensions to the Tcl programming language... an open source project that's been in existence for nearly twenty years.

# CONFIGURING

Pgtcl is now Tcl Extension Architecture (TEA) compliant, shipping with a standard "configure" script.  It no longer needs to reside in a specific place within the Postgres source tree in order to build.  

For standard builds to put things in subdirectories of /usr/local, you can often simply execute configure with no arguments at all...

```sh
autoreconf
./configure
```

The configure script will attempt to find where the Postgres includes and libraries are using pg_config, a program built and installed as part of Postgres.  Alternatively, you can specify a path to the Postgres include files using --with-postgres-include and to the Postgres libraries using --with-postgres-lib.  

If you had PostgreSQL installed into /usr/postgres and a Tcl build in /usr/pptcl, you might use something like

```sh
./configure --prefix=/usr/pptcl
```

With this style of configure command, you'll need to make sure pg_config (and the correct pg_config if you have postgres installed in multiple places) is in the PATH.

Alternatively you can still explicitly specify where the Postgres includes and libraries are found:

```sh
./configure --prefix=/usr/pptcl --with-postgres-include=/usr/postgres/include --with-postgres-lib=/usr/postgres/lib
```

The other configure parameters that may need tweaking are where Tcl's includes and libraries (and tclConfig.sh) are.  Although normally they will be in /usr/local/include and /usr/local/lib, in some cases they may reside elsewhere.  If Tcl is built and installed from the FreeBSD ports tree, for example, they'll be in versioned subdirectories...

```sh
./configure  --with-tcl=/usr/local/lib/tcl8.6 --with-tclinclude=/usr/local/include/tcl8.6
```

# BUILDING

Do a `make`.  If all goes well, libpgtcl will be compiled and linked.

# INSTALLING

Do a `make install`

# USING IT

With version 1.4, Pgtcl is a standard package and can be loaded with "package require" instead of the shared library load routine, "load".

Fire up your tclsh:

```
tclsh8.6
% package require Pgtcl
2.1.0
```

It's a good idea to switch to using the ``package require`` instead of "load" to pick up Pgtcl, because there will be additional Tcl code shipped in future versions of Pgtcl, and using "package require" will make that code available to your application.  Also it keeps you from hard-coding the path to the library and hard-coding a dependency on a specific version.

## TROUBLESHOOTING

If it compiles and installs ok, but ``package require`` doesn't work, it probably didn't install into the search path Tcl uses to find extensions.  You might have more than one Tclsh installed.  Try adding a path to the parent directory of where the thing installed, for example, if it installed into /opt/local/bin

```
    tclsh8.6
    % lappend auto_path /opt/local/lib
    ...
    % package require Pgtcl
    2.1.0
```

# CREDITS

Pgtcl was originally written by Jolly Chen.  Many people have contributed to the development of Pgtcl over the years, including Randy Kunkee, who added the channel handler code, among many other improvements.

Karl Lehenbauer has been maintaining the software since 2002.  Brett Schwartz contributed many improvements and bug fixes between 2002 and 2011.  Jeff Lawson and Peter da Silva have made prolific contributions to Pgtcl since 2016.  (Special thanks also to Peter for cleaning up and updating the documentation.)

Additional thanks to David McNett for the work to migrate Pgtcl from Tcl 8.4 to 8.5, and to Pietro Cerutti for ideas, assistance, and for maintaining the Pgtcl port for FreeBSD.

If you have contributed to Pgtcl at any time since its inception and your name is not listed here, please file an issue and we will gladly acknowledge your contribution.
