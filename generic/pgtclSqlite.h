#ifndef PGTCLSQLITE_H
#define PGTCLSQLITE_H

#include <tcl.h>
#include "pgtclCompat.h"
extern int Pg_sqlite(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

#endif
