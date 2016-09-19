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

#define RES_HARD_MAX 128
#define RES_START 16

/*
 * Tcl 8.6 and TIP 330/336 compatability
 * New function in 8.6 Tcl_GetErrorLine instead of direct access
 * to errorLine in struct. Define Tcl_GetErrorLine here if less
 * 8.6
 */
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION < 6)
#define Tcl_GetErrorLine(interp) (interp->errorLine)
#endif

/*
 * Each Pg_ConnectionId has a list of Pg_TclNotifies structs, one for each
 * Tcl interpreter that has executed any pg_listens on the connection.
 * We need this arrangement to be able to clean up if an interpreter is
 * deleted while the connection remains open.  A free side benefit is that
 * multiple interpreters can be registered to listen for the same notify
 * name.  (All their callbacks will be called, but in an unspecified order.)
 *
 * We use the same approach for pg_on_connection_loss callbacks, but they
 * are not kept in a hashtable since there's no name associated.
 */

typedef struct Pg_TclNotifies_s
{
	struct Pg_TclNotifies_s *next;		/* list link */
	Tcl_Interp *interp;			/* This Tcl interpreter */

	/*
	 * NB: if interp == NULL, the interpreter is gone but we haven't yet
	 * got round to deleting the Pg_TclNotifies structure.
	 */
	Tcl_HashTable notify_hash;	/* Active pg_listen requests */

	char	   *conn_loss_cmd;	/* pg_on_connection_loss cmd, or NULL */
}	Pg_TclNotifies;

typedef struct Pg_resultid_s
{
    int                id;
    Tcl_Obj            *str;
    Tcl_Interp         *interp;
    Tcl_Command        cmd_token;
    char               *nullValueString;
    struct Pg_ConnectionId_s    *connid;
} Pg_resultid;

typedef struct Pg_ConnectionId_s
{
	char		id[32];
	PGconn	   *conn;
	int			res_max;		/* Max number of results allocated */
	int			res_hardmax;	/* Absolute max to allow */
	int			res_count;		/* Current count of active results */
	int			res_last;		/* Optimize where to start looking */
	int			res_copy;		/* Query result with active copy */
	int			res_copyStatus; /* Copying status */
	PGresult  **results;		/* The results */

	Pg_TclNotifies *notify_list;	/* head of list of notify info */
	int			notifier_running;		/* notify event source is live */
	Tcl_Channel notifier_channel;		/* Tcl_Channel on which notifier
										 * is listening */
	Tcl_Command cmd_token;               /* handle command token */
	Tcl_Interp *interp;               /* save Interp info */
	char       *nullValueString; /* null vals are returned as this, if set */
	Pg_resultid **resultids;       /* resultids (internal storage) */
	int			sql_count;       /* number of pg_exec, pg_select, etc, done */
        Tcl_Obj           *callbackPtr;      /* callback for async queries */
        Tcl_Interp        *callbackInterp;   /* interp where the callback should run */
}	Pg_ConnectionId;



/* Values of res_copyStatus */
#define RES_COPY_NONE	0
#define RES_COPY_INPROGRESS 1
#define RES_COPY_FIN	2


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
