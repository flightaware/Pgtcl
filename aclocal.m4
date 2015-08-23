#
# Include the TEA standard macro set
#

builtin(include,tclconfig/tcl.m4)

#
# Add here whatever m4 macros you want to define for your package
#

#--------------------------------------------------------------------
# PGTCL_CONFIG_CFLAGS
#
# a modified copy of TEA_CONFIG_CFLAGS
#
# The reason is that we have to set up search paths to the Tcl shared lib
# AND the postgresql libpq shared library.
#
# Unfortunately this is a big routine and our changes are small.  This is
# a maintenance problem as changes to TEA_CONFIG_CFLAGS in tcl.m4 need to be
# propagated.
#
# Basically anywhere you see LIB_RUNTIME_DIR you have to add
# LIB_PGTCL_RUNTIME_DIR, things like this:
#
#CC_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR},-R,${LIB_PGTCL_RUNTIME_DIR}'
#CC_SEARCH_FLAGS='-R${LIB_RUNTIME_DIR},-R,${LIB_PGTCL_RUNTIME_DIR}'
#LD_SEARCH_FLAGS='-R ${LIB_RUNTIME_DIR} -R ${LIB_PGTCL_RUNTIME_DIR}'
#CC_SEARCH_FLAGS='-L${LIB_RUNTIME_DIR} -L{LIB_PGTCL_RUNTIME_DIR}'
#CC_SEARCH_FLAGS='-Wl,+s,+b,${LIB_RUNTIME_DIR}:${LIB_PGTCL_RUNTIME_DIR}:.'
#LD_SEARCH_FLAGS='+s +b ${LIB_RUNTIME_DIR}:${LIB_PGTCL_RUNTIME_DIR}:.'
#CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'
#
#	Try to determine the proper flags to pass to the compiler
#	for building shared libraries and other such nonsense.
#
# Arguments:
#	none
#
# Results:
#
#	Defines and substitutes the following vars:
#
#	DL_OBJS, DL_LIBS - removed for TEA, only needed by core.
#       LDFLAGS -      Flags to pass to the compiler when linking object
#                       files into an executable application binary such
#                       as tclsh.
#       LD_SEARCH_FLAGS-Flags to pass to ld, such as "-R /usr/local/tcl/lib",
#                       that tell the run-time dynamic linker where to look
#                       for shared libraries such as libtcl.so.  Depends on
#                       the variable LIB_RUNTIME_DIR in the Makefile. Could
#                       be the same as CC_SEARCH_FLAGS if ${CC} is used to link.
#       CC_SEARCH_FLAGS-Flags to pass to ${CC}, such as "-Wl,-rpath,/usr/local/tcl/lib",
#                       that tell the run-time dynamic linker where to look
#                       for shared libraries such as libtcl.so.  Depends on
#                       the variable LIB_RUNTIME_DIR in the Makefile.
#       SHLIB_CFLAGS -  Flags to pass to cc when compiling the components
#                       of a shared library (may request position-independent
#                       code, among other things).
#       SHLIB_LD -      Base command to use for combining object files
#                       into a shared library.
#       SHLIB_LD_LIBS - Dependent libraries for the linker to scan when
#                       creating shared libraries.  This symbol typically
#                       goes at the end of the "ld" commands that build
#                       shared libraries. The value of the symbol defaults to
#                       "${LIBS}" if all of the dependent libraries should
#                       be specified when creating a shared library.  If
#                       dependent libraries should not be specified (as on
#                       SunOS 4.x, where they cause the link to fail, or in
#                       general if Tcl and Tk aren't themselves shared
#                       libraries), then this symbol has an empty string
#                       as its value.
#       SHLIB_SUFFIX -  Suffix to use for the names of dynamically loadable
#                       extensions.  An empty string means we don't know how
#                       to use shared libraries on this platform.
#       LIB_SUFFIX -    Specifies everything that comes after the "libfoo"
#                       in a static or shared library name, using the $PACKAGE_VERSION variable
#                       to put the version in the right place.  This is used
#                       by platforms that need non-standard library names.
#                       Examples:  ${PACKAGE_VERSION}.so.1.1 on NetBSD, since it needs
#                       to have a version after the .so, and ${PACKAGE_VERSION}.a
#                       on AIX, since a shared library needs to have
#                       a .a extension whereas shared objects for loadable
#                       extensions have a .so extension.  Defaults to
#                       ${PACKAGE_VERSION}${SHLIB_SUFFIX}.
#	CFLAGS_DEBUG -
#			Flags used when running the compiler in debug mode
#	CFLAGS_OPTIMIZE -
#			Flags used when running the compiler in optimize mode
#	CFLAGS -	Additional CFLAGS added as necessary (usually 64-bit)
#--------------------------------------------------------------------

AC_DEFUN([TEA_CONFIG_CFLAGS], [
    dnl TEA specific: Make sure we are initialized
    AC_REQUIRE([TEA_INIT])

    # Step 0.a: Enable 64 bit support?

    AC_MSG_CHECKING([if 64bit support is requested])
    AC_ARG_ENABLE(64bit,
	AC_HELP_STRING([--enable-64bit],
	    [enable 64bit support (default: off)]),
	[do64bit=$enableval], [do64bit=no])
    AC_MSG_RESULT([$do64bit])

    # Step 0.b: Enable Solaris 64 bit VIS support?

    AC_MSG_CHECKING([if 64bit Sparc VIS support is requested])
    AC_ARG_ENABLE(64bit-vis,
	AC_HELP_STRING([--enable-64bit-vis],
	    [enable 64bit Sparc VIS support (default: off)]),
	[do64bitVIS=$enableval], [do64bitVIS=no])
    AC_MSG_RESULT([$do64bitVIS])
    # Force 64bit on with VIS
    AS_IF([test "$do64bitVIS" = "yes"], [do64bit=yes])

    # Step 0.c: Check if visibility support is available. Do this here so
    # that platform specific alternatives can be used below if this fails.

    AC_CACHE_CHECK([if compiler supports visibility "hidden"],
	tcl_cv_cc_visibility_hidden, [
	hold_cflags=$CFLAGS; CFLAGS="$CFLAGS -Werror"
	AC_TRY_LINK([
	    extern __attribute__((__visibility__("hidden"))) void f(void);
	    void f(void) {}], [f();], tcl_cv_cc_visibility_hidden=yes,
	    tcl_cv_cc_visibility_hidden=no)
	CFLAGS=$hold_cflags])
    AS_IF([test $tcl_cv_cc_visibility_hidden = yes], [
	AC_DEFINE(MODULE_SCOPE,
	    [extern __attribute__((__visibility__("hidden")))],
	    [Compiler support for module scope symbols])
	AC_DEFINE(HAVE_HIDDEN, [1], [Compiler support for module scope symbols])
    ])

    # Step 0.d: Disable -rpath support?

    AC_MSG_CHECKING([if rpath support is requested])
    AC_ARG_ENABLE(rpath,
	AC_HELP_STRING([--disable-rpath],
	    [disable rpath support (default: on)]),
	[doRpath=$enableval], [doRpath=yes])
    AC_MSG_RESULT([$doRpath])

    # TEA specific: Cross-compiling options for Windows/CE builds?

    AS_IF([test "${TEA_PLATFORM}" = windows], [
	AC_MSG_CHECKING([if Windows/CE build is requested])
	AC_ARG_ENABLE(wince,
	    AC_HELP_STRING([--enable-wince],
		[enable Win/CE support (where applicable)]),
	    [doWince=$enableval], [doWince=no])
	AC_MSG_RESULT([$doWince])
    ])

    # Set the variable "system" to hold the name and version number
    # for the system.

    TEA_CONFIG_SYSTEM

    # Require ranlib early so we can override it in special cases below.

    AC_REQUIRE([AC_PROG_RANLIB])

    # Set configuration options based on system name and version.
    # This is similar to Tcl's unix/tcl.m4 except that we've added a
    # "windows" case and removed some core-only vars.

    do64bit_ok=no
    # default to '{$LIBS}' and set to "" on per-platform necessary basis
    SHLIB_LD_LIBS='${LIBS}'
    # When ld needs options to work in 64-bit mode, put them in
    # LDFLAGS_ARCH so they eventually end up in LDFLAGS even if [load]
    # is disabled by the user. [Bug 1016796]
    LDFLAGS_ARCH=""
    UNSHARED_LIB_SUFFIX=""
    # TEA specific: use PACKAGE_VERSION instead of VERSION
    TCL_TRIM_DOTS='`echo ${PACKAGE_VERSION} | tr -d .`'
    ECHO_VERSION='`echo ${PACKAGE_VERSION}`'
    TCL_LIB_VERSIONS_OK=ok
    CFLAGS_DEBUG=-g
    AS_IF([test "$GCC" = yes], [
	CFLAGS_OPTIMIZE=-O2
	CFLAGS_WARNING="-Wall"
    ], [
	CFLAGS_OPTIMIZE=-O
	CFLAGS_WARNING=""
    ])
    AC_CHECK_TOOL(AR, ar)
    STLIB_LD='${AR} cr'
    LD_LIBRARY_PATH_VAR="LD_LIBRARY_PATH"
    AS_IF([test "x$SHLIB_VERSION" = x],[SHLIB_VERSION=""],[SHLIB_VERSION=".$SHLIB_VERSION"])
    case $system in
	# TEA specific:
	windows)
	    # This is a 2-stage check to make sure we have the 64-bit SDK
	    # We have to know where the SDK is installed.
	    # This magic is based on MS Platform SDK for Win2003 SP1 - hobbs
	    # MACHINE is IX86 for LINK, but this is used by the manifest,
	    # which requires x86|amd64|ia64.
	    MACHINE="X86"
	    if test "$do64bit" != "no" ; then
		if test "x${MSSDK}x" = "xx" ; then
		    MSSDK="C:/Progra~1/Microsoft Platform SDK"
		fi
		MSSDK=`echo "$MSSDK" | sed -e  's!\\\!/!g'`
		PATH64=""
		case "$do64bit" in
		    amd64|x64|yes)
			MACHINE="AMD64" ; # default to AMD64 64-bit build
			PATH64="${MSSDK}/Bin/Win64/x86/AMD64"
			;;
		    ia64)
			MACHINE="IA64"
			PATH64="${MSSDK}/Bin/Win64"
			;;
		esac
		if test "$GCC" != "yes" -a ! -d "${PATH64}" ; then
		    AC_MSG_WARN([Could not find 64-bit $MACHINE SDK to enable 64bit mode])
		    AC_MSG_WARN([Ensure latest Platform SDK is installed])
		    do64bit="no"
		else
		    AC_MSG_RESULT([   Using 64-bit $MACHINE mode])
		    do64bit_ok="yes"
		fi
	    fi

	    if test "$doWince" != "no" ; then
		if test "$do64bit" != "no" ; then
		    AC_MSG_ERROR([Windows/CE and 64-bit builds incompatible])
		fi
		if test "$GCC" = "yes" ; then
		    AC_MSG_ERROR([Windows/CE and GCC builds incompatible])
		fi
		TEA_PATH_CELIB
		# Set defaults for common evc4/PPC2003 setup
		# Currently Tcl requires 300+, possibly 420+ for sockets
		CEVERSION=420; 		# could be 211 300 301 400 420 ...
		TARGETCPU=ARMV4;	# could be ARMV4 ARM MIPS SH3 X86 ...
		ARCH=ARM;		# could be ARM MIPS X86EM ...
		PLATFORM="Pocket PC 2003"; # or "Pocket PC 2002"
		if test "$doWince" != "yes"; then
		    # If !yes then the user specified something
		    # Reset ARCH to allow user to skip specifying it
		    ARCH=
		    eval `echo $doWince | awk -F, '{ \
	    if (length([$]1)) { printf "CEVERSION=\"%s\"\n", [$]1; \
	    if ([$]1 < 400)   { printf "PLATFORM=\"Pocket PC 2002\"\n" } }; \
	    if (length([$]2)) { printf "TARGETCPU=\"%s\"\n", toupper([$]2) }; \
	    if (length([$]3)) { printf "ARCH=\"%s\"\n", toupper([$]3) }; \
	    if (length([$]4)) { printf "PLATFORM=\"%s\"\n", [$]4 }; \
		    }'`
		    if test "x${ARCH}" = "x" ; then
			ARCH=$TARGETCPU;
		    fi
		fi
		OSVERSION=WCE$CEVERSION;
	    	if test "x${WCEROOT}" = "x" ; then
			WCEROOT="C:/Program Files/Microsoft eMbedded C++ 4.0"
		    if test ! -d "${WCEROOT}" ; then
			WCEROOT="C:/Program Files/Microsoft eMbedded Tools"
		    fi
		fi
		if test "x${SDKROOT}" = "x" ; then
		    SDKROOT="C:/Program Files/Windows CE Tools"
		    if test ! -d "${SDKROOT}" ; then
			SDKROOT="C:/Windows CE Tools"
		    fi
		fi
		WCEROOT=`echo "$WCEROOT" | sed -e 's!\\\!/!g'`
		SDKROOT=`echo "$SDKROOT" | sed -e 's!\\\!/!g'`
		if test ! -d "${SDKROOT}/${OSVERSION}/${PLATFORM}/Lib/${TARGETCPU}" \
		    -o ! -d "${WCEROOT}/EVC/${OSVERSION}/bin"; then
		    AC_MSG_ERROR([could not find PocketPC SDK or target compiler to enable WinCE mode [$CEVERSION,$TARGETCPU,$ARCH,$PLATFORM]])
		    doWince="no"
		else
		    # We could PATH_NOSPACE these, but that's not important,
		    # as long as we quote them when used.
		    CEINCLUDE="${SDKROOT}/${OSVERSION}/${PLATFORM}/include"
		    if test -d "${CEINCLUDE}/${TARGETCPU}" ; then
			CEINCLUDE="${CEINCLUDE}/${TARGETCPU}"
		    fi
		    CELIBPATH="${SDKROOT}/${OSVERSION}/${PLATFORM}/Lib/${TARGETCPU}"
    		fi
	    fi

	    if test "$GCC" != "yes" ; then
	        if test "${SHARED_BUILD}" = "0" ; then
		    runtime=-MT
	        else
		    runtime=-MD
	        fi

                if test "$do64bit" != "no" ; then
		    # All this magic is necessary for the Win64 SDK RC1 - hobbs
		    CC="\"${PATH64}/cl.exe\""
		    CFLAGS="${CFLAGS} -I\"${MSSDK}/Include\" -I\"${MSSDK}/Include/crt\" -I\"${MSSDK}/Include/crt/sys\""
		    RC="\"${MSSDK}/bin/rc.exe\""
		    lflags="-nologo -MACHINE:${MACHINE} -LIBPATH:\"${MSSDK}/Lib/${MACHINE}\""
		    LINKBIN="\"${PATH64}/link.exe\""
		    CFLAGS_DEBUG="-nologo -Zi -Od -W3 ${runtime}d"
		    CFLAGS_OPTIMIZE="-nologo -O2 -W2 ${runtime}"
		    # Avoid 'unresolved external symbol __security_cookie'
		    # errors, c.f. http://support.microsoft.com/?id=894573
		    TEA_ADD_LIBS([bufferoverflowU.lib])
		elif test "$doWince" != "no" ; then
		    CEBINROOT="${WCEROOT}/EVC/${OSVERSION}/bin"
		    if test "${TARGETCPU}" = "X86"; then
			CC="\"${CEBINROOT}/cl.exe\""
		    else
			CC="\"${CEBINROOT}/cl${ARCH}.exe\""
		    fi
		    CFLAGS="$CFLAGS -I\"${CELIB_DIR}/inc\" -I\"${CEINCLUDE}\""
		    RC="\"${WCEROOT}/Common/EVC/bin/rc.exe\""
		    arch=`echo ${ARCH} | awk '{print tolower([$]0)}'`
		    defs="${ARCH} _${ARCH}_ ${arch} PALM_SIZE _MT _WINDOWS"
		    if test "${SHARED_BUILD}" = "1" ; then
			# Static CE builds require static celib as well
		    	defs="${defs} _DLL"
		    fi
		    for i in $defs ; do
			AC_DEFINE_UNQUOTED($i, 1, [WinCE def ]$i)
		    done
		    AC_DEFINE_UNQUOTED(_WIN32_WCE, $CEVERSION, [_WIN32_WCE version])
		    AC_DEFINE_UNQUOTED(UNDER_CE, $CEVERSION, [UNDER_CE version])
		    CFLAGS_DEBUG="-nologo -Zi -Od"
		    CFLAGS_OPTIMIZE="-nologo -Ox"
		    lversion=`echo ${CEVERSION} | sed -e 's/\(.\)\(..\)/\1\.\2/'`
		    lflags="-MACHINE:${ARCH} -LIBPATH:\"${CELIBPATH}\" -subsystem:windowsce,${lversion} -nologo"
		    LINKBIN="\"${CEBINROOT}/link.exe\""
		    AC_SUBST(CELIB_DIR)
		else
		    RC="rc"
		    lflags="-nologo"
		    LINKBIN="link"
		    CFLAGS_DEBUG="-nologo -Z7 -Od -W3 -WX ${runtime}d"
		    CFLAGS_OPTIMIZE="-nologo -O2 -W2 ${runtime}"
		fi
	    fi

	    if test "$GCC" = "yes"; then
		# mingw gcc mode
		AC_CHECK_TOOL(RC, windres)
		CFLAGS_DEBUG="-g"
		CFLAGS_OPTIMIZE="-O2 -fomit-frame-pointer"
		SHLIB_LD='${CC} -shared'
		UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.a'
		LDFLAGS_CONSOLE="-wl,--subsystem,console ${lflags}"
		LDFLAGS_WINDOW="-wl,--subsystem,windows ${lflags}"

		AC_CACHE_CHECK(for cross-compile version of gcc,
			ac_cv_cross,
			AC_TRY_COMPILE([
			    #ifdef _WIN32
				#error cross-compiler
			    #endif
			], [],
			ac_cv_cross=yes,
			ac_cv_cross=no)
		      )
		      if test "$ac_cv_cross" = "yes"; then
			case "$do64bit" in
			    amd64|x64|yes)
				CC="x86_64-w64-mingw32-gcc"
				LD="x86_64-w64-mingw32-ld"
				AR="x86_64-w64-mingw32-ar"
				RANLIB="x86_64-w64-mingw32-ranlib"
				RC="x86_64-w64-mingw32-windres"
			    ;;
			    *)
				CC="i686-w64-mingw32-gcc"
				LD="i686-w64-mingw32-ld"
				AR="i686-w64-mingw32-ar"
				RANLIB="i686-w64-mingw32-ranlib"
				RC="i686-w64-mingw32-windres"
			    ;;
			esac
		fi

	    else
		SHLIB_LD="${LINKBIN} -dll ${lflags}"
		# link -lib only works when -lib is the first arg
		STLIB_LD="${LINKBIN} -lib ${lflags}"
		UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.lib'
		PATHTYPE=-w
		# For information on what debugtype is most useful, see:
		# http://msdn.microsoft.com/library/en-us/dnvc60/html/gendepdebug.asp
		# and also
		# http://msdn2.microsoft.com/en-us/library/y0zzbyt4%28VS.80%29.aspx
		# This essentially turns it all on.
		LDFLAGS_DEBUG="-debug -debugtype:cv"
		LDFLAGS_OPTIMIZE="-release"
		if test "$doWince" != "no" ; then
		    LDFLAGS_CONSOLE="-link ${lflags}"
		    LDFLAGS_WINDOW=${LDFLAGS_CONSOLE}
		else
		    LDFLAGS_CONSOLE="-link -subsystem:console ${lflags}"
		    LDFLAGS_WINDOW="-link -subsystem:windows ${lflags}"
		fi
	    fi

	    SHLIB_SUFFIX=".dll"
	    SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.dll'

	    TCL_LIB_VERSIONS_OK=nodots
    	    ;;
	AIX-*)
	    AS_IF([test "${TCL_THREADS}" = "1" -a "$GCC" != "yes"], [
		# AIX requires the _r compiler when gcc isn't being used
		case "${CC}" in
		    *_r|*_r\ *)
			# ok ...
			;;
		    *)
			# Make sure only first arg gets _r
		    	CC=`echo "$CC" | sed -e 's/^\([[^ ]]*\)/\1_r/'`
			;;
		esac
		AC_MSG_RESULT([Using $CC for compiling with threads])
	    ])
	    LIBS="$LIBS -lc"
	    SHLIB_CFLAGS=""
	    SHLIB_SUFFIX=".so"

	    LD_LIBRARY_PATH_VAR="LIBPATH"

	    # Check to enable 64-bit flags for compiler/linker
	    AS_IF([test "$do64bit" = yes], [
		AS_IF([test "$GCC" = yes], [
		    AC_MSG_WARN([64bit mode not supported with GCC on $system])
		], [
		    do64bit_ok=yes
		    CFLAGS="$CFLAGS -q64"
		    LDFLAGS_ARCH="-q64"
		    RANLIB="${RANLIB} -X64"
		    AR="${AR} -X64"
		    SHLIB_LD_FLAGS="-b64"
		])
	    ])

	    AS_IF([test "`uname -m`" = ia64], [
		# AIX-5 uses ELF style dynamic libraries on IA-64, but not PPC
		SHLIB_LD="/usr/ccs/bin/ld -G -z text"
		AS_IF([test "$GCC" = yes], [
		    CC_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR},-R,${LIB_PGTCL_RUNTIME_DIR}'
		], [
		    CC_SEARCH_FLAGS='-R${LIB_RUNTIME_DIR},-R,${LIB_PGTCL_RUNTIME_DIR}'
		])
		LD_SEARCH_FLAGS='-R ${LIB_RUNTIME_DIR} -R ${LIB_PGTCL_RUNTIME_DIR}'
	    ], [
		AS_IF([test "$GCC" = yes], [
		    SHLIB_LD='${CC} -shared -Wl,-bexpall'
		], [
		    SHLIB_LD="/bin/ld -bhalt:4 -bM:SRE -bexpall -H512 -T512 -bnoentry"
		    LDFLAGS="$LDFLAGS -brtl"
		])
		SHLIB_LD="${SHLIB_LD} ${SHLIB_LD_FLAGS}"
		CC_SEARCH_FLAGS='-L${LIB_RUNTIME_DIR} -L{LIB_PGTCL_RUNTIME_DIR}'
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    ])
	    ;;
	BeOS*)
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD='${CC} -nostart'
	    SHLIB_SUFFIX=".so"

	    #-----------------------------------------------------------
	    # Check for inet_ntoa in -lbind, for BeOS (which also needs
	    # -lsocket, even if the network functions are in -lnet which
	    # is always linked to, for compatibility.
	    #-----------------------------------------------------------
	    AC_CHECK_LIB(bind, inet_ntoa, [LIBS="$LIBS -lbind -lsocket"])
	    ;;
	BSD/OS-4.*)
	    SHLIB_CFLAGS="-export-dynamic -fPIC"
	    SHLIB_LD='${CC} -shared'
	    SHLIB_SUFFIX=".so"
	    LDFLAGS="$LDFLAGS -export-dynamic"
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	CYGWIN_*)
	    SHLIB_CFLAGS=""
	    SHLIB_LD='${CC} -shared'
	    SHLIB_LD_LIBS="${SHLIB_LD_LIBS} -Wl,--out-implib,\$[@].a"
	    SHLIB_SUFFIX=".dll"
	    EXEEXT=".exe"
	    do64bit_ok=yes
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	Haiku*)
	    LDFLAGS="$LDFLAGS -Wl,--export-dynamic"
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_SUFFIX=".so"
	    SHLIB_LD='${CC} -shared ${CFLAGS} ${LDFLAGS}'
	    AC_CHECK_LIB(network, inet_ntoa, [LIBS="$LIBS -lnetwork"])
	    ;;
	HP-UX-*.11.*)
	    # Use updated header definitions where possible
	    AC_DEFINE(_XOPEN_SOURCE_EXTENDED, 1, [Do we want to use the XOPEN network library?])
	    # TEA specific: Needed by Tcl, but not most extensions
	    #AC_DEFINE(_XOPEN_SOURCE, 1, [Do we want to use the XOPEN network library?])
	    #LIBS="$LIBS -lxnet"               # Use the XOPEN network library

	    AS_IF([test "`uname -m`" = ia64], [
		SHLIB_SUFFIX=".so"
		# Use newer C++ library for C++ extensions
		#if test "$GCC" != "yes" ; then
		#   CPPFLAGS="-AA"
		#fi
	    ], [
		SHLIB_SUFFIX=".sl"
	    ])
	    AC_CHECK_LIB(dld, shl_load, tcl_ok=yes, tcl_ok=no)
	    AS_IF([test "$tcl_ok" = yes], [
		LDFLAGS="$LDFLAGS -Wl,-E"
		CC_SEARCH_FLAGS='-Wl,+s,+b,${LIB_RUNTIME_DIR}:${LIB_PGTCL_RUNTIME_DIR}:.'
		LD_SEARCH_FLAGS='+s +b ${LIB_RUNTIME_DIR}:${LIB_PGTCL_RUNTIME_DIR}:.'
		LD_LIBRARY_PATH_VAR="SHLIB_PATH"
	    ])
	    AS_IF([test "$GCC" = yes], [
		SHLIB_LD='${CC} -shared'
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    ], [
		CFLAGS="$CFLAGS -z"
		# Users may want PA-RISC 1.1/2.0 portable code - needs HP cc
		#CFLAGS="$CFLAGS +DAportable"
		SHLIB_CFLAGS="+z"
		SHLIB_LD="ld -b"
	    ])

	    # Check to enable 64-bit flags for compiler/linker
	    AS_IF([test "$do64bit" = "yes"], [
		AS_IF([test "$GCC" = yes], [
		    case `${CC} -dumpmachine` in
			hppa64*)
			    # 64-bit gcc in use.  Fix flags for GNU ld.
			    do64bit_ok=yes
			    SHLIB_LD='${CC} -shared'
			    AS_IF([test $doRpath = yes], [
				CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'])
			    LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
			    ;;
			*)
			    AC_MSG_WARN([64bit mode not supported with GCC on $system])
			    ;;
		    esac
		], [
		    do64bit_ok=yes
		    CFLAGS="$CFLAGS +DD64"
		    LDFLAGS_ARCH="+DD64"
		])
	    ]) ;;
	IRIX-6.*)
	    SHLIB_CFLAGS=""
	    SHLIB_LD="ld -n32 -shared -rdata_shared"
	    SHLIB_SUFFIX=".so"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-rpath ${LIB_RUNTIME_DIR} -rpath ${LIB_PGTCL_RUNTIME_DIR}'])
	    AS_IF([test "$GCC" = yes], [
		CFLAGS="$CFLAGS -mabi=n32"
		LDFLAGS="$LDFLAGS -mabi=n32"
	    ], [
		case $system in
		    IRIX-6.3)
			# Use to build 6.2 compatible binaries on 6.3.
			CFLAGS="$CFLAGS -n32 -D_OLD_TERMIOS"
			;;
		    *)
			CFLAGS="$CFLAGS -n32"
			;;
		esac
		LDFLAGS="$LDFLAGS -n32"
	    ])
	    ;;
	IRIX64-6.*)
	    SHLIB_CFLAGS=""
	    SHLIB_LD="ld -n32 -shared -rdata_shared"
	    SHLIB_SUFFIX=".so"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-rpath ${LIB_RUNTIME_DIR} -rpath ${LIB_PGTCL_RUNTIME_DIR}'])

	    # Check to enable 64-bit flags for compiler/linker

	    AS_IF([test "$do64bit" = yes], [
	        AS_IF([test "$GCC" = yes], [
	            AC_MSG_WARN([64bit mode not supported by gcc])
	        ], [
	            do64bit_ok=yes
	            SHLIB_LD="ld -64 -shared -rdata_shared"
	            CFLAGS="$CFLAGS -64"
	            LDFLAGS_ARCH="-64"
	        ])
	    ])
	    ;;
	Linux*|GNU*|NetBSD-Debian)
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_SUFFIX=".so"

	    # TEA specific:
	    CFLAGS_OPTIMIZE="-O2 -fomit-frame-pointer"

	    # TEA specific: use LDFLAGS_DEFAULT instead of LDFLAGS
	    SHLIB_LD='${CC} -shared ${CFLAGS} ${LDFLAGS_DEFAULT}'
	    LDFLAGS="$LDFLAGS -Wl,--export-dynamic"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'])
	    LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    AS_IF([test "`uname -m`" = "alpha"], [CFLAGS="$CFLAGS -mieee"])
	    AS_IF([test $do64bit = yes], [
		AC_CACHE_CHECK([if compiler accepts -m64 flag], tcl_cv_cc_m64, [
		    hold_cflags=$CFLAGS
		    CFLAGS="$CFLAGS -m64"
		    AC_TRY_LINK(,, tcl_cv_cc_m64=yes, tcl_cv_cc_m64=no)
		    CFLAGS=$hold_cflags])
		AS_IF([test $tcl_cv_cc_m64 = yes], [
		    CFLAGS="$CFLAGS -m64"
		    do64bit_ok=yes
		])
	   ])

	    # The combo of gcc + glibc has a bug related to inlining of
	    # functions like strtod(). The -fno-builtin flag should address
	    # this problem but it does not work. The -fno-inline flag is kind
	    # of overkill but it works. Disable inlining only when one of the
	    # files in compat/*.c is being linked in.

	    AS_IF([test x"${USE_COMPAT}" != x],[CFLAGS="$CFLAGS -fno-inline"])
	    ;;
	Lynx*)
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_SUFFIX=".so"
	    CFLAGS_OPTIMIZE=-02
	    SHLIB_LD='${CC} -shared'
	    LD_FLAGS="-Wl,--export-dynamic"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'])
	    ;;
	OpenBSD-*)
	    arch=`arch -s`
	    case "$arch" in
	    vax)
		SHLIB_SUFFIX=""
		SHARED_LIB_SUFFIX=""
		LDFLAGS=""
		;;
	    *)
		case "$arch" in
		alpha|sparc64)
		    SHLIB_CFLAGS="-fPIC"
		    ;;
		*)
		    SHLIB_CFLAGS="-fpic"
		    ;;
		esac
		SHLIB_LD='${CC} -shared ${SHLIB_CFLAGS}'
		SHLIB_SUFFIX=".so"
		AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'])
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
		SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.so${SHLIB_VERSION}'
		LDFLAGS="-Wl,-export-dynamic"
		;;
	    esac
	    case "$arch" in
	    vax)
		CFLAGS_OPTIMIZE="-O1"
		;;
	    *)
		CFLAGS_OPTIMIZE="-O2"
		;;
	    esac
	    AS_IF([test "${TCL_THREADS}" = "1"], [
		# On OpenBSD:	Compile with -pthread
		#		Don't link with -lpthread
		LIBS=`echo $LIBS | sed s/-lpthread//`
		CFLAGS="$CFLAGS -pthread"
	    ])
	    # OpenBSD doesn't do version numbers with dots.
	    UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.a'
	    TCL_LIB_VERSIONS_OK=nodots
	    ;;
	NetBSD-*)
	    # NetBSD has ELF and can use 'cc -shared' to build shared libs
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD='${CC} -shared ${SHLIB_CFLAGS}'
	    SHLIB_SUFFIX=".so"
	    LDFLAGS="$LDFLAGS -export-dynamic"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'])
	    LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    AS_IF([test "${TCL_THREADS}" = "1"], [
		# The -pthread needs to go in the CFLAGS, not LIBS
		LIBS=`echo $LIBS | sed s/-pthread//`
		CFLAGS="$CFLAGS -pthread"
	    	LDFLAGS="$LDFLAGS -pthread"
	    ])
	    ;;
	FreeBSD-*)
	    # This configuration from FreeBSD Ports.
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD="${CC} -shared"
	    SHLIB_LD_LIBS="${SHLIB_LD_LIBS} -Wl,-soname,\$[@]"
	    SHLIB_SUFFIX=".so"
	    LDFLAGS=""
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'])
	    AS_IF([test "${TCL_THREADS}" = "1"], [
		# The -pthread needs to go in the LDFLAGS, not LIBS
		LIBS=`echo $LIBS | sed s/-pthread//`
		CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
		LDFLAGS="$LDFLAGS $PTHREAD_LIBS"])
	    case $system in
	    FreeBSD-3.*)
		# Version numbers are dot-stripped by system policy.
		TCL_TRIM_DOTS=`echo ${PACKAGE_VERSION} | tr -d .`
		UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.a'
		SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.so.1'
		TCL_LIB_VERSIONS_OK=nodots
		;;
	    esac
	    ;;
	Darwin-*)
	    CFLAGS_OPTIMIZE="-Os"
	    SHLIB_CFLAGS="-fno-common"
	    # To avoid discrepancies between what headers configure sees during
	    # preprocessing tests and compiling tests, move any -isysroot and
	    # -mmacosx-version-min flags from CFLAGS to CPPFLAGS:
	    CPPFLAGS="${CPPFLAGS} `echo " ${CFLAGS}" | \
		awk 'BEGIN {FS=" +-";ORS=" "}; {for (i=2;i<=NF;i++) \
		if ([$]i~/^(isysroot|mmacosx-version-min)/) print "-"[$]i}'`"
	    CFLAGS="`echo " ${CFLAGS}" | \
		awk 'BEGIN {FS=" +-";ORS=" "}; {for (i=2;i<=NF;i++) \
		if (!([$]i~/^(isysroot|mmacosx-version-min)/)) print "-"[$]i}'`"
	    AS_IF([test $do64bit = yes], [
		case `arch` in
		    ppc)
			AC_CACHE_CHECK([if compiler accepts -arch ppc64 flag],
				tcl_cv_cc_arch_ppc64, [
			    hold_cflags=$CFLAGS
			    CFLAGS="$CFLAGS -arch ppc64 -mpowerpc64 -mcpu=G5"
			    AC_TRY_LINK(,, tcl_cv_cc_arch_ppc64=yes,
				    tcl_cv_cc_arch_ppc64=no)
			    CFLAGS=$hold_cflags])
			AS_IF([test $tcl_cv_cc_arch_ppc64 = yes], [
			    CFLAGS="$CFLAGS -arch ppc64 -mpowerpc64 -mcpu=G5"
			    do64bit_ok=yes
			]);;
		    i386)
			AC_CACHE_CHECK([if compiler accepts -arch x86_64 flag],
				tcl_cv_cc_arch_x86_64, [
			    hold_cflags=$CFLAGS
			    CFLAGS="$CFLAGS -arch x86_64"
			    AC_TRY_LINK(,, tcl_cv_cc_arch_x86_64=yes,
				    tcl_cv_cc_arch_x86_64=no)
			    CFLAGS=$hold_cflags])
			AS_IF([test $tcl_cv_cc_arch_x86_64 = yes], [
			    CFLAGS="$CFLAGS -arch x86_64"
			    do64bit_ok=yes
			]);;
		    *)
			AC_MSG_WARN([Don't know how enable 64-bit on architecture `arch`]);;
		esac
	    ], [
		# Check for combined 32-bit and 64-bit fat build
		AS_IF([echo "$CFLAGS " |grep -E -q -- '-arch (ppc64|x86_64) ' \
		    && echo "$CFLAGS " |grep -E -q -- '-arch (ppc|i386) '], [
		    fat_32_64=yes])
	    ])
	    # TEA specific: use LDFLAGS_DEFAULT instead of LDFLAGS
	    SHLIB_LD='${CC} -dynamiclib ${CFLAGS} ${LDFLAGS_DEFAULT}'
	    AC_CACHE_CHECK([if ld accepts -single_module flag], tcl_cv_ld_single_module, [
		hold_ldflags=$LDFLAGS
		LDFLAGS="$LDFLAGS -dynamiclib -Wl,-single_module"
		AC_TRY_LINK(, [int i;], tcl_cv_ld_single_module=yes, tcl_cv_ld_single_module=no)
		LDFLAGS=$hold_ldflags])
	    AS_IF([test $tcl_cv_ld_single_module = yes], [
		SHLIB_LD="${SHLIB_LD} -Wl,-single_module"
	    ])
	    # TEA specific: link shlib with current and compatibility version flags
	    vers=`echo ${PACKAGE_VERSION} | sed -e 's/^\([[0-9]]\{1,5\}\)\(\(\.[[0-9]]\{1,3\}\)\{0,2\}\).*$/\1\2/p' -e d`
	    SHLIB_LD="${SHLIB_LD} -current_version ${vers:-0} -compatibility_version ${vers:-0}"
	    SHLIB_SUFFIX=".dylib"
	    # Don't use -prebind when building for Mac OS X 10.4 or later only:
	    AS_IF([test "`echo "${MACOSX_DEPLOYMENT_TARGET}" | awk -F '10\\.' '{print int([$]2)}'`" -lt 4 -a \
		"`echo "${CPPFLAGS}" | awk -F '-mmacosx-version-min=10\\.' '{print int([$]2)}'`" -lt 4], [
		LDFLAGS="$LDFLAGS -prebind"])
	    LDFLAGS="$LDFLAGS -headerpad_max_install_names"
	    AC_CACHE_CHECK([if ld accepts -search_paths_first flag],
		    tcl_cv_ld_search_paths_first, [
		hold_ldflags=$LDFLAGS
		LDFLAGS="$LDFLAGS -Wl,-search_paths_first"
		AC_TRY_LINK(, [int i;], tcl_cv_ld_search_paths_first=yes,
			tcl_cv_ld_search_paths_first=no)
		LDFLAGS=$hold_ldflags])
	    AS_IF([test $tcl_cv_ld_search_paths_first = yes], [
		LDFLAGS="$LDFLAGS -Wl,-search_paths_first"
	    ])
	    AS_IF([test "$tcl_cv_cc_visibility_hidden" != yes], [
		AC_DEFINE(MODULE_SCOPE, [__private_extern__],
		    [Compiler support for module scope symbols])
		tcl_cv_cc_visibility_hidden=yes
	    ])
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    LD_LIBRARY_PATH_VAR="DYLD_LIBRARY_PATH"
	    # TEA specific: for combined 32 & 64 bit fat builds of Tk
	    # extensions, verify that 64-bit build is possible.
	    AS_IF([test "$fat_32_64" = yes && test -n "${TK_BIN_DIR}"], [
		AS_IF([test "${TEA_WINDOWINGSYSTEM}" = x11], [
		    AC_CACHE_CHECK([for 64-bit X11], tcl_cv_lib_x11_64, [
			for v in CFLAGS CPPFLAGS LDFLAGS; do
			    eval 'hold_'$v'="$'$v'";'$v'="`echo "$'$v' "|sed -e "s/-arch ppc / /g" -e "s/-arch i386 / /g"`"'
			done
			CPPFLAGS="$CPPFLAGS -I/usr/X11R6/include"
			LDFLAGS="$LDFLAGS -L/usr/X11R6/lib -lX11"
			AC_TRY_LINK([#include <X11/Xlib.h>], [XrmInitialize();],
			    tcl_cv_lib_x11_64=yes, tcl_cv_lib_x11_64=no)
			for v in CFLAGS CPPFLAGS LDFLAGS; do
			    eval $v'="$hold_'$v'"'
			done])
		])
		AS_IF([test "${TEA_WINDOWINGSYSTEM}" = aqua], [
		    AC_CACHE_CHECK([for 64-bit Tk], tcl_cv_lib_tk_64, [
			for v in CFLAGS CPPFLAGS LDFLAGS; do
			    eval 'hold_'$v'="$'$v'";'$v'="`echo "$'$v' "|sed -e "s/-arch ppc / /g" -e "s/-arch i386 / /g"`"'
			done
			CPPFLAGS="$CPPFLAGS -DUSE_TCL_STUBS=1 -DUSE_TK_STUBS=1 ${TCL_INCLUDES} ${TK_INCLUDES}"
			LDFLAGS="$LDFLAGS ${TCL_STUB_LIB_SPEC} ${TK_STUB_LIB_SPEC}"
			AC_TRY_LINK([#include <tk.h>], [Tk_InitStubs(NULL, "", 0);],
			    tcl_cv_lib_tk_64=yes, tcl_cv_lib_tk_64=no)
			for v in CFLAGS CPPFLAGS LDFLAGS; do
			    eval $v'="$hold_'$v'"'
			done])
		])
		# remove 64-bit arch flags from CFLAGS et al. if configuration
		# does not support 64-bit.
		AS_IF([test "$tcl_cv_lib_tk_64" = no -o "$tcl_cv_lib_x11_64" = no], [
		    AC_MSG_NOTICE([Removing 64-bit architectures from compiler & linker flags])
		    for v in CFLAGS CPPFLAGS LDFLAGS; do
			eval $v'="`echo "$'$v' "|sed -e "s/-arch ppc64 / /g" -e "s/-arch x86_64 / /g"`"'
		    done])
	    ])
	    ;;
	OS/390-*)
	    CFLAGS_OPTIMIZE=""		# Optimizer is buggy
	    AC_DEFINE(_OE_SOCKETS, 1,	# needed in sys/socket.h
		[Should OS/390 do the right thing with sockets?])
	    ;;
	OSF1-V*)
	    # Digital OSF/1
	    SHLIB_CFLAGS=""
	    AS_IF([test "$SHARED_BUILD" = 1], [
	        SHLIB_LD='ld -shared -expect_unresolved "*"'
	    ], [
	        SHLIB_LD='ld -non_shared -expect_unresolved "*"'
	    ])
	    SHLIB_SUFFIX=".so"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-rpath ${LIB_RUNTIME_DIR} -rpath ${LIB_PGTCL_RUNTIME_DIR}'])
	    AS_IF([test "$GCC" = yes], [CFLAGS="$CFLAGS -mieee"], [
		CFLAGS="$CFLAGS -DHAVE_TZSET -std1 -ieee"])
	    # see pthread_intro(3) for pthread support on osf1, k.furukawa
	    AS_IF([test "${TCL_THREADS}" = 1], [
		CFLAGS="$CFLAGS -DHAVE_PTHREAD_ATTR_SETSTACKSIZE"
		CFLAGS="$CFLAGS -DTCL_THREAD_STACK_MIN=PTHREAD_STACK_MIN*64"
		LIBS=`echo $LIBS | sed s/-lpthreads//`
		AS_IF([test "$GCC" = yes], [
		    LIBS="$LIBS -lpthread -lmach -lexc"
		], [
		    CFLAGS="$CFLAGS -pthread"
		    LDFLAGS="$LDFLAGS -pthread"
		])
	    ])
	    ;;
	QNX-6*)
	    # QNX RTP
	    # This may work for all QNX, but it was only reported for v6.
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD="ld -Bshareable -x"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	SCO_SV-3.2*)
	    AS_IF([test "$GCC" = yes], [
		SHLIB_CFLAGS="-fPIC -melf"
		LDFLAGS="$LDFLAGS -melf -Wl,-Bexport"
	    ], [
		SHLIB_CFLAGS="-Kpic -belf"
		LDFLAGS="$LDFLAGS -belf -Wl,-Bexport"
	    ])
	    SHLIB_LD="ld -G"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	SunOS-5.[[0-6]])
	    # Careful to not let 5.10+ fall into this case

	    # Note: If _REENTRANT isn't defined, then Solaris
	    # won't define thread-safe library routines.

	    AC_DEFINE(_REENTRANT, 1, [Do we want the reentrant OS API?])
	    AC_DEFINE(_POSIX_PTHREAD_SEMANTICS, 1,
		[Do we really want to follow the standard? Yes we do!])

	    SHLIB_CFLAGS="-KPIC"
	    SHLIB_SUFFIX=".so"
	    AS_IF([test "$GCC" = yes], [
		SHLIB_LD='${CC} -shared'
		CC_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR},-R,${LIB_PGTCL_RUNTIME_DIR}'
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    ], [
		SHLIB_LD="/usr/ccs/bin/ld -G -z text"
		CC_SEARCH_FLAGS='-R ${LIB_RUNTIME_DIR} -R ${LIB_PGTCL_RUNTIME_DIR}'
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    ])
	    ;;
	SunOS-5*)
	    # Note: If _REENTRANT isn't defined, then Solaris
	    # won't define thread-safe library routines.

	    AC_DEFINE(_REENTRANT, 1, [Do we want the reentrant OS API?])
	    AC_DEFINE(_POSIX_PTHREAD_SEMANTICS, 1,
		[Do we really want to follow the standard? Yes we do!])

	    SHLIB_CFLAGS="-KPIC"

	    # Check to enable 64-bit flags for compiler/linker
	    AS_IF([test "$do64bit" = yes], [
		arch=`isainfo`
		AS_IF([test "$arch" = "sparcv9 sparc"], [
		    AS_IF([test "$GCC" = yes], [
			AS_IF([test "`${CC} -dumpversion | awk -F. '{print [$]1}'`" -lt 3], [
			    AC_MSG_WARN([64bit mode not supported with GCC < 3.2 on $system])
			], [
			    do64bit_ok=yes
			    CFLAGS="$CFLAGS -m64 -mcpu=v9"
			    LDFLAGS="$LDFLAGS -m64 -mcpu=v9"
			    SHLIB_CFLAGS="-fPIC"
			])
		    ], [
			do64bit_ok=yes
			AS_IF([test "$do64bitVIS" = yes], [
			    CFLAGS="$CFLAGS -xarch=v9a"
			    LDFLAGS_ARCH="-xarch=v9a"
			], [
			    CFLAGS="$CFLAGS -xarch=v9"
			    LDFLAGS_ARCH="-xarch=v9"
			])
			# Solaris 64 uses this as well
			#LD_LIBRARY_PATH_VAR="LD_LIBRARY_PATH_64"
		    ])
		], [AS_IF([test "$arch" = "amd64 i386"], [
		    AS_IF([test "$GCC" = yes], [
			case $system in
			    SunOS-5.1[[1-9]]*|SunOS-5.[[2-9]][[0-9]]*)
				do64bit_ok=yes
				CFLAGS="$CFLAGS -m64"
				LDFLAGS="$LDFLAGS -m64";;
			    *)
				AC_MSG_WARN([64bit mode not supported with GCC on $system]);;
			esac
		    ], [
			do64bit_ok=yes
			case $system in
			    SunOS-5.1[[1-9]]*|SunOS-5.[[2-9]][[0-9]]*)
				CFLAGS="$CFLAGS -m64"
				LDFLAGS="$LDFLAGS -m64";;
			    *)
				CFLAGS="$CFLAGS -xarch=amd64"
				LDFLAGS="$LDFLAGS -xarch=amd64";;
			esac
		    ])
		], [AC_MSG_WARN([64bit mode not supported for $arch])])])
	    ])

	    SHLIB_SUFFIX=".so"
	    AS_IF([test "$GCC" = yes], [
		SHLIB_LD='${CC} -shared'
		CC_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR},-R,${LIB_PGTCL_RUNTIME_DIR}'
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
		AS_IF([test "$do64bit_ok" = yes], [
		    AS_IF([test "$arch" = "sparcv9 sparc"], [
			# We need to specify -static-libgcc or we need to
			# add the path to the sparv9 libgcc.
			# JH: static-libgcc is necessary for core Tcl, but may
			# not be necessary for extensions.
			SHLIB_LD="$SHLIB_LD -m64 -mcpu=v9 -static-libgcc"
			# for finding sparcv9 libgcc, get the regular libgcc
			# path, remove so name and append 'sparcv9'
			#v9gcclibdir="`gcc -print-file-name=libgcc_s.so` | ..."
			#CC_SEARCH_FLAGS="${CC_SEARCH_FLAGS},-R,$v9gcclibdir"
		    ], [AS_IF([test "$arch" = "amd64 i386"], [
			# JH: static-libgcc is necessary for core Tcl, but may
			# not be necessary for extensions.
			SHLIB_LD="$SHLIB_LD -m64 -static-libgcc"
		    ])])
		])
	    ], [
		case $system in
		    SunOS-5.[[1-9]][[0-9]]*)
			# TEA specific: use LDFLAGS_DEFAULT instead of LDFLAGS
			SHLIB_LD='${CC} -G -z text ${LDFLAGS_DEFAULT}';;
		    *)
			SHLIB_LD='/usr/ccs/bin/ld -G -z text';;
		esac
		CC_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR},-R,${LIB_PGTCL_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-R ${LIB_RUNTIME_DIR} -R ${LIB_PGTCL_RUNTIME_DIR}'
	    ])
	    ;;
	UNIX_SV* | UnixWare-5*)
	    SHLIB_CFLAGS="-KPIC"
	    SHLIB_LD='${CC} -G'
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    # Some UNIX_SV* systems (unixware 1.1.2 for example) have linkers
	    # that don't grok the -Bexport option.  Test that it does.
	    AC_CACHE_CHECK([for ld accepts -Bexport flag], tcl_cv_ld_Bexport, [
		hold_ldflags=$LDFLAGS
		LDFLAGS="$LDFLAGS -Wl,-Bexport"
		AC_TRY_LINK(, [int i;], tcl_cv_ld_Bexport=yes, tcl_cv_ld_Bexport=no)
	        LDFLAGS=$hold_ldflags])
	    AS_IF([test $tcl_cv_ld_Bexport = yes], [
		LDFLAGS="$LDFLAGS -Wl,-Bexport"
	    ])
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    ;;
    esac

    AS_IF([test "$do64bit" = yes -a "$do64bit_ok" = no], [
	AC_MSG_WARN([64bit support being disabled -- don't know magic for this platform])
    ])

dnl # Add any CPPFLAGS set in the environment to our CFLAGS, but delay doing so
dnl # until the end of configure, as configure's compile and link tests use
dnl # both CPPFLAGS and CFLAGS (unlike our compile and link) but configure's
dnl # preprocessing tests use only CPPFLAGS.
    AC_CONFIG_COMMANDS_PRE([CFLAGS="${CFLAGS} ${CPPFLAGS}"; CPPFLAGS=""])

    # Add in the arch flags late to ensure it wasn't removed.
    # Not necessary in TEA, but this is aligned with core
    LDFLAGS="$LDFLAGS $LDFLAGS_ARCH"

    # If we're running gcc, then change the C flags for compiling shared
    # libraries to the right flags for gcc, instead of those for the
    # standard manufacturer compiler.

    AS_IF([test "$GCC" = yes], [
	case $system in
	    AIX-*) ;;
	    BSD/OS*) ;;
	    CYGWIN_*|MINGW32_*) ;;
	    IRIX*) ;;
	    NetBSD-*|FreeBSD-*|OpenBSD-*) ;;
	    Darwin-*) ;;
	    SCO_SV-3.2*) ;;
	    windows) ;;
	    *) SHLIB_CFLAGS="-fPIC" ;;
	esac])

    AS_IF([test "$tcl_cv_cc_visibility_hidden" != yes], [
	AC_DEFINE(MODULE_SCOPE, [extern],
	    [No Compiler support for module scope symbols])
    ])

    AS_IF([test "$SHARED_LIB_SUFFIX" = ""], [
    # TEA specific: use PACKAGE_VERSION instead of VERSION
    SHARED_LIB_SUFFIX='${PACKAGE_VERSION}${SHLIB_SUFFIX}'])
    AS_IF([test "$UNSHARED_LIB_SUFFIX" = ""], [
    # TEA specific: use PACKAGE_VERSION instead of VERSION
    UNSHARED_LIB_SUFFIX='${PACKAGE_VERSION}.a'])

    if test "${GCC}" = "yes" -a ${SHLIB_SUFFIX} = ".dll"; then
	AC_CACHE_CHECK(for SEH support in compiler,
	    tcl_cv_seh,
	AC_TRY_RUN([
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

	    int main(int argc, char** argv) {
		int a, b = 0;
		__try {
		    a = 666 / b;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		    return 0;
		}
		return 1;
	    }
	],
	    tcl_cv_seh=yes,
	    tcl_cv_seh=no,
	    tcl_cv_seh=no)
	)
	if test "$tcl_cv_seh" = "no" ; then
	    AC_DEFINE(HAVE_NO_SEH, 1,
		    [Defined when mingw does not support SEH])
	fi

	#
	# Check to see if the excpt.h include file provided contains the
	# definition for EXCEPTION_DISPOSITION; if not, which is the case
	# with Cygwin's version as of 2002-04-10, define it to be int,
	# sufficient for getting the current code to work.
	#
	AC_CACHE_CHECK(for EXCEPTION_DISPOSITION support in include files,
	    tcl_cv_eh_disposition,
	    AC_TRY_COMPILE([
#	    define WIN32_LEAN_AND_MEAN
#	    include <windows.h>
#	    undef WIN32_LEAN_AND_MEAN
	    ],[
		EXCEPTION_DISPOSITION x;
	    ],
		tcl_cv_eh_disposition=yes,
		tcl_cv_eh_disposition=no)
	)
	if test "$tcl_cv_eh_disposition" = "no" ; then
	AC_DEFINE(EXCEPTION_DISPOSITION, int,
		[Defined when cygwin/mingw does not support EXCEPTION DISPOSITION])
	fi

	# Check to see if winnt.h defines CHAR, SHORT, and LONG
	# even if VOID has already been #defined. The win32api
	# used by mingw and cygwin is known to do this.

	AC_CACHE_CHECK(for winnt.h that ignores VOID define,
	    tcl_cv_winnt_ignore_void,
	    AC_TRY_COMPILE([
#define VOID void
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
	    ], [
		CHAR c;
		SHORT s;
		LONG l;
	    ],
        tcl_cv_winnt_ignore_void=yes,
        tcl_cv_winnt_ignore_void=no)
	)
	if test "$tcl_cv_winnt_ignore_void" = "yes" ; then
	    AC_DEFINE(HAVE_WINNT_IGNORE_VOID, 1,
		    [Defined when cygwin/mingw ignores VOID define in winnt.h])
	fi
    fi

	# See if the compiler supports casting to a union type.
	# This is used to stop gcc from printing a compiler
	# warning when initializing a union member.

	AC_CACHE_CHECK(for cast to union support,
	    tcl_cv_cast_to_union,
	    AC_TRY_COMPILE([],
	    [
		  union foo { int i; double d; };
		  union foo f = (union foo) (int) 0;
	    ],
	    tcl_cv_cast_to_union=yes,
	    tcl_cv_cast_to_union=no)
	)
	if test "$tcl_cv_cast_to_union" = "yes"; then
	    AC_DEFINE(HAVE_CAST_TO_UNION, 1,
		    [Defined when compiler supports casting to union type.])
	fi

    AC_SUBST(CFLAGS_DEBUG)
    AC_SUBST(CFLAGS_OPTIMIZE)
    AC_SUBST(CFLAGS_WARNING)

    AC_SUBST(STLIB_LD)
    AC_SUBST(SHLIB_LD)

    AC_SUBST(SHLIB_LD_LIBS)
    AC_SUBST(SHLIB_CFLAGS)

    AC_SUBST(LD_LIBRARY_PATH_VAR)

    # These must be called after we do the basic CFLAGS checks and
    # verify any possible 64-bit or similar switches are necessary
    TEA_TCL_EARLY_FLAGS
    TEA_TCL_64BIT_FLAGS
])

# Local Variables:
# mode: autoconf
# End:

