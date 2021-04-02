/*-------------------------------------------------------------------------
 *
 * pgtclCmds.h
 *	  declarations for the C functions which implement pg_* tcl commands
 *
 * Portions Copyright (c) 1996-2003, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id$
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGTCLCMDS_H
#define PGTCLCMDS_H

#include <tcl.h>
#include "libpq-fe.h"

extern int pgtclInitEncoding(Tcl_Interp *interp);

/* MOVED structure definitions for connection IDs to pctclId.h */

/* **************************/
/* registered Tcl functions */
/* **************************/
extern int Pg_conndefaults(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_connect(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_disconnect(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_exec(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_exec_prepared(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_execute(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_select(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_result(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_lo_open(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_lo_close(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_lo_read(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_lo_write(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_lo_lseek(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_lo_creat(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_lo_tell(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_lo_truncate(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_lo_unlink(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_lo_import(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_lo_export(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_listen(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_sendquery(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_sendquery_prepared(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_getresult(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_set_single_row_mode(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_isbusy(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_blocking(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_null_value_string(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_cancelrequest(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_on_connection_loss(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_quote(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_escapeBytea(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_unescapeBytea(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_dbinfo(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_getdata(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

extern int Pg_sql(
  ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

#endif   /* PGTCLCMDS_H */
