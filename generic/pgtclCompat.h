/*-------------------------------------------------------------------------
 *
 * pgtclCompat.h
 *
 *	Small compatibility shim so that pgtcl can be built against both
 *	Tcl 8.x (8.4 through 8.6) and Tcl 9.0.
 *
 *	Tcl 9.0 made two changes that affect every file in this package:
 *
 *	  1. The ancient "CONST"/"CONST84"/"CONST86" macros (kept around
 *	     since the Tcl 8.0 days for extensions that still needed to
 *	     build against Tcl 7.x/8.0) were removed.  Plain "const" has
 *	     been the correct, portable spelling since Tcl 8.4, so pgtcl's
 *	     sources have been updated to just use "const" directly -- no
 *	     fallback is required here.
 *
 *	  2. String/list lengths and element counts that used to be passed
 *	     around as "int" (e.g. the out-parameters of Tcl_GetStringFromObj,
 *	     Tcl_GetByteArrayFromObj, Tcl_ListObjGetElements, Tcl_ListObjLength)
 *	     now use the new "Tcl_Size" type, which is wide enough to hold
 *	     lengths greater than INT_MAX on 64-bit builds.  Tcl 8.x does not
 *	     define Tcl_Size at all, so provide a fallback typedef for it.
 *
 *	See https://www.tcl-lang.org/doc/howto/size_t.html for the upstream
 *	description of this migration.
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGTCL_COMPAT_H
#define PGTCL_COMPAT_H

#include <tcl.h>

#ifndef TCL_SIZE_MAX
#ifndef Tcl_Size
/* Tcl 8.x without the Tcl_Size backport: all the affected APIs use "int" */
typedef int Tcl_Size;
#endif
#endif

#endif   /* PGTCL_COMPAT_H */
