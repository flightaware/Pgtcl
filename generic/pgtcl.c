/*-------------------------------------------------------------------------
 *
 * pgtcl.c
 *
 *	libpgtcl is a tcl package for front-ends to interface with PostgreSQL.
 *	It's a Tcl wrapper for libpq.
 *
 * Portions Copyright (c) 1996-2002, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Id$
 *
 *-------------------------------------------------------------------------
 */

#include "internal/postgres_fe.h"
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
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_connect",
						 Pg_connect,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_disconnect",
						 Pg_disconnect,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_exec",
						 Pg_exec,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_select",
						 Pg_select,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_result",
						 Pg_result,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_execute",
						 Pg_execute,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_open",
						 Pg_lo_open,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_close",
						 Pg_lo_close,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_read",
						 Pg_lo_read,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_write",
						 Pg_lo_write,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_lseek",
						 Pg_lo_lseek,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_creat",
						 Pg_lo_creat,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_tell",
						 Pg_lo_tell,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_unlink",
						 Pg_lo_unlink,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_import",
						 Pg_lo_import,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_lo_export",
						 Pg_lo_export,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_listen",
						 Pg_listen,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_sendquery",
						 Pg_sendquery,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_getresult",
						 Pg_getresult,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_isbusy",
						 Pg_isbusy,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_blocking",
						 Pg_blocking,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						 "pg_cancelrequest",
						 Pg_cancelrequest,
						 (ClientData)NULL,
						 (Tcl_CmdDeleteProc *)NULL);

	Tcl_CreateObjCommand(interp,
						  "pg_on_connection_loss",
						  Pg_on_connection_loss,
						  (ClientData) NULL, 
						  (Tcl_CmdDeleteProc *) NULL);

	Tcl_CreateObjCommand(interp,
						  "pg_escape_string",
						  Pg_escape_string,
						  (ClientData) NULL, 
						  (Tcl_CmdDeleteProc *) NULL);

	Tcl_PkgProvide(interp, "Pgtcl", "1.4");

	return TCL_OK;
}

int
Pgtcl_SafeInit(Tcl_Interp *interp)
{
	return Pgtcl_Init(interp);
}
