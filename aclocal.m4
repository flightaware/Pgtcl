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
# The reason is that we have to set up search paths to the Tcl shared
# lib AND the postgres libpq shared lib.
#
# Unfortunately this is a big routine and our changes are small.
# This is a maintenance problem as changes to TEA_CONFIG_CFLAGS in
# tcl.m4 need to be propagated.
#
#	Try to determine the proper flags to pass to the compiler
#	for building shared libraries and other such nonsense.
#
# Arguments:
#	none
#
# Results:
#
#	Defines the following vars:
#
#       DL_OBJS -       Name of the object file that implements dynamic
#                       loading for Tcl on this system.
#       DL_LIBS -       Library file(s) to include in tclsh and other base
#                       applications in order for the "load" command to work.
#       LDFLAGS -      Flags to pass to the compiler when linking object
#                       files into an executable application binary such
#                       as tclsh.
#       LD_SEARCH_FLAGS-Flags to pass to ld, such as "-R /usr/local/tcl/lib",
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
#                       shared libraries. The value of the symbol is
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
#       TCL_LIB_FILE -  Name of the file that contains the Tcl library, such
#                       as libtcl7.8.so or libtcl7.8.a.
#       TCL_LIB_SUFFIX -Specifies everything that comes after the "libtcl"
#                       in the shared library name, using the
#                       ${PACKAGE_VERSION} variable to put the version in
#                       the right place.  This is used by platforms that
#                       need non-standard library names.
#                       Examples:  ${PACKAGE_VERSION}.so.1.1 on NetBSD,
#                       since it needs to have a version after the .so, and
#                       ${PACKAGE_VERSION}.a on AIX, since the Tcl shared
#                       library needs to have a .a extension whereas shared
#                       objects for loadable extensions have a .so
#                       extension.  Defaults to
#                       ${PACKAGE_VERSION}${SHLIB_SUFFIX}.
#       TCL_NEEDS_EXP_FILE -
#                       1 means that an export file is needed to link to a
#                       shared library.
#       TCL_EXP_FILE -  The name of the installed export / import file which
#                       should be used to link to the Tcl shared library.
#                       Empty if Tcl is unshared.
#       TCL_BUILD_EXP_FILE -
#                       The name of the built export / import file which
#                       should be used to link to the Tcl shared library.
#                       Empty if Tcl is unshared.
#	CFLAGS_DEBUG -
#			Flags used when running the compiler in debug mode
#	CFLAGS_OPTIMIZE -
#			Flags used when running the compiler in optimize mode
#	CFLAGS -	We add CFLAGS to pass to the compiler
#
#	Subst's the following vars:
#		DL_LIBS
#		CFLAGS_DEBUG
#		CFLAGS_OPTIMIZE
#		CFLAGS_WARNING
#
#		STLIB_LD
#		SHLIB_LD
#		SHLIB_CFLAGS
#		SHLIB_LD_FLAGS
#		LDFLAGS_DEBUG
#		LDFLAGS_OPTIMIZE
#--------------------------------------------------------------------

AC_DEFUN(PGTCL_CONFIG_CFLAGS, [
    dnl Make sure we are initialized
    AC_REQUIRE([TEA_INIT])

    # Step 0: Enable 64 bit support?

    AC_MSG_CHECKING([if 64bit support is enabled])
    AC_ARG_ENABLE(64bit,[  --enable-64bit          enable 64bit support (where applicable)], [do64bit=$enableval], [do64bit=no])
    AC_MSG_RESULT([$do64bit])
 
    # Step 0.b: Enable Solaris 64 bit VIS support?

    AC_MSG_CHECKING([if 64bit Sparc VIS support is requested])
    AC_ARG_ENABLE(64bit-vis,[  --enable-64bit-vis      enable 64bit Sparc VIS support], [do64bitVIS=$enableval], [do64bitVIS=no])
    AC_MSG_RESULT([$do64bitVIS])

    if test "$do64bitVIS" = "yes"; then
	# Force 64bit on with VIS
	do64bit=yes
    fi

    # Step 0.c: Cross-compiling options for Windows/CE builds?

    if test "${TEA_PLATFORM}" = "windows" ; then
	AC_MSG_CHECKING([if Windows/CE build is requested])
	AC_ARG_ENABLE(wince,[  --enable-wince          enable Win/CE support (where applicable)], [doWince=$enableval], [doWince=no])
	AC_MSG_RESULT($doWince)
    fi

    # Step 1: set the variable "system" to hold the name and version number
    # for the system.  This can usually be done via the "uname" command, but
    # there are a few systems, like Next, where this doesn't work.

    AC_MSG_CHECKING([system version (for dynamic loading)])
    if test -f /usr/lib/NextStep/software_version; then
	system=NEXTSTEP-`awk '/3/,/3/' /usr/lib/NextStep/software_version`
    else
	system=`uname -s`-`uname -r`
	if test "$?" -ne 0 ; then
	    AC_MSG_RESULT([unknown (can't find uname command)])
	    system=unknown
	else
	    # Special check for weird MP-RAS system (uname returns weird
	    # results, and the version is kept in special file).
	
	    if test -r /etc/.relid -a "X`uname -n`" = "X`uname -s`" ; then
		system=MP-RAS-`awk '{print $3}' /etc/.relid'`
	    fi
	    if test "`uname -s`" = "AIX" ; then
		system=AIX-`uname -v`.`uname -r`
	    fi
	    if test "${TEA_PLATFORM}" = "windows" ; then
		system=windows
	    fi
	    AC_MSG_RESULT([$system])
	fi
    fi

    # Step 2: check for existence of -ldl library.  This is needed because
    # Linux can use either -ldl or -ldld for dynamic loading.

    AC_CHECK_LIB(dl, dlopen, have_dl=yes, have_dl=no)

    # Step 3: set configuration options based on system name and version.
    # This is similar to Tcl's unix/tcl.m4 except that we've added a
    # "windows" case and CC_SEARCH_FLAGS becomes LD_SEARCH_FLAGS for us
    # (and we have no CC_SEARCH_FLAGS).

    do64bit_ok=no
    LDFLAGS_ORIG="$LDFLAGS"
    TCL_EXPORT_FILE_SUFFIX=""
    UNSHARED_LIB_SUFFIX=""
    TCL_TRIM_DOTS='`echo ${PACKAGE_VERSION} | tr -d .`'
    ECHO_VERSION='`echo ${PACKAGE_VERSION}`'
    TCL_LIB_VERSIONS_OK=ok
    CFLAGS_DEBUG=-g
    CFLAGS_OPTIMIZE=-O
    if test "$GCC" = "yes" ; then
	CFLAGS_WARNING="-Wall -Wno-implicit-int"
    else
	CFLAGS_WARNING=""
    fi
    TCL_NEEDS_EXP_FILE=0
    TCL_BUILD_EXP_FILE=""
    TCL_EXP_FILE=""
dnl FIXME: Replace AC_CHECK_PROG with AC_CHECK_TOOL once cross compiling is fixed.
dnl AC_CHECK_TOOL(AR, ar, :)
    AC_CHECK_PROG(AR, ar, ar)
    STLIB_LD='${AR} cr'
    LD_LIBRARY_PATH_VAR="LD_LIBRARY_PATH"
    case $system in
	windows)
	    # This is a 2-stage check to make sure we have the 64-bit SDK
	    # We have to know where the SDK is installed.
	    if test "$do64bit" = "yes" ; then
		if test "x${MSSDK}x" = "xx" ; then
		    MSSDK="C:/Progra~1/Microsoft SDK"
		fi
		# In order to work in the tortured autoconf environment,
		# we need to ensure that this path has no spaces
		MSSDK=`cygpath -w -s "$MSSDK" | sed -e 's!\\\!/!g'`
		if test ! -d "${MSSDK}/bin/win64" ; then
		    AC_MSG_WARN("could not find 64-bit SDK to enable 64bit mode")
		    do64bit="no"
		else
		    do64bit_ok="yes"
		fi
	    fi

	    if test "$doWince" != "no" ; then
		if test "$do64bit" = "yes" ; then
		    AC_MSG_ERROR([Windows/CE and 64-bit builds incompatible])
		fi
		if test "$GCC" = "yes" ; then
		    AC_MSG_ERROR([Windows/CE and GCC builds incompatible])
		fi
		TEA_PATH_CELIB
		# set defaults
		# Currently Tcl requires 300+
		CEVERSION=300;	  # could be 211 300 301 ...
		TARGETCPU=ARM;	  # could be ARM MIPS SH3 X86 ...
		PLATFORM="Pocket PC 2002"
		if test "$doWince" = "yes"; then
		    doWince="300,ARM,ARM,Pocket PC 2002"
		fi
		eval `echo $doWince | awk -F "," '{ \
		    if (length([$]1)) { printf "CEVERSION=\"%s\"\n", [$]1; \
		      if ([$]1 >= 400) { printf "PLATFORM=\"Pocket PC 2003\"\n" } }; \
		    if (length([$]2)) { printf "TARGETCPU=\"%s\"\n", toupper([$]2) }; \
		    if (length([$]3)) { printf "ARCH=\"%s\"\n", toupper([$]3) }; \
		    if (length([$]4)) { printf "PLATFORM=\"%s\"\n", [$]4 }; \
		    }'`
		OSVERSION=WCE$CEVERSION;
		if test "x${ARCH}" = "x" ; then
	            ARCH=$TARGETCPU;  # could be ARM MIPS SH3 X86 X86EM ...
		fi
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
		# In order to work in the tortured autoconf environment,
		# we need to ensure that this path has no spaces
		WCEROOT=`cygpath -w -s "$WCEROOT" | sed -e 's!\\\!/!g'`
		SDKROOT=`cygpath -w -s "$SDKROOT" | sed -e 's!\\\!/!g'`
		CELIB_DIR=`cygpath -w -s "$CELIB_DIR" | sed -e 's!\\\!/!g'`
		if test ! -d "${SDKROOT}/${OSVERSION}/${PLATFORM}/Lib/${TARGETCPU}" \
		    -o ! -d "${WCEROOT}/EVC/${OSVERSION}/bin"; then
		    AC_MSG_ERROR([could not find PocketPC SDK or target compiler to enable WinCE mode [$CEVERSION,$TARGETCPU,$ARCH,$PLATFORM]])
		    doWince="no"
		else
		    CEINCLUDE=`cygpath -w -s "${SDKROOT}/${OSVERSION}/${PLATFORM}/include" | sed -e 's!\\\!/!g'`
		    if test -d "${CEINCLUDE}/${TARGETCPU}" ; then
			CEINCLUDE="${CEINCLUDE}/${TARGETCPU}"
		    fi
		    CELIBPATH=`cygpath -w -s "${SDKROOT}/${OSVERSION}/${PLATFORM}/Lib/${TARGETCPU}" | sed -e 's!\\\!/!g'`
    		fi
	    fi

	    if test "$GCC" != "yes" ; then
	        if test "${SHARED_BUILD}" = "0" ; then
		    runtime=-MT
	        else
		    runtime=-MD
	        fi

                if test "$do64bit" = "yes" ; then
		    # All this magic is necessary for the Win64 SDK RC1 - hobbs
		    export CC="${MSSDK}/Bin/Win64/cl.exe \
	                -I${MSSDK}/Include/prerelease \
                        -I${MSSDK}/Include/Win64/crt \
	                -I${MSSDK}/Include"
		    export RC="${MSSDK}/bin/rc.exe"
		    export lflags="-MACHINE:IA64 -LIBPATH:${MSSDK}/Lib/IA64 \
	                -LIBPATH:${MSSDK}/Lib/Prerelease/IA64"
		    export STLIB_LD="${MSSDK}/bin/win64/lib.exe -nologo ${lflags}"
		    export LINKBIN="${MSSDK}/bin/win64/link.exe ${lflags}"
		    CFLAGS_DEBUG="-nologo -Zi -Od -W3 ${runtime}d"
		    CFLAGS_OPTIMIZE="-nologo -O2 -Gs -W2 ${runtime}"
		elif test "$doWince" != "no" ; then
		    CEBINROOT="${WCEROOT}/EVC/${OSVERSION}/bin"
		    if test "${TARGETCPU}" = "X86"; then
			CC="${CEBINROOT}/cl.exe"
		    else
			CC="${CEBINROOT}/cl${ARCH}.exe"
		    fi
		    CC="${CC} -I\"${CELIB_DIR}/inc\" -I\"${CEINCLUDE}\""
		    RC="${WCEROOT}/Common/EVC/bin/rc.exe"
		    arch=`echo ${ARCH} | awk '{print tolower([$]0)}'`
		    defs="${ARCH} _${ARCH}_ ${arch} PALM_SIZE _MT _DLL _WINDOWS"
		    for i in $defs ; do
			AC_DEFINE_UNQUOTED($i)
		    done
		    AC_DEFINE_UNQUOTED(_WIN32_WCE, $CEVERSION)
		    AC_DEFINE_UNQUOTED(UNDER_CE, $CEVERSION)
		    CFLAGS_DEBUG="-nologo -Zi -Od"
		    CFLAGS_OPTIMIZE="-nologo -Ox"
		    lversion=`echo ${CEVERSION} | sed -e 's/\(.\)\(..\)/\1\.\2/'`
		    lflags="-MACHINE:${ARCH} -LIBPATH:\"${CELIBPATH}\" -subsystem:windowsce,${lversion}"
		    STLIB_LD="${CEBINROOT}/lib.exe -nologo ${lflags}"
		    LINKBIN="${CEBINROOT}/link.exe ${lflags}"
		    AC_SUBST(CELIB_DIR)
		else
		    RC="rc"
		    STLIB_LD="lib -nologo"
    		    LINKBIN="link -link50compat"
		    CFLAGS_DEBUG="-nologo -Z7 -Od -W3 -WX ${runtime}d"
		    CFLAGS_OPTIMIZE="-nologo -O2 -Gs -GD -W2 ${runtime}"
		fi
	    fi

	    if test "$GCC" = "yes"; then
		# mingw gcc mode
		RC="windres"
		CFLAGS_DEBUG="-g"
		CFLAGS_OPTIMIZE="-O2"
		SHLIB_LD="$CC -shared"
		UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.a'
		LDFLAGS_CONSOLE="-wl,--subsystem,console ${lflags}"
		LDFLAGS_WINDOW="-wl,--subsystem,windows ${lflags}"
	    else
		SHLIB_LD="${LINKBIN} -dll -nologo"
		UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.lib'
		CFLAGS="$CFLAGS -YX"
		PATHTYPE=-w
		# For information on what debugtype is most useful, see:
		# http://msdn.microsoft.com/library/en-us/dnvc60/html/gendepdebug.asp
		# This essentially turns it all on.
		LDFLAGS_DEBUG="-debug:full -debugtype:both -warn:2"
		LDFLAGS_OPTIMIZE="-release"
		if test "$doWince" != "no" ; then
		    LDFLAGS_CONSOLE="-link ${lflags}"
		    LDFLAGS_WINDOW=${LDFLAGS_CONSOLE}
		else
		    LDFLAGS_CONSOLE="-link -subsystem:console ${lflags}"
		    LDFLAGS_WINDOW="-link -subsystem:windows ${lflags}"
		fi
	    fi

	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".dll"
	    SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.dll'

	    TCL_LIB_VERSIONS_OK=nodots
	    # Bogus to avoid getting this turned off
	    DL_OBJS="tclLoadNone.obj"
    	    ;;
	AIX-*)
	    if test "${TCL_THREADS}" = "1" -a "$GCC" != "yes" ; then
		# AIX requires the _r compiler when gcc isn't being used
		if test "${CC}" != "cc_r" ; then
		    CC=${CC}_r
		fi
		AC_MSG_RESULT([Using $CC for compiling with threads])
	    fi
	    LIBS="$LIBS -lc"
	    SHLIB_CFLAGS=""
	    SHLIB_SUFFIX=".so"
	    SHLIB_LD_LIBS='${LIBS}'

	    DL_OBJS="tclLoadDl.o"
	    LD_LIBRARY_PATH_VAR="LIBPATH"

	    # AIX v<=4.1 has some different flags than 4.2+
	    if test "$system" = "AIX-4.1" -o "`uname -v`" -lt "4" ; then
		#LIBOBJS="$LIBOBJS tclLoadAix.o"
		AC_LIBOBJ([tclLoadAix])
		DL_LIBS="-lld"
	    fi

	    # Check to enable 64-bit flags for compiler/linker on AIX 4+
	    if test "$do64bit" = "yes" -a "`uname -v`" -gt "3" ; then
		if test "$GCC" = "yes" ; then
		    AC_MSG_WARN("64bit mode not supported with GCC on $system")
		else 
		    do64bit_ok=yes
		    CFLAGS="$CFLAGS -q64"
		    LDFLAGS="$LDFLAGS -q64"
		    RANLIB="${RANLIB} -X64"
		    AR="${AR} -X64"
		    SHLIB_LD_FLAGS="-b64"
		fi
	    fi

	    if test "`uname -m`" = "ia64" ; then
		# AIX-5 uses ELF style dynamic libraries on IA-64, but not PPC
		SHLIB_LD="/usr/ccs/bin/ld -G -z text"
		# AIX-5 has dl* in libc.so
		DL_LIBS=""
		if test "$GCC" = "yes" ; then
		    LD_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR},-R,${LIB_PGTCL_RUNTIME_DIR}''
		else
		    LD_SEARCH_FLAGS='-R${LIB_RUNTIME_DIR},-R,${LIB_PGTCL_RUNTIME_DIR}''
		fi
	    else
		SHLIB_LD="${TCL_SRC_DIR}/unix/ldAix /bin/ld -bhalt:4 -bM:SRE -bE:lib.exp -H512 -T512 -bnoentry ${SHLIB_LD_FLAGS}"
		DL_LIBS="-ldl"
		LD_SEARCH_FLAGS='-L${LIB_RUNTIME_DIR} -L{LIB_PGTCL_RUNTIME_DIR}'
		TCL_NEEDS_EXP_FILE=1
		TCL_EXPORT_FILE_SUFFIX='${PACKAGE_VERSION}\$\{DBGX\}.exp'
	    fi

	    # On AIX <=v4 systems, libbsd.a has to be linked in to support
	    # non-blocking file IO.  This library has to be linked in after
	    # the MATH_LIBS or it breaks the pow() function.  The way to
	    # insure proper sequencing, is to add it to the tail of MATH_LIBS.
	    # This library also supplies gettimeofday.
	    #
	    # AIX does not have a timezone field in struct tm. When the AIX
	    # bsd library is used, the timezone global and the gettimeofday
	    # methods are to be avoided for timezone deduction instead, we
	    # deduce the timezone by comparing the localtime result on a
	    # known GMT value.

	    AC_CHECK_LIB(bsd, gettimeofday, libbsd=yes, libbsd=no)
	    if test $libbsd = yes; then
	    	MATH_LIBS="$MATH_LIBS -lbsd"
	    	AC_DEFINE(USE_DELTA_FOR_TZ)
	    fi
	    ;;
	BeOS*)
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD="${CC} -nostart"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS="-ldl"
	    ;;
	BSD/OS-2.1*|BSD/OS-3*)
	    SHLIB_CFLAGS=""
	    SHLIB_LD="shlicc -r"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS="-ldl"
	    LD_SEARCH_FLAGS=""
	    ;;
	BSD/OS-4.*)
	    SHLIB_CFLAGS="-export-dynamic -fPIC"
	    SHLIB_LD="cc -shared"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS="-ldl"
	    LDFLAGS="$LDFLAGS -export-dynamic"
	    LD_SEARCH_FLAGS=""
	    ;;
	dgux*)
	    SHLIB_CFLAGS="-K PIC"
	    SHLIB_LD="cc -G"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS="-ldl"
	    LD_SEARCH_FLAGS=""
	    ;;
	HP-UX-*.11.*)
	    # Use updated header definitions where possible
	    AC_DEFINE(_XOPEN_SOURCE_EXTENDED)

	    SHLIB_SUFFIX=".sl"
	    AC_CHECK_LIB(dld, shl_load, tcl_ok=yes, tcl_ok=no)
	    if test "$tcl_ok" = yes; then
		SHLIB_CFLAGS="+z"
		SHLIB_LD="ld -b"
		SHLIB_LD_LIBS='${LIBS}'
		DL_OBJS="tclLoadShl.o"
		DL_LIBS="-ldld"
		LDFLAGS="$LDFLAGS -Wl,-E"
		LD_SEARCH_FLAGS='-Wl,+s,+b,${LIB_RUNTIME_DIR}:${LIB_PGTCL_RUNTIME_DIR}:.'
		LD_LIBRARY_PATH_VAR="SHLIB_PATH"
	    fi
	    if test "$GCC" = "yes" ; then
		SHLIB_LD="gcc -shared"
		SHLIB_LD_LIBS='${LIBS}'
		LD_SEARCH_FLAGS=''
	    fi

	    # Users may want PA-RISC 1.1/2.0 portable code - needs HP cc
	    #CFLAGS="$CFLAGS +DAportable"

	    # Check to enable 64-bit flags for compiler/linker
	    if test "$do64bit" = "yes" ; then
		if test "$GCC" = "yes" ; then
		    hpux_arch=`${CC} -dumpmachine`
		    case $hpux_arch in
			hppa64*)
			    # 64-bit gcc in use.  Fix flags for GNU ld.
			    do64bit_ok=yes
			    SHLIB_LD="${CC} -shared"
			    SHLIB_LD_LIBS='${LIBS}'
			    LD_SEARCH_FLAGS=''
			    ;;
			*)
			    AC_MSG_WARN("64bit mode not supported with GCC on $system")
			    ;;
		    esac
		else
		    do64bit_ok=yes
		    CFLAGS="$CFLAGS +DD64"
		    LDFLAGS="$LDFLAGS +DD64"
		fi
	    fi
	    ;;
	HP-UX-*.08.*|HP-UX-*.09.*|HP-UX-*.10.*)
	    SHLIB_SUFFIX=".sl"
	    AC_CHECK_LIB(dld, shl_load, tcl_ok=yes, tcl_ok=no)
	    if test "$tcl_ok" = yes; then
		SHLIB_CFLAGS="+z"
		SHLIB_LD="ld -b"
		SHLIB_LD_LIBS=""
		DL_OBJS="tclLoadShl.o"
		DL_LIBS="-ldld"
		LDFLAGS="$LDFLAGS -Wl,-E"
		LD_SEARCH_FLAGS='-Wl,+s,+b,${LIB_RUNTIME_DIR}:${LIB_PGTCL_RUNTIME_DIR}:.'
	    fi
	    LD_LIBRARY_PATH_VAR="SHLIB_PATH"
	    ;;
	IRIX-4.*)
	    SHLIB_CFLAGS="-G 0"
	    SHLIB_SUFFIX=".a"
	    SHLIB_LD="echo tclLdAout $CC \{$SHLIB_CFLAGS\} | `pwd`/tclsh -r -G 0"
	    SHLIB_LD_LIBS='${LIBS}'
	    DL_OBJS="tclLoadAout.o"
	    DL_LIBS=""
	    LDFLAGS="$LDFLAGS -Wl,-D,08000000"
	    LD_SEARCH_FLAGS='-L${LIB_RUNTIME_DIR} -L{$LIB_PGTCL_RUNTIME_DIR}'
	    SHARED_LIB_SUFFIX='${PACKAGE_VERSION}\$\{DBGX\}.a'
	    ;;
	IRIX-5.*)
	    SHLIB_CFLAGS=""
	    SHLIB_LD="ld -shared -rdata_shared"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS=""
	    LD_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'
	    ;;
	IRIX-6.*|IRIX64-6.5*)
	    SHLIB_CFLAGS=""
	    SHLIB_LD="ld -n32 -shared -rdata_shared"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS=""
	    LD_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'
	    if test "$GCC" = "yes" ; then
		CFLAGS="$CFLAGS -mabi=n32"
		LDFLAGS="$LDFLAGS -mabi=n32"
	    else
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
	    fi
	    ;;
	IRIX64-6.*)
	    SHLIB_CFLAGS=""
	    SHLIB_LD="ld -n32 -shared -rdata_shared"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS=""
	    LD_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'

	    # Check to enable 64-bit flags for compiler/linker

	    if test "$do64bit" = "yes" ; then
	        if test "$GCC" = "yes" ; then
	            AC_MSG_WARN([64bit mode not supported by gcc])
	        else
	            do64bit_ok=yes
	            SHLIB_LD="ld -64 -shared -rdata_shared"
	            CFLAGS="$CFLAGS -64"
	            LDFLAGS="$LDFLAGS -64"
	        fi
	    fi
	    ;;
	Linux*)
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"

	    CFLAGS_OPTIMIZE=-O2
	    # egcs-2.91.66 on Redhat Linux 6.0 generates lots of warnings 
	    # when you inline the string and math operations.  Turn this off to
	    # get rid of the warnings.

	    #CFLAGS_OPTIMIZE="${CFLAGS_OPTIMIZE} -D__NO_STRING_INLINES -D__NO_MATH_INLINES"

	    if test "$have_dl" = yes; then
		SHLIB_LD="${CC} -shared"
		DL_OBJS="tclLoadDl.o"
		DL_LIBS="-ldl"
		LDFLAGS="$LDFLAGS -Wl,--export-dynamic"
		LD_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'
	    else
		AC_CHECK_HEADER(dld.h, [
		    SHLIB_LD="ld -shared"
		    DL_OBJS="tclLoadDld.o"
		    DL_LIBS="-ldld"
		    LD_SEARCH_FLAGS=""])
	    fi
	    if test "`uname -m`" = "alpha" ; then
		CFLAGS="$CFLAGS -mieee"
	    fi

	    # The combo of gcc + glibc has a bug related
	    # to inlining of functions like strtod(). The
	    # -fno-builtin flag should address this problem
	    # but it does not work. The -fno-inline flag
	    # is kind of overkill but it works.
	    # Disable inlining only when one of the
	    # files in compat/*.c is being linked in.
	    if test x"${USE_COMPAT}" != x ; then
	        CFLAGS="$CFLAGS -fno-inline"
	    fi

	    ;;
	GNU*)
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"

	    if test "$have_dl" = yes; then
		SHLIB_LD="${CC} -shared"
		DL_OBJS=""
		DL_LIBS="-ldl"
		LDFLAGS="$LDFLAGS -Wl,--export-dynamic"
		LD_SEARCH_FLAGS=""
	    else
		AC_CHECK_HEADER(dld.h, [
		    SHLIB_LD="ld -shared"
		    DL_OBJS=""
		    DL_LIBS="-ldld"
		    LD_SEARCH_FLAGS=""])
	    fi
	    if test "`uname -m`" = "alpha" ; then
		CFLAGS="$CFLAGS -mieee"
	    fi
	    ;;
	MP-RAS-02*)
	    SHLIB_CFLAGS="-K PIC"
	    SHLIB_LD="cc -G"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS="-ldl"
	    LD_SEARCH_FLAGS=""
	    ;;
	MP-RAS-*)
	    SHLIB_CFLAGS="-K PIC"
	    SHLIB_LD="cc -G"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS="-ldl"
	    LDFLAGS="$LDFLAGS -Wl,-Bexport"
	    LD_SEARCH_FLAGS=""
	    ;;
	NetBSD-*|FreeBSD-[[1-2]].*)
	    # Not available on all versions:  check for include file.
	    AC_CHECK_HEADER(dlfcn.h, [
		# NetBSD/SPARC needs -fPIC, -fpic will not do.
		SHLIB_CFLAGS="-fPIC"
		SHLIB_LD="ld -Bshareable -x"
		SHLIB_LD_LIBS=""
		SHLIB_SUFFIX=".so"
		DL_OBJS="tclLoadDl.o"
		DL_LIBS=""
		LD_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'
		AC_MSG_CHECKING([for ELF])
		AC_EGREP_CPP(yes, [
#ifdef __ELF__
	yes
#endif
		],
		    AC_MSG_RESULT([yes])
		    SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.so',
		    AC_MSG_RESULT([no])
		    SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.so.1.0'
		)
	    ], [
		SHLIB_CFLAGS=""
		SHLIB_LD="echo tclLdAout $CC \{$SHLIB_CFLAGS\} | `pwd`/tclsh -r"
		SHLIB_LD_LIBS='${LIBS}'
		SHLIB_SUFFIX=".a"
		DL_OBJS="tclLoadAout.o"
		DL_LIBS=""
		LD_SEARCH_FLAGS='-L${LIB_RUNTIME_DIR} -L{LIB_PGTCL_RUNTIME_DIR}'
		SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.a'
	    ])

	    # FreeBSD doesn't handle version numbers with dots.

	    UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.a'
	    TCL_LIB_VERSIONS_OK=nodots
	    ;;
	OpenBSD-*)
	    SHLIB_LD="${CC} -shared"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS=""
	    LD_SEARCH_FLAGS=""
	    AC_MSG_CHECKING(for ELF)
	    AC_EGREP_CPP(yes, [
#ifdef __ELF__
	yes
#endif
	    ],
		[AC_MSG_RESULT(yes)
		SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.so.1.0'],
		[AC_MSG_RESULT(no)
		SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.so.1.0']
	    )

	    # OpenBSD doesn't do version numbers with dots.
	    UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.a'
	    TCL_LIB_VERSIONS_OK=nodots
	    ;;
	FreeBSD-*)
	    # FreeBSD 3.* and greater have ELF.
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD="ld -Bshareable -x"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS=""
	    LDFLAGS="$LDFLAGS -export-dynamic"
	    LD_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR},-rpath,${LIB_PGTCL_RUNTIME_DIR}'
	    if test "${TCL_THREADS}" = "1" ; then
		# The -pthread needs to go in the CFLAGS, not LIBS
		LIBS=`echo $LIBS | sed s/-pthread//`
		CFLAGS="$CFLAGS -pthread"
	    	LDFLAGS="$LDFLAGS -pthread"
	    fi
	    case $system in
	    FreeBSD-3.*)
	    	# FreeBSD-3 doesn't handle version numbers with dots.
	    	UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.a'
	    	SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.so'
	    	TCL_LIB_VERSIONS_OK=nodots
		;;
	    esac
	    ;;
	Rhapsody-*|Darwin-*)
	    SHLIB_CFLAGS="-fno-common"
	    SHLIB_LD="cc -dynamiclib \${LDFLAGS}"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".dylib"
	    DL_OBJS="tclLoadDyld.o"
	    DL_LIBS=""
	    LDFLAGS="$LDFLAGS -prebind"
	    LD_SEARCH_FLAGS=""
	    LD_LIBRARY_PATH_VAR="DYLD_LIBRARY_PATH"
	    CFLAGS_OPTIMIZE="-Os"
	    ;;
	NEXTSTEP-*)
	    SHLIB_CFLAGS=""
	    SHLIB_LD="cc -nostdlib -r"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadNext.o"
	    DL_LIBS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	OS/390-*)
	    CFLAGS_OPTIMIZE=""      # Optimizer is buggy
	    AC_DEFINE(_OE_SOCKETS)  # needed in sys/socket.h
	    ;;      
	OSF1-1.0|OSF1-1.1|OSF1-1.2)
	    # OSF/1 1.[012] from OSF, and derivatives, including Paragon OSF/1
	    SHLIB_CFLAGS=""
	    # Hack: make package name same as library name
	    SHLIB_LD='ld -R -export $@:'
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadOSF.o"
	    DL_LIBS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	OSF1-1.*)
	    # OSF/1 1.3 from OSF using ELF, and derivatives, including AD2
	    SHLIB_CFLAGS="-fPIC"
	    if test "$SHARED_BUILD" = "1" ; then
	        SHLIB_LD="ld -shared"
	    else
	        SHLIB_LD="ld -non_shared"
	    fi
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	OSF1-V*)
	    # Digital OSF/1
	    SHLIB_CFLAGS=""
	    if test "$SHARED_BUILD" = "1" ; then
	        SHLIB_LD='ld -shared -expect_unresolved "*"'
	    else
	        SHLIB_LD='ld -non_shared -expect_unresolved "*"'
	    fi
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS=""
	    LD_SEARCH_FLAGS='-rpath ${LIB_RUNTIME_DIR} -rpath {$LIB_PGTCL_RUNTIME_DIR}'
	    if test "$GCC" = "yes" ; then
		CFLAGS="$CFLAGS -mieee"
            else
		CFLAGS="$CFLAGS -DHAVE_TZSET -std1 -ieee"
	    fi
	    # see pthread_intro(3) for pthread support on osf1, k.furukawa
	    if test "${TCL_THREADS}" = "1" ; then
		CFLAGS="$CFLAGS -DHAVE_PTHREAD_ATTR_SETSTACKSIZE"
		CFLAGS="$CFLAGS -DTCL_THREAD_STACK_MIN=PTHREAD_STACK_MIN*64"
		LIBS=`echo $LIBS | sed s/-lpthreads//`
		if test "$GCC" = "yes" ; then
		    LIBS="$LIBS -lpthread -lmach -lexc"
		else
		    CFLAGS="$CFLAGS -pthread"
		    LDFLAGS="$LDFLAGS -pthread"
		fi
	    fi

	    ;;
	QNX-6*)
	    # QNX RTP
	    # This may work for all QNX, but it was only reported for v6.
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD="ld -Bshareable -x"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    # dlopen is in -lc on QNX
	    DL_LIBS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	RISCos-*)
	    SHLIB_CFLAGS="-G 0"
	    SHLIB_LD="echo tclLdAout $CC \{$SHLIB_CFLAGS\} | `pwd`/tclsh -r -G 0"
	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".a"
	    DL_OBJS="tclLoadAout.o"
	    DL_LIBS=""
	    LDFLAGS="$LDFLAGS -Wl,-D,08000000"
	    LD_SEARCH_FLAGS='-L${LIB_RUNTIME_DIR} -L${LIB_PGTCL_RUNTIME_DIR}'
	    ;;
	SCO_SV-3.2*)
	    # Note, dlopen is available only on SCO 3.2.5 and greater. However,
	    # this test works, since "uname -s" was non-standard in 3.2.4 and
	    # below.
	    if test "$GCC" = "yes" ; then
	    	SHLIB_CFLAGS="-fPIC -melf"
	    	LDFLAGS="$LDFLAGS -melf -Wl,-Bexport"
	    else
	    	SHLIB_CFLAGS="-Kpic -belf"
	    	LDFLAGS="$LDFLAGS -belf -Wl,-Bexport"
	    fi
	    SHLIB_LD="ld -G"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	SINIX*5.4*)
	    SHLIB_CFLAGS="-K PIC"
	    SHLIB_LD="cc -G"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS="-ldl"
	    LD_SEARCH_FLAGS=""
	    ;;
	SunOS-4*)
	    SHLIB_CFLAGS="-PIC"
	    SHLIB_LD="ld"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS="-ldl"
	    LD_SEARCH_FLAGS='-L${LIB_RUNTIME_DIR} -L${LIB_PGTCL_RUNTIME_DIR}'

	    # SunOS can't handle version numbers with dots in them in library
	    # specs, like -ltcl7.5, so use -ltcl75 instead.  Also, it
	    # requires an extra version number at the end of .so file names.
	    # So, the library has to have a name like libtcl75.so.1.0

	    SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.so.1.0'
	    UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}\$\{DBGX\}.a'
	    TCL_LIB_VERSIONS_OK=nodots
	    ;;
	SunOS-5.[[0-6]]*)

	    # Note: If _REENTRANT isn't defined, then Solaris
	    # won't define thread-safe library routines.

	    AC_DEFINE(_REENTRANT)
	    AC_DEFINE(_POSIX_PTHREAD_SEMANTICS)

	    SHLIB_CFLAGS="-KPIC"

	    # Note: need the LIBS below, otherwise Tk won't find Tcl's
	    # symbols when dynamically loaded into tclsh.

	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS="-ldl"
	    if test "$GCC" = "yes" ; then
		SHLIB_LD="$CC -shared"
		LD_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR},-R,${LIB_PGTCL_RUNTIME_DIR}'
	    else
		SHLIB_LD="/usr/ccs/bin/ld -G -z text"
		LD_SEARCH_FLAGS='-R ${LIB_RUNTIME_DIR} -R {$LIB_PGTCL_RUNTIME_DIR}'
	    fi
	    ;;
	SunOS-5*)

	    # Note: If _REENTRANT isn't defined, then Solaris
	    # won't define thread-safe library routines.

	    AC_DEFINE(_REENTRANT)
	    AC_DEFINE(_POSIX_PTHREAD_SEMANTICS)

	    SHLIB_CFLAGS="-KPIC"
    
	    # Check to enable 64-bit flags for compiler/linker
	    if test "$do64bit" = "yes" ; then
		arch=`isainfo`
		if test "$arch" = "sparcv9 sparc" ; then
			if test "$GCC" = "yes" ; then
			    AC_MSG_WARN("64bit mode not supported with GCC on $system")
			else
			    do64bit_ok=yes
			    if test "$do64bitVIS" = "yes" ; then
				CFLAGS="$CFLAGS -xarch=v9a"
			    	LDFLAGS="$LDFLAGS -xarch=v9a"
			    else
				CFLAGS="$CFLAGS -xarch=v9"
			    	LDFLAGS="$LDFLAGS -xarch=v9"
			    fi
			fi
		else
		    AC_MSG_WARN("64bit mode only supported sparcv9 system")
		fi
	    fi
	    
	    # Note: need the LIBS below, otherwise Tk won't find Tcl's
	    # symbols when dynamically loaded into tclsh.

	    SHLIB_LD_LIBS='${LIBS}'
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS="-ldl"
	    if test "$GCC" = "yes" ; then
		SHLIB_LD="$CC -shared"
		LD_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR},-R,${LIB_PGTCL_RUNTIME_DIR}'
	    else
		SHLIB_LD="/usr/ccs/bin/ld -G -z text"
		LD_SEARCH_FLAGS='-R ${LIB_RUNTIME_DIR} -R ${LIB_PGTCL_RUNTIME_DIR}'
	    fi
	    ;;
	ULTRIX-4.*)
	    SHLIB_CFLAGS="-G 0"
	    SHLIB_SUFFIX=".a"
	    SHLIB_LD="echo tclLdAout $CC \{$SHLIB_CFLAGS\} | `pwd`/tclsh -r -G 0"
	    SHLIB_LD_LIBS='${LIBS}'
	    DL_OBJS="tclLoadAout.o"
	    DL_LIBS=""
	    LDFLAGS="$LDFLAGS -Wl,-D,08000000"
	    LD_SEARCH_FLAGS='-L${LIB_RUNTIME_DIR} -L${LIB_PGTCL_RUNTIME_DIR}'
	    if test "$GCC" != "yes" ; then
		CFLAGS="$CFLAGS -DHAVE_TZSET -std1"
	    fi
	    ;;
	UNIX_SV* | UnixWare-5*)
	    SHLIB_CFLAGS="-KPIC"
	    SHLIB_LD="cc -G"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    DL_OBJS="tclLoadDl.o"
	    DL_LIBS="-ldl"
	    # Some UNIX_SV* systems (unixware 1.1.2 for example) have linkers
	    # that don't grok the -Bexport option.  Test that it does.
	    hold_ldflags=$LDFLAGS
	    AC_MSG_CHECKING(for ld accepts -Bexport flag)
	    LDFLAGS="$LDFLAGS -Wl,-Bexport"
	    AC_TRY_LINK(, [int i;], [found=yes],
			[LDFLAGS=$hold_ldflags found=no])
	    AC_MSG_RESULT([$found])
	    LD_SEARCH_FLAGS=""
	    ;;
    esac

    if test "$do64bit" = "yes" -a "$do64bit_ok" = "no" ; then
    AC_MSG_WARN("64bit support being disabled -- don\'t know magic for this platform")
    fi

    # Step 4: If pseudo-static linking is in use (see K. B. Kenny, "Dynamic
    # Loading for Tcl -- What Became of It?".  Proc. 2nd Tcl/Tk Workshop,
    # New Orleans, LA, Computerized Processes Unlimited, 1994), then we need
    # to determine which of several header files defines the a.out file
    # format (a.out.h, sys/exec.h, or sys/exec_aout.h).  At present, we
    # support only a file format that is more or less version-7-compatible. 
    # In particular,
    #	- a.out files must begin with `struct exec'.
    #	- the N_TXTOFF on the `struct exec' must compute the seek address
    #	  of the text segment
    #	- The `struct exec' must contain a_magic, a_text, a_data, a_bss
    #	  and a_entry fields.
    # The following compilation should succeed if and only if either sys/exec.h
    # or a.out.h is usable for the purpose.
    #
    # Note that the modified COFF format used on MIPS Ultrix 4.x is usable; the
    # `struct exec' includes a second header that contains information that
    # duplicates the v7 fields that are needed.

    if test "x$DL_OBJS" = "xtclLoadAout.o" ; then
	AC_MSG_CHECKING([sys/exec.h])
	AC_TRY_COMPILE([#include <sys/exec.h>],[
	    struct exec foo;
	    unsigned long seek;
	    int flag;
#if defined(__mips) || defined(mips)
	    seek = N_TXTOFF (foo.ex_f, foo.ex_o);
#else
	    seek = N_TXTOFF (foo);
#endif
	    flag = (foo.a_magic == OMAGIC);
	    return foo.a_text + foo.a_data + foo.a_bss + foo.a_entry;
    ], tcl_ok=usable, tcl_ok=unusable)
	AC_MSG_RESULT([$tcl_ok])
	if test $tcl_ok = usable; then
	    AC_DEFINE(USE_SYS_EXEC_H)
	else
	    AC_MSG_CHECKING([a.out.h])
	    AC_TRY_COMPILE([#include <a.out.h>],[
		struct exec foo;
		unsigned long seek;
		int flag;
#if defined(__mips) || defined(mips)
		seek = N_TXTOFF (foo.ex_f, foo.ex_o);
#else
		seek = N_TXTOFF (foo);
#endif
		flag = (foo.a_magic == OMAGIC);
		return foo.a_text + foo.a_data + foo.a_bss + foo.a_entry;
	    ], tcl_ok=usable, tcl_ok=unusable)
	    AC_MSG_RESULT([$tcl_ok])
	    if test $tcl_ok = usable; then
		AC_DEFINE(USE_A_OUT_H)
	    else
		AC_MSG_CHECKING([sys/exec_aout.h])
		AC_TRY_COMPILE([#include <sys/exec_aout.h>],[
		    struct exec foo;
		    unsigned long seek;
		    int flag;
#if defined(__mips) || defined(mips)
		    seek = N_TXTOFF (foo.ex_f, foo.ex_o);
#else
		    seek = N_TXTOFF (foo);
#endif
		    flag = (foo.a_midmag == OMAGIC);
		    return foo.a_text + foo.a_data + foo.a_bss + foo.a_entry;
		], tcl_ok=usable, tcl_ok=unusable)
		AC_MSG_RESULT([$tcl_ok])
		if test $tcl_ok = usable; then
		    AC_DEFINE(USE_SYS_EXEC_AOUT_H)
		else
		    DL_OBJS=""
		fi
	    fi
	fi
    fi

    # Step 5: disable dynamic loading if requested via a command-line switch.

    AC_ARG_ENABLE(load, [  --disable-load          disallow dynamic loading and "load" command],
	[tcl_ok=$enableval], [tcl_ok=yes])
    if test "$tcl_ok" = "no"; then
	DL_OBJS=""
    fi

    if test "x$DL_OBJS" != "x" ; then
	BUILD_DLTEST="\$(DLTEST_TARGETS)"
    else
	echo "Can't figure out how to do dynamic loading or shared libraries"
	echo "on this system."
	SHLIB_CFLAGS=""
	SHLIB_LD=""
	SHLIB_SUFFIX=""
	DL_OBJS="tclLoadNone.o"
	DL_LIBS=""
	LDFLAGS="$LDFLAGS_ORIG"
	LD_SEARCH_FLAGS=""
	BUILD_DLTEST=""
    fi

    # If we're running gcc, then change the C flags for compiling shared
    # libraries to the right flags for gcc, instead of those for the
    # standard manufacturer compiler.

    if test "$DL_OBJS" != "tclLoadNone.o" ; then
	if test "$GCC" = "yes" ; then
	    case $system in
		AIX-*)
		    ;;
		BSD/OS*)
		    ;;
		IRIX*)
		    ;;
		NetBSD-*|FreeBSD-*)
		    ;;
		Rhapsody-*|Darwin-*)
		    ;;
		RISCos-*)
		    ;;
		SCO_SV-3.2*)
		    ;;
		ULTRIX-4.*)
		    ;;
		windows)
		    ;;
		*)
		    SHLIB_CFLAGS="-fPIC"
		    ;;
	    esac
	fi
    fi

    if test "$SHARED_LIB_SUFFIX" = "" ; then
	SHARED_LIB_SUFFIX='${PACKAGE_VERSION}\$\{DBGX\}${SHLIB_SUFFIX}'
    fi
    if test "$UNSHARED_LIB_SUFFIX" = "" ; then
	UNSHARED_LIB_SUFFIX='${PACKAGE_VERSION}\$\{DBGX\}.a'
    fi

    AC_SUBST(DL_LIBS)
    AC_SUBST(CFLAGS_DEBUG)
    AC_SUBST(CFLAGS_OPTIMIZE)
    AC_SUBST(CFLAGS_WARNING)

    AC_SUBST(STLIB_LD)
    AC_SUBST(SHLIB_LD)
    AC_SUBST(SHLIB_CFLAGS)
    AC_SUBST(SHLIB_LD_FLAGS)
    AC_SUBST(SHLIB_LD_LIBS)
    AC_SUBST(LDFLAGS_DEBUG)
    AC_SUBST(LDFLAGS_OPTIMIZE)
    AC_SUBST(LD_LIBRARY_PATH_VAR)

    # These must be called after we do the basic CFLAGS checks and
    # verify any possible 64-bit or similar switches are necessary
    TEA_TCL_EARLY_FLAGS
    TEA_TCL_64BIT_FLAGS
])

