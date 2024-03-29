/*-------------------------------------------------------------------------
 *
 * libpgtcl.h
 *
 *	libpgtcl is a tcl package for front-ends to interface with PostgreSQL.
 *	It's a Tcl wrapper for libpq.
 *
 * Portions Copyright (c) 1996-2003, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id$
 *
 *-------------------------------------------------------------------------
 */

#ifndef LIBPGTCL_H
#define LIBPGTCL_H

#include <tcl.h>

#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT

EXTERN int	Pgtcl_Init(Tcl_Interp *interp);
extern int	Pgtcl_SafeInit(Tcl_Interp *interp);

#endif   /* LIBPGTCL_H */
