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

#ifdef WIN32
#include <c.h>
#endif


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



typedef struct {
    char *name;                 /* Name of command. */
    char *name2;                /* Name of command, in ::pg namespace. */
    Tcl_ObjCmdProc *objProc;    /* Command's object-based procedure. */
    int protocol;    /* version 2 or version 3 (>=7.4) of PG protocol */
} PgCmd;

static PgCmd commands[] = {
    {"pg_conndefaults", "::pg::conndefaults", Pg_conndefaults, 2},
    {"pg_connect", "::pg::connect", Pg_connect,2},
    {"pg_disconnect", "::pg::disconnect", Pg_disconnect,2},
    {"pg_exec", "::pg::sqlexec", Pg_exec,2},
    {"pg_exec_prepared", "::pg::sqlexec_prepared", Pg_exec_prepared,3},
    {"pg_select", "::pg::select", Pg_select,2},
    {"pg_result", "::pg::result", Pg_result,2},
    {"pg_execute", "::pg::execute", Pg_execute,2},
    {"pg_lo_open", "::pg::lo_open", Pg_lo_open,2},
    {"pg_lo_close", "::pg::lo_close", Pg_lo_close,2},
    {"pg_lo_read", "::pg::lo_read", Pg_lo_read,2},
    {"pg_lo_write", "::pg::lo_write", Pg_lo_write,2},
    {"pg_lo_lseek", "::pg::lo_lseek", Pg_lo_lseek,2},
    {"pg_lo_creat", "::pg::lo_creat", Pg_lo_creat,2},
    {"pg_lo_tell", "::pg::lo_tell", Pg_lo_tell,2},
    {"pg_lo_unlink", "::pg::lo_unlink", Pg_lo_unlink,2},
    {"pg_lo_import", "::pg::lo_import", Pg_lo_import,2},
    {"pg_lo_export", "::pg::lo_export", Pg_lo_export,2},
    {"pg_listen", "::pg::listen", Pg_listen,2},
    {"pg_sendquery", "::pg::sendquery", Pg_sendquery,2},
    {"pg_sendquery_prepared", "::pg::sendquery_prepared", Pg_sendquery_prepared,3},
    {"pg_getresult", "::pg::getresult", Pg_getresult,2},
    {"pg_isbusy", "::pg::isbusy", Pg_isbusy,2},
    {"pg_blocking", "::pg::blocking", Pg_blocking,2},
    {"pg_cancelrequest", "::pg::cancelrequest", Pg_cancelrequest,2},
    {"pg_on_connection_loss", "::pg::on_connection_loss", Pg_on_connection_loss,2},
    {"pg_quote", "::pg::quote", Pg_quote,2},
    {"pg_conninfo", "::pg::conninfo", Pg_conninfo,2},
    {"pg_results", "::pg::results", Pg_results,2},
    {NULL, NULL, NULL, NULL}
};



/*
 * Pgtcl_Init
 *	  initialization package for the PGTCL Tcl package
 *
 */

EXTERN int
Pgtcl_Init(Tcl_Interp *interp)
{
	double		tclversion;
	Tcl_Obj    *tclVersionObj;
        PgCmd *cmdPtr;

        #ifdef WIN32
        WSADATA wsaData;
        #endif

#ifdef USE_TCL_STUBS
	if (Tcl_InitStubs(interp, "8.1", 0) == NULL)
		return TCL_ERROR;
#endif

        #ifdef WIN32X
        /*
        * On Windows, need to explicitly load the libpq library to
        * force the call to WSAStartup.
        */
        if (LoadLibrary("libpq.dll") == NULL) {
        char buf[32];
        sprintf(buf, "%d", GetLastError());
        Tcl_AppendResult(interp, "Cannot load "libpq.dll" (or dependant), error was ", buf, NULL);
        return TCL_ERROR;
        }
        #endif
        
    #ifdef WIN32

            if (WSAStartup(MAKEWORD(1, 1), &wsaData))
            {
                   /*
                    * No really good way to do error handling here, since we
                    * don't know how we were loaded
                    */
                    return FALSE;
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


        for (cmdPtr = commands; cmdPtr->name != NULL; cmdPtr++) {

             Tcl_CreateObjCommand(interp, cmdPtr->name, cmdPtr->objProc, (ClientData) "::",NULL);
             Tcl_CreateObjCommand(interp, cmdPtr->name2, cmdPtr->objProc, (ClientData) "::pg::",NULL);
        }


        if (Tcl_Eval(interp, "namespace eval ::pg namespace export *") == TCL_ERROR)
            return TCL_ERROR;

	Tcl_PkgProvide(interp, "Pgtcl", "1.5");

	return TCL_OK;
}

int
Pgtcl_SafeInit(Tcl_Interp *interp)
{
	return Pgtcl_Init(interp);
}
