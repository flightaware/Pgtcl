/*------------------------------------------------------------------------
 *
 * pgtcl.c
 *
 *	libpgtcl is a tcl package for front-ends to interface with PostgreSQL.
 *	It's a Tcl wrapper for libpq.
 *
 * Portions Copyright (c) 1996-2003, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Id$
 *
 *-------------------------------------------------------------------------
 */

#include <libpq-fe.h>
#include "libpgtcl.h"
#include "pgtclCmds.h"
#include "pgtclId.h"

/* BEGIN STUBS MUMBO JUMBO http://mini.net/tcl/1687 */
/* We need at least the Tcl_Obj interface that was started in 8.0 */
#if TCL_MAJOR_VERSION < 8
#error "we need Tcl 8.0 or greater to build this"

/* Check for Stubs compatibility when asked for it. */
#elif defined(USE_TCL_STUBS) && TCL_MAJOR_VERSION == 8 && \
		(TCL_MINOR_VERSION == 0 || \
		(TCL_MINOR_VERSION == 1 && TCL_RELEASE_LEVEL != TCL_FINAL_RELEASE))
#error "Stubs interface doesn't work in 8.0 and alpha/beta 8.1; only 8.1.0+"
#endif

#ifdef _MSC_VER
/* Only do this when MSVC++ is compiling us. */
#ifdef USE_TCL_STUBS
 /* Mark this .obj as needing tcl's Stubs library. */
#pragma comment(lib, "tclstub" \
			STRINGIFY(JOIN(TCL_MAJOR_VERSION,TCL_MINOR_VERSION)) ".lib")
#if !defined(_MT) || !defined(_DLL) || defined(_DEBUG)

 /*
  * This fixes a bug with how the Stubs library was compiled. The
  * requirement for msvcrt.lib from tclstubXX.lib should be removed.
  */
#pragma comment(linker, "-nodefaultlib:msvcrt.lib")
#endif
#else
 /* Mark this .obj needing the import library */
#pragma comment(lib, "tcl" \
		STRINGIFY(JOIN(TCL_MAJOR_VERSION,TCL_MINOR_VERSION)) ".lib")
#endif
#endif
#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT
/* END STUBS MUMBO JUMBO */

/*
 * Pgtcl_Init
 *	  initialization package for the PGTCL Tcl package
 *
 */

int
Pgtcl_Init(Tcl_Interp *interp)
{
	double		tclversion;
	Tcl_Obj    *tclVersionObj;

#ifdef USE_TCL_STUBS
	if (Tcl_InitStubs(interp, "8.1", 0) == NULL)
		return TCL_ERROR;
#endif

        #ifdef WIN32
        /*
        * On Windows, need to explicitly load the libpq library to
        * force the call to WSAStartup.
        */
        if (LoadLibrary("libpq.dll") == NULL) {
        char buf[32];
        sprintf(buf, "%d", GetLastError());
        Tcl_AppendResult(interp,
        "Cannot load "libpq.dll" (or dependant), error was ",
        buf,
        NULL);
        return TCL_ERROR;
        }
        #endif
        

	/*
	 * Tcl versions >= 8.1 use UTF-8 for their internal string
	 * representation. Therefore PGCLIENTENCODING must be set to UNICODE
	 * for these versions.
	 */

	if ((tclVersionObj = Tcl_GetVar2Ex(interp, "tcl_version", NULL, TCL_GLOBAL_ONLY)) == NULL)
		return TCL_ERROR;

	if (Tcl_GetDoubleFromObj(interp, tclVersionObj, &tclversion) == TCL_ERROR)
		return TCL_ERROR;

	if (tclversion >= 8.1)
		Tcl_PutEnv("PGCLIENTENCODING=UNICODE");

	/* register all pgtcl commands */
	Tcl_CreateObjCommand(interp,
						 "pg_conndefaults",
						 Pg_conndefaults,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_connect",
						 Pg_connect,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_disconnect",
						 Pg_disconnect,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_exec",
						 Pg_exec,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_exec_prepared",
						 Pg_exec_prepared,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_select",
						 Pg_select,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_result",
						 Pg_result,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_execute",
						 Pg_execute,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_open",
						 Pg_lo_open,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_close",
						 Pg_lo_close,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_read",
						 Pg_lo_read,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_write",
						 Pg_lo_write,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_lseek",
						 Pg_lo_lseek,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_creat",
						 Pg_lo_creat,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_tell",
						 Pg_lo_tell,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_unlink",
						 Pg_lo_unlink,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_import",
						 Pg_lo_import,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_export",
						 Pg_lo_export,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_listen",
						 Pg_listen,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_sendquery",
						 Pg_sendquery,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_getresult",
						 Pg_getresult,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_isbusy",
						 Pg_isbusy,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_blocking",
						 Pg_blocking,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_cancelrequest",
						 Pg_cancelrequest,
						 NULL,
						 NULL);

	Tcl_CreateObjCommand(interp,
						  "pg_on_connection_loss",
						  Pg_on_connection_loss,
						  NULL, 
						  NULL);

	Tcl_CreateObjCommand(interp,
						  "pg_quote",
						  Pg_quote,
						  NULL, 
						  NULL);

	Tcl_PkgProvide(interp, "Pgtcl", "1.4");

	return TCL_OK;
}

int
Pgtcl_SafeInit(Tcl_Interp *interp)
{
	return Pgtcl_Init(interp);
}
