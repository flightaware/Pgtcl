/*-------------------------------------------------------------------------
 *
 * pgtclId.c
 *
 *	Contains Tcl "channel" interface routines, plus useful routines
 *	to convert between strings and pointers.  These are needed because
 *	everything in Tcl is a string, but in C, pointers to data structures
 *	are needed.
 *
 *	ASSUMPTION:  sizeof(long) >= sizeof(void*)
 *
 * Portions Copyright (c) 1996-2003, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  $Id$
 *
 *-------------------------------------------------------------------------
 */

#include <errno.h>
#include <string.h>
#include <libpq-fe.h>

#include "pgtclCmds.h"
#include "pgtclId.h"

#ifndef CONST84
#     define CONST84
#endif

static int
PgEndCopy(Pg_ConnectionId * connid, int *errorCodePtr)
{
	connid->res_copyStatus = RES_COPY_NONE;
	if (PQendcopy(connid->conn))
	{
		PQclear(connid->results[connid->res_copy]);
		connid->results[connid->res_copy] =
			PQmakeEmptyPGresult(connid->conn, PGRES_BAD_RESPONSE);
		connid->res_copy = -1;
		*errorCodePtr = EIO;
		return -1;
	}
	else
	{
		PQclear(connid->results[connid->res_copy]);
		connid->results[connid->res_copy] =
			PQmakeEmptyPGresult(connid->conn, PGRES_COMMAND_OK);
		connid->res_copy = -1;
		return 0;
	}
}

/*
 *	Called when reading data (via gets) for a copy <rel> to stdout.
 */
int
PgInputProc(DRIVER_INPUT_PROTO)
{
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	int			avail;

	connid = (Pg_ConnectionId *) cData;
	conn = connid->conn;

	if (connid->res_copy < 0 ||
	 PQresultStatus(connid->results[connid->res_copy]) != PGRES_COPY_OUT)
	{
		*errorCodePtr = EBUSY;
		return -1;
	}

	/*
	 * Read any newly arrived data into libpq's buffer, thereby clearing
	 * the socket's read-ready condition.
	 */
	if (!PQconsumeInput(conn))
	{
		*errorCodePtr = EIO;
		return -1;
	}

	/* Move data from libpq's buffer to Tcl's. */

	avail = PQgetlineAsync(conn, buf, bufSize);

	if (avail < 0)
	{
		/* Endmarker detected, change state and return 0 */
		return PgEndCopy(connid, errorCodePtr);
	}

	return avail;
}

/*
 *	Called when writing data (via puts) for a copy <rel> from stdin
 */
int
PgOutputProc(DRIVER_OUTPUT_PROTO)
{
	Pg_ConnectionId *connid;
	PGconn	   *conn;

	connid = (Pg_ConnectionId *) cData;
	conn = connid->conn;

	if (connid->res_copy < 0 ||
	  PQresultStatus(connid->results[connid->res_copy]) != PGRES_COPY_IN)
	{
		*errorCodePtr = EBUSY;
		return -1;
	}

	if (PQputnbytes(conn, buf, bufSize))
	{
		*errorCodePtr = EIO;
		return -1;
	}

	/*
	 * This assumes Tcl script will write the terminator line in a single
	 * operation; maybe not such a good assumption?
	 */
	if (bufSize >= 3 && strncmp(&buf[bufSize - 3], "\\.\n", 3) == 0)
	{
		if (PgEndCopy(connid, errorCodePtr) == -1)
			return -1;
	}
	return bufSize;
}

/*
 * The WatchProc and GetHandleProc are no-ops but must be present.
 */
static void
PgWatchProc(ClientData instanceData, int mask)
{
}

static int
PgGetHandleProc(ClientData instanceData, int direction,
				ClientData *handlePtr)
{
	return TCL_ERROR;
}

Tcl_ChannelType Pg_ConnType = {
    "pgsql",             /* channel type */
    NULL,                /* blockmodeproc */
    PgDelConnectionId,   /* closeproc */
    PgInputProc,         /* inputproc */
    PgOutputProc,        /* outputproc */
    NULL,                /* SeekProc, Not used */
    NULL,                /* SetOptionProc, Not used */
    NULL,                /* GetOptionProc, Not used */
    PgWatchProc,         /* WatchProc, must be defined */
    PgGetHandleProc,     /* GetHandleProc, must be defined */
    NULL                 /* Close2Proc, Not used */
};

/*
 * Create and register a new channel for the connection
 */
int
PgSetConnectionId(Tcl_Interp *interp, PGconn *conn, char *chandle)
{
	Tcl_Channel     conn_chan;
        Tcl_Obj         *nsstr;
	Pg_ConnectionId *connid;
	int             i;
        CONST char      *ns = "";

	connid = (Pg_ConnectionId *) ckalloc(sizeof(Pg_ConnectionId));
	connid->conn = conn;
	connid->res_count = 0;
	connid->res_last = -1;
	connid->res_max = RES_START;
	connid->res_hardmax = RES_HARD_MAX;
	connid->res_copy = -1;
	connid->res_copyStatus = RES_COPY_NONE;
	connid->results = (PGresult **)ckalloc(sizeof(PGresult *) * RES_START);
	connid->resultids = (Pg_resultid **)ckalloc(sizeof(Pg_resultid *) * RES_START);
        connid->callbackPtr = (Tcl_Obj *) NULL;
        connid->callbackInterp = (Tcl_Interp *) NULL;


	for (i = 0; i < RES_START; i++)
	{
		connid->results[i] = NULL;
		connid->resultids[i] = NULL;
	}

	connid->notify_list = NULL;
	connid->notifier_running = 0;
	connid->interp = interp;
	connid->nullValueString = NULL;
	connid->sql_count = 0;

        nsstr = Tcl_NewStringObj("if {[namespace current] != \"::\"} {set k [namespace current]::}", -1);


        Tcl_EvalObjEx(interp, nsstr, 0);
/*
        Tcl_Eval(interp, "if {[namespace current] != \"::\"} {\
                              set k [namespace current]::\
                           }");
*/
        
        ns = Tcl_GetStringResult(interp);
        Tcl_ResetResult(interp);

        if (chandle == NULL)
        {
	    sprintf(connid->id, "%spgsql%d", ns, PQsocket(conn));
        }
        else
        {
	    sprintf(connid->id, "%s%s", ns, chandle);
        }

        conn_chan = Tcl_GetChannel(interp, connid->id, 0);

	if (conn_chan != NULL)
	{
	    return 0;
	}
	
	connid->notifier_channel = Tcl_MakeTcpClientChannel((ClientData)(long)PQsocket(conn));
	/* Code  executing  outside  of  any Tcl interpreter can call
       Tcl_RegisterChannel with interp as NULL, to indicate  that
       it  wishes  to  hold  a  reference to this channel. Subse-
       quently, the channel can be registered  in  a  Tcl  inter-
       preter and it will only be closed when the matching number
       of calls to Tcl_UnregisterChannel have  been  made.   This
       allows code executing outside of any interpreter to safely
       hold a reference to a channel that is also registered in a
       Tcl interpreter.
	*/
	Tcl_RegisterChannel(NULL, connid->notifier_channel);

	conn_chan = Tcl_CreateChannel(&Pg_ConnType, connid->id, (ClientData) connid,
								  TCL_READABLE | TCL_WRITABLE);

	Tcl_SetChannelOption(interp, conn_chan, "-buffering", "line");
	Tcl_SetResult(interp, connid->id, TCL_VOLATILE);
	Tcl_RegisterChannel(interp, conn_chan);

    connid->cmd_token=Tcl_CreateObjCommand(interp, connid->id, PgConnCmd, (ClientData) connid, PgDelCmdHandle);

    return 1;
}

/* 
 *----------------------------------------------------------------------
 *
 * PgConnCmd --
 *
 *    dispatches the correct command from a handle command
 *
 * Results:
 *    Returns the return value of the command that gets called. If
 *    the command is not found, then a TCL_ERROR is returned
 *
 *----------------------------------------------------------------------
 */
int
PgConnCmd(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int             optIndex;
    int             objvxi;
    int             idx = 1;
    char            *arg;
    Tcl_Obj         *objvx[25];
    Tcl_CmdInfo     info;
    Pg_ConnectionId *connid;
    int             returnCode = TCL_ERROR;

    static CONST84 char *options[] = {
        "quote", "escape_bytea", "unescape_bytea", "disconnect", "exec", 
	"sqlexec", "execute", "select", 
	"listen", "on_connection_loss", "lo_creat", "lo_open", "lo_close", 
        "lo_read", "lo_write", "lo_lseek", "lo_tell", "lo_truncate", 
	"lo_unlink", "lo_import", "lo_export", "sendquery", "exec_prepared", 
        "sendquery_prepared",  "null_value_string", "version", 
        "protocol", "param", "backendpid", "socket", 
	"conndefaults",  "set_single_row_mode", "is_busy", "blocking",
	"cancel_request", (char *)NULL
    };

    enum options
    {
        QUOTE, ESCAPE_BYTEA, UNESCAPE_BYTEA, DISCONNECT, EXEC, 
	SQLEXEC, EXECUTE, SELECT, 
	LISTEN, ON_CONNECTION_LOSS, LO_CREAT, LO_OPEN, LO_CLOSE, 
	LO_READ, LO_WRITE, LO_LSEEK, LO_TELL, LO_TRUNCATE, LO_UNLINK, 
	LO_IMPORT, LO_EXPORT, SENDQUERY, EXEC_PREPARED, 
	SENDQUERY_PREPARED, NULL_VALUE_STRING, VERSION, 
	PROTOCOL, PARAM, BACKENDPID, SOCKET,
	CONNDEFAULTS, SET_SINGLE_ROW_MODE, ISBUSY, BLOCKING,
	CANCELREQUEST
    };

    if (objc == 1 || objc > 25)
    {
	    Tcl_WrongNumArgs(interp, 1, objv, "command...");
	    return TCL_ERROR;
    }

    /*
     *    this assigns the args array with an offset, since
     *    the command handle args looks is offset
     */
    for (objvxi = 2; objvxi < objc; objvxi++) {
        objvx[objvxi] = objv[objvxi];
    }

    /* swap the first and second elements of the copied command */
    objvx[0] = objv[1];
    objvx[1] = objv[0];

    if (Tcl_GetCommandInfo(interp, Tcl_GetStringFromObj(objvx[1], NULL), &info) == 0)
        return TCL_ERROR;

    connid = (Pg_ConnectionId *) info.objClientData;


    if (Tcl_GetIndexFromObj(interp, objv[1], options, "command", TCL_EXACT, &optIndex) != TCL_OK)
                    return TCL_ERROR;

    /*
     *  Need to test here, since EXECUTE and UNESCAPE_BYTEA branches do things
     *  a little differently
    if ((optIndex != EXECUTE) && (optIndex != UNESCAPE_BYTEA))
    {
        objvx[1] = Tcl_NewStringObj(connid->id, -1);
    }
*/

    switch ((enum options) optIndex)
    {
	case QUOTE:
	{
	    /* error if only two args, we gotta pick it up here or
	     * Pg_quote will happily quote and return the connection ID.
	     */
	    if (objc == 2)
	    {
		Tcl_WrongNumArgs(interp, 1, objv, "quote string");
		return TCL_ERROR;
	    }

            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_quote(cData, interp, objc, objvx);
            break;
	}

	case ESCAPE_BYTEA:
	{
	    /* error if only two args, we gotta pick it up here or
	     * Pg_escapeBytea will happily quote and return the connection ID.
	     */
	    if (objc == 2)
	    {
		Tcl_WrongNumArgs(interp, 1, objv, "escape_bytea byteArray");
		return TCL_ERROR;
	    }

            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_escapeBytea(cData, interp, objc, objvx);
            break;
	}

	case UNESCAPE_BYTEA:
	{
	    if (objc != 3)
	    {
		Tcl_WrongNumArgs(interp, 1, objv, "unescape_bytea string");
		return TCL_ERROR;
	    }
	    objvx[1] = objv[2];
            returnCode = Pg_unescapeBytea(cData, interp, 2, objvx);
	    return returnCode;
	}

        case DISCONNECT:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_disconnect(cData, interp, objc, objvx);
            break;
        }
        case EXEC:
        case SQLEXEC:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_exec(cData, interp, objc, objvx);
			break;
        }
        case EXECUTE:
        {
            /*
             * Need a little extra mojo here, since
             * there can be the -array and -oid options
             * before the connection handle -- arrggh
             */
            int num = 2;
            arg = Tcl_GetStringFromObj(objvx[2], NULL);
            if (arg[0] == '-')
            {
                /* see if there are 2 options on the command line */
                arg = Tcl_GetStringFromObj(objvx[4], NULL);
                if (arg[0] == '-')
                {
                    num = 4;
                }

                for (objvxi = 1; objvxi <= num; objvxi++)
                {
                    objvx[objvxi] = objv[objvxi+1];
                }
                objvx[objvxi++] = objv[0];
                
            }
            /*  DEBUGGING
            for (objvxi = 0; objvxi < objc; objvxi++)
            {
                     Tcl_GetStringFromObj(objv[objvxi], NULL), 
                     Tcl_GetStringFromObj(objvx[objvxi], NULL));
            }
            */
               
            idx += num;
            objvx[idx] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_execute(cData, interp, objc, objvx);
			break;
        }
        case SELECT:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_select(cData, interp, objc, objvx);
			break;
        }
        case LISTEN:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_listen(cData, interp, objc, objvx);
			break;
        }
        case ON_CONNECTION_LOSS:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_listen(cData, interp, objc, objvx);
			break;
        }
        case LO_CREAT:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_lo_creat(cData, interp, objc, objvx);
			break;
        }
        case LO_OPEN:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_lo_open(cData, interp, objc, objvx);
			break;
        }
        case LO_CLOSE:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_lo_close(cData, interp, objc, objvx);
			break;
        }
        case LO_READ:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_lo_read(cData, interp, objc, objvx);
			break;
        }
        case LO_WRITE:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_lo_write(cData, interp, objc, objvx);
			break;
        }
        case LO_LSEEK:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_lo_lseek(cData, interp, objc, objvx);
			break;
        }
        case LO_TELL:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_lo_tell(cData, interp, objc, objvx);
			break;
        }
        case LO_TRUNCATE:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_lo_truncate(cData, interp, objc, objvx);
			break;
        }
        case LO_UNLINK:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_lo_unlink(cData, interp, objc, objvx);
			break;
        }
        case LO_IMPORT:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_lo_import(cData, interp, objc, objvx);
			break;
        }
        case LO_EXPORT:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_lo_export(cData, interp, objc, objvx);
			break;
        }
        case SENDQUERY:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_sendquery(cData, interp, objc, objvx);
			break;
        }
        case EXEC_PREPARED:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_exec_prepared(cData, interp, objc, objvx);
			break;
        }
        case SENDQUERY_PREPARED:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_sendquery_prepared(cData, interp, objc, objvx);
			break;
        }
        case NULL_VALUE_STRING:
        {
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_null_value_string(cData, interp, objc, objvx);
			break;
        }
        case PROTOCOL:
        case BACKENDPID:
        case SOCKET:
        case VERSION:
        {
            
            objc = 3;
            objvx[2] = objv[0];
            objvx[1] = objv[1];
            idx++;
            objvx[idx] = Tcl_NewStringObj(connid->id, -1);
            returnCode= Pg_dbinfo(cData, interp, objc, objvx);
	    break;
        }
        case PARAM:
        {
            
            objc = 4;
            objvx[2] = objv[0];
            objvx[1] = objv[1];
            objvx[3] = objv[2];
            idx++;
            objvx[idx] = Tcl_NewStringObj(connid->id, -1);
            returnCode= Pg_dbinfo(cData, interp, objc, objvx);
	    break;
        }
	case CONNDEFAULTS:
	{
            returnCode = Pg_conndefaults(cData, interp, 1, objvx);
	    break;
	}
	
	case SET_SINGLE_ROW_MODE:
	{
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_set_single_row_mode(cData, interp, objc, objvx);
	    break;
	}
	
	case ISBUSY:
	{
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_isbusy(cData, interp, objc, objvx);
	    break;
	}
	
	case BLOCKING:
	{
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_blocking(cData, interp, objc, objvx);
	    break;
	}

	case CANCELREQUEST:
	{
            objvx[1] = Tcl_NewStringObj(connid->id, -1);
            returnCode = Pg_cancelrequest(cData, interp, objc, objvx);
	    break;
	}
    }
	Tcl_DecrRefCount(objvx[idx]);
	return returnCode;
}

/* 
 *----------------------------------------------------------------------
 *
 * PgResultCmd --
 *
 *    dispatches the correct command from a result handle command
 *
 * Results:
 *    Returns the return value of the command that gets called. If
 *    the command is not found, then a TCL_ERROR is returned
 *
 *----------------------------------------------------------------------
 */
int
PgResultCmd(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int    objvxi;
    Tcl_Obj    *objvx[25];

    if (objc == 1 || objc > 25)
    {
	    Tcl_WrongNumArgs(interp, 1, objv, "command...");
	    return TCL_ERROR;
    }

    /*
     *    this assigns the args array with an offset, since
     *    the command handle args looks is offset
     */

    for (objvxi = 0; objvxi < objc; objvxi++) {
        objvx[objvxi + 1] = objv[objvxi];
    }

    objvx[0] = objv[0];

    return Pg_result(cData, interp, objc + 1, objvx);
}



/*
 * Get back the connection from the Id
 */
PGconn *
PgGetConnectionId(Tcl_Interp *interp, CONST84 char *id, Pg_ConnectionId ** connid_p)
{
	Tcl_Channel conn_chan;
	Pg_ConnectionId *connid;
    Tcl_Obj     *tresult;

	conn_chan = Tcl_GetChannel(interp, id, 0);
	if (conn_chan == NULL || Tcl_GetChannelType(conn_chan) != &Pg_ConnType)
	{
            tresult = Tcl_NewStringObj(id, -1);
            Tcl_AppendStringsToObj(tresult, " is not a valid postgresql connection", NULL);
            Tcl_SetObjResult(interp, tresult);

		if (connid_p)
			*connid_p = NULL;
		return NULL;
	}

	connid = (Pg_ConnectionId *) Tcl_GetChannelInstanceData(conn_chan);
	if (connid_p)
		*connid_p = connid;
	return connid->conn;
}


/*
 * Remove a connection Id from the hash table and
 * close all portals the user forgot.
 */
int
PgDelConnectionId(DRIVER_DEL_PROTO)
{
	Tcl_HashEntry *entry;
	Tcl_HashSearch hsearch;
	Pg_ConnectionId *connid;
	Pg_TclNotifies *notifies;
	int			i;
        Pg_resultid    *resultid;


	connid = (Pg_ConnectionId *) cData;

	for (i = 0; i < connid->res_max; i++)
	{
	    if (connid->results[i])
		{
			PQclear(connid->results[i]);


            resultid = connid->resultids[i];

			if (resultid != NULL) {
				Tcl_DecrRefCount(resultid->str);

				if ((resultid->nullValueString != NULL) && (resultid->nullValueString != connid->nullValueString))
					ckfree (resultid->nullValueString);

				ckfree((void *)resultid);
			}
		}
	}
	
	ckfree((void *)connid->results);
	ckfree((void *)connid->resultids);

	/* Release associated notify info */
	while ((notifies = connid->notify_list) != NULL)
	{
		connid->notify_list = notifies->next;
		for (entry = Tcl_FirstHashEntry(&notifies->notify_hash, &hsearch);
			 entry != NULL;
			 entry = Tcl_NextHashEntry(&hsearch))
			ckfree((char *)Tcl_GetHashValue(entry));
		Tcl_DeleteHashTable(&notifies->notify_hash);
		if (notifies->conn_loss_cmd)
			ckfree((void *) notifies->conn_loss_cmd);
                if (notifies->interp)
		Tcl_DontCallWhenDeleted(notifies->interp, PgNotifyInterpDelete,
								(ClientData)notifies);
		ckfree((void *)notifies);
	}

	/*
	 * Turn off the Tcl event source for this connection, and delete any
	 * pending notify and connection-loss events.
	 */
	PgStopNotifyEventSource(connid, 1);
 

	/* Close the libpq connection too */
	PQfinish(connid->conn);
	connid->conn = NULL;

	if (connid->nullValueString != NULL)
		ckfree(connid->nullValueString);

	/*
	 * Kill the notifier channel, too.	We must not do this until after
	 * we've closed the libpq connection, because Tcl will try to close
	 * the socket itself!
	 *
	 * XXX Unfortunately, while this works fine if we are closing due to
	 * explicit pg_disconnect, Tcl versions through 8.4.1 dump core if we
	 * try to do it during interpreter shutdown.  Not clear why, or if
	 * there is a workaround.  For now, accept leakage of the (fairly
	 * small) amount of memory taken for the channel state representation.
	 * Note we are not leaking a socket, since libpq closed that already.
	 */

#if TCL_MAJOR_VERSION >= 8
	if (connid->notifier_channel != NULL && interp != NULL)
        {
		Tcl_UnregisterChannel(NULL, connid->notifier_channel);
         }
#endif

       /*
        * Clear any async result callback, if present.
        */

        if (connid->callbackPtr)    {
           Tcl_DecrRefCount(connid->callbackPtr);
           connid->callbackPtr = NULL;
        }

        if (connid->callbackInterp) {
           Tcl_Release((ClientData) connid->interp);
           connid->callbackInterp = NULL;
        }


	/*
	 * We must use Tcl_EventuallyFree because we don't want the connid
	 * struct to vanish instantly if Pg_Notify_EventProc is active for it.
	 * (Otherwise, closing the connection from inside a pg_listen callback
	 * could lead to coredump.)  Pg_Notify_EventProc can detect that the
	 * connection has been deleted from under it by checking connid->conn.
	 */
	Tcl_EventuallyFree((ClientData)connid, TCL_DYNAMIC);

	return 0;
}



/*
 *----------------------------------------------------------------------
 *
 * PgResultId --
 *
 *    Find a slot for a new result id.  If the table is full, expand 
 *    it by a factor of 2.  However, do not expand past the hard max, 
 *    as the client is probably just not clearing result handles like 
 *    they should.
 *
 * Results:
 *    Returns the result id. If the an error occurs, TCL_ERROR is 
 *    returned. The result handle is put into the interp result.
 *
 *----------------------------------------------------------------------
 */

int
PgSetResultId(Tcl_Interp *interp, CONST84 char *connid_c, PGresult *res)
{
    Tcl_Channel     conn_chan;
    Pg_ConnectionId *connid;
    int             resid,
                    i;
    char            buf[32];
    Tcl_Obj         *cmd;
    Pg_resultid     *resultid;


    conn_chan = Tcl_GetChannel(interp, connid_c, 0);
    if (conn_chan == NULL)
        return TCL_ERROR;
    connid = (Pg_ConnectionId *) Tcl_GetChannelInstanceData(conn_chan);

    /* search, starting at slot after the last one used */
    resid = connid->res_last;
    for (;;)
    {
	/* advance, with wraparound */
        if (++resid >= connid->res_max)
            resid = 0;

            /* this slot empty? */
        if (!connid->results[resid])
        {
            connid->res_last = resid;
            break;	/* success exit */
        }

	/* checked all slots? */
        if (resid == connid->res_last)
            break;	/* failure exit */
    }
    if (connid->results[resid])
    {
        /* no free slot found, so try to enlarge array */
        if (connid->res_max >= connid->res_hardmax)
        {
            Tcl_SetResult(interp, "hard limit on result handles reached",
					  TCL_STATIC);
            return TCL_ERROR;
        }

        connid->res_last = resid = connid->res_max;
        connid->res_max *= 2;

        if (connid->res_max > connid->res_hardmax)
            connid->res_max = connid->res_hardmax;

        connid->results = (PGresult **)ckrealloc((void *)connid->results,
            sizeof(PGresult *) * connid->res_max);

	connid->resultids = (Pg_resultid **)ckrealloc((void *)connid->resultids,
            sizeof(Pg_resultid *) * connid->res_max);

        for (i = connid->res_last; i < connid->res_max; i++)
        {
            connid->results[i] = NULL;
			connid->resultids[i] = NULL;
	}

    }

    connid->results[resid] = res;

    sprintf(buf, "%s.%d", connid_c, resid);
    cmd = Tcl_NewStringObj(buf, -1);

    resultid = (Pg_resultid *) ckalloc(sizeof(Pg_resultid));

    resultid->interp = interp;
    resultid->id     = resid;
    resultid->str = Tcl_NewStringObj(buf, -1);
    resultid->cmd_token = Tcl_CreateObjCommand(interp, buf, 
        PgResultCmd, (ClientData) resultid, PgDelResultHandle);
	resultid->connid = connid;
	resultid->nullValueString = connid->nullValueString;

    connid->resultids[resid] = resultid;

    Tcl_SetObjResult(interp, cmd);

    return resid;
}


static int
getresid(Tcl_Interp *interp, CONST84 char *id, Pg_ConnectionId ** connid_p)
{
	Tcl_Channel conn_chan;
	char	   *mark;
	int			resid;
	Pg_ConnectionId *connid;

	if (!(mark = strrchr(id, '.')))
	{
		return -1;
	}
	*mark = '\0';
	conn_chan = Tcl_GetChannel(interp, id, 0);
	*mark = '.';
	if (conn_chan == NULL || Tcl_GetChannelType(conn_chan) != &Pg_ConnType)
	{
		Tcl_SetResult(interp, "Invalid connection handle", TCL_STATIC);
		return -1;
	}

	if (Tcl_GetInt(interp, mark + 1, &resid) == TCL_ERROR)
	{
		Tcl_SetResult(interp, "Poorly formated result handle", TCL_STATIC);
		return -1;
	}

	connid = (Pg_ConnectionId *) Tcl_GetChannelInstanceData(conn_chan);

	if (resid < 0 || resid >= connid->res_max || connid->results[resid] == NULL)
	{
		
		Tcl_SetResult(interp, "Invalid result handle", TCL_STATIC);
		return -1;
	}

	*connid_p = connid;

	return resid;
}


/*
 * Get back the result pointer from the Id
 */
PGresult *
PgGetResultId(Tcl_Interp *interp, CONST84 char *id, Pg_resultid **resultidPtr)
{
	Pg_ConnectionId *connid;
	int			resid;

	if (!id)
		return NULL;
	resid = getresid(interp, id, &connid);
	if (resid == -1)
		return NULL;

	if (resultidPtr != NULL) {
		*resultidPtr = connid->resultids[resid];
	}
	return connid->results[resid];
}


/*
 * Remove a result Id from the hash tables
 */
void
PgDelResultId(Tcl_Interp *interp, CONST84 char *id)
{
	Pg_ConnectionId *connid;
	Pg_resultid     *resultid;
	int			resid;

	resid = getresid(interp, id, &connid);
	if (resid == -1)
		return;

	connid->results[resid] = 0;

	resultid = connid->resultids[resid];

	Tcl_DecrRefCount((Tcl_Obj *)resultid->str);

	if ((resultid->nullValueString != NULL) && (resultid->nullValueString != connid->nullValueString))
		ckfree (resultid->nullValueString);

	ckfree((void *)resultid);
	connid->resultids[resid] = 0;
}


/*
 * Get the connection Id from the result Id
 */
int
PgGetConnByResultId(Tcl_Interp *interp, CONST84 char *resid_c)
{
	char	   *mark;
	Tcl_Channel conn_chan;
    Tcl_Obj     *tresult;

	if (!(mark = strrchr(resid_c, '.')))
		goto error_out;
	*mark = '\0';
	conn_chan = Tcl_GetChannel(interp, resid_c, 0);
	*mark = '.';
	if (conn_chan && Tcl_GetChannelType(conn_chan) == &Pg_ConnType)
	{
		Tcl_SetResult(interp, (char *)Tcl_GetChannelName(conn_chan), TCL_VOLATILE);
		return TCL_OK;
	}

error_out:
        tresult = Tcl_NewStringObj(resid_c, -1);
        Tcl_AppendStringsToObj(tresult, " is not a valid connection\n", NULL);
        Tcl_SetObjResult(interp, tresult);

	return TCL_ERROR;
}




/*-------------------------------------------
  Notify event source

  These functions allow asynchronous notify messages arriving from
  the SQL server to be dispatched as Tcl events.  See the Tcl
  Notifier(3) man page for more info.

  The main trick in this code is that we have to cope with status changes
  between the queueing and the execution of a Tcl event.  For example,
  if the user changes or cancels the pg_listen callback command, we should
  use the new setting; we do that by not resolving the notify relation
  name until the last possible moment.
  We also have to handle closure of the channel or deletion of the interpreter
  to be used for the callback (note that with multiple interpreters,
  the channel can outlive the interpreter it was created by!)
  Upon closure of the channel, we immediately delete the file event handler
  for it, which has the effect of disabling any file-ready events that might
  be hanging about in the Tcl event queue.	But for interpreter deletion,
  we just set any matching interp pointers in the Pg_TclNotifies list to NULL.
  The list item stays around until the connection is deleted.  (This avoids
  trouble with walking through a list whose members may get deleted under us.)

  In the current design, Pg_Notify_FileHandler is a file handler that
  we establish by calling Tcl_CreateFileHandler().	It gets invoked from
  the Tcl event loop whenever the underlying PGconn's socket is read-ready.
  We suck up any available data (to clear the OS-level read-ready condition)
  and then transfer any available PGnotify events into the Tcl event queue.
  Eventually these events will be dispatched to Pg_Notify_EventProc.  When
  we do an ordinary PQexec, we must also transfer PGnotify events into Tcl's
  event queue, since libpq might have read them when we weren't looking.
  ------------------------------------------*/

typedef struct
{
	Tcl_Event	header;			/* Standard Tcl event info */
	PGnotify   *notify;			/* Notify event from libpq, or NULL */
	/* We use a NULL notify pointer to denote a connection-loss event */
	Pg_ConnectionId *connid;	/* Connection for server */
}	NotifyEvent;

/* Dispatch a NotifyEvent that has reached the front of the event queue */

static int
Pg_Notify_EventProc(Tcl_Event *evPtr, int flags)
{
	NotifyEvent *event = (NotifyEvent *) evPtr;
	Pg_TclNotifies *notifies;
	char	   *callback;
	char	   *svcallback;

	/* We classify SQL notifies as Tcl file events. */
	if (!(flags & TCL_FILE_EVENTS))
		return 0;

	/* If connection's been closed, just forget the whole thing. */
	if (event->connid == NULL)
	{
		if (event->notify)
                {
                    #ifdef PQfreemem
                        PQfreemem(event->notify);
                    #else
                        PQfreeNotify(event->notify);
                    #endif
                }
		return 1;
	}

	/*
	 * Preserve/Release to ensure the connection struct doesn't disappear
	 * underneath us.
	 */
	Tcl_Preserve((ClientData)event->connid);

	/*
	 * Loop for each interpreter that has ever registered on the
	 * connection. Each one can get a callback.
	 */

	for (notifies = event->connid->notify_list;
		 notifies != NULL;
		 notifies = notifies->next)
	{
		Tcl_Interp *interp = notifies->interp;

		if (interp == NULL)
			continue;			/* ignore deleted interpreter */

		/*
		 * Find the callback to be executed for this interpreter, if any.
		 */
		if (event->notify)
		{
			/* Ordinary NOTIFY event */
			Tcl_HashEntry *entry;

			entry = Tcl_FindHashEntry(&notifies->notify_hash,
									  event->notify->relname);
			if (entry == NULL)
				continue;		/* no pg_listen in this interpreter */
			callback = (char *) Tcl_GetHashValue(entry);
		}
		else
		{
			/* Connection-loss event */
			callback = notifies->conn_loss_cmd;
		}

		if (callback == NULL)
			continue;			/* nothing to do for this interpreter */

		/*
		 * We have to copy the callback string in case the user executes a
		 * new pg_listen or pg_on_connection_loss during the callback.
		 */
		svcallback = (char *)ckalloc((unsigned)(strlen(callback) + 1));
		strcpy(svcallback, callback);

		/*
		 * Execute the callback.
		 */
		Tcl_Preserve((ClientData)interp);
		if (Tcl_GlobalEval(interp, svcallback) != TCL_OK)
		{
			if (event->notify)
				Tcl_AddErrorInfo(interp, "\n    (\"pg_listen\" script)");
			else
				Tcl_AddErrorInfo(interp, "\n    (\"pg_on_connection_loss\" script)");
			Tcl_BackgroundError(interp);
		}
		Tcl_Release((ClientData)interp);
		ckfree(svcallback);

		/*
		 * Check for the possibility that the callback closed the
		 * connection.
		 */
		if (event->connid->conn == NULL)
			break;
	}

	Tcl_Release((ClientData)event->connid);

	if (event->notify)
        {
            #ifdef PQfreemem
                PQfreemem(event->notify);
            #else
                PQfreeNotify(event->notify);
            #endif
        }

	return 1;
}

/*
 * Transfer any notify events available from libpq into the Tcl event queue.
 * Note that this must be called after each PQexec (to capture notifies
 * that arrive during command execution) as well as in Pg_Notify_FileHandler
 * (to capture notifies that arrive when we're idle).
 */

void
PgNotifyTransferEvents(Pg_ConnectionId * connid)
{
	PGnotify   *notify;

	while ((notify = PQnotifies(connid->conn)) != NULL)
	{
		NotifyEvent *event = (NotifyEvent *) ckalloc(sizeof(NotifyEvent));

		event->header.proc = Pg_Notify_EventProc;
		event->notify = notify;
		event->connid = connid;
		Tcl_QueueEvent((Tcl_Event *) event, TCL_QUEUE_TAIL);
	}

	/*
	 * This is also a good place to check for unexpected closure of the
	 * connection (ie, backend crash), in which case we must shut down the
	 * notify event source to keep Tcl from trying to select() on the now-
	 * closed socket descriptor.  But don't kill on-connection-loss
	 * events; in fact, register one.
	 */
	if (PQsocket(connid->conn) < 0)
		PgConnLossTransferEvents(connid);
}

/*
 * Handle a connection-loss event
 */
void
PgConnLossTransferEvents(Pg_ConnectionId * connid)
{
	if (connid->notifier_running)
	{
		/* Put the on-connection-loss event in the Tcl queue */
		NotifyEvent *event = (NotifyEvent *) ckalloc(sizeof(NotifyEvent));

		event->header.proc = Pg_Notify_EventProc;
		event->notify = NULL;
		event->connid = connid;
		Tcl_QueueEvent((Tcl_Event *) event, TCL_QUEUE_TAIL);
	}

	/*
	 * Shut down the notify event source to keep Tcl from trying to
	 * select() on the now-closed socket descriptor.  And zap any
	 * unprocessed notify events ... but not, of course, the
	 * connection-loss event.
	 */
	PgStopNotifyEventSource(connid, 0);
}

/*
 * Cleanup code for coping when an interpreter or a channel is deleted.
 *
 * PgNotifyInterpDelete is registered as an interpreter deletion callback
 * for each extant Pg_TclNotifies structure.
 * NotifyEventDeleteProc is used by PgStopNotifyEventSource to cancel
 * pending Tcl NotifyEvents that reference a dying connection.
 */

void
PgNotifyInterpDelete(ClientData clientData, Tcl_Interp *interp)
{
	/* Mark the interpreter dead, but don't do anything else yet */
	Pg_TclNotifies *notifies = (Pg_TclNotifies *) clientData;

	notifies->interp = NULL;
}

/*
 * Comparison routines for detecting events to be removed by Tcl_DeleteEvents.
 * NB: In (at least) Tcl versions 7.6 through 8.0.3, there is a serious
 * bug in Tcl_DeleteEvents: if there are multiple events on the queue and
 * you tell it to delete the last one, the event list pointers get corrupted,
 * with the result that events queued immediately thereafter get lost.
 * Therefore we daren't tell Tcl_DeleteEvents to actually delete anything!
 * We simply use it as a way of scanning the event queue.  Events matching
 * the about-to-be-deleted connid are marked dead by setting their connid
 * fields to NULL.	Then Pg_Notify_EventProc will do nothing when those
 * events are executed.
 */
static int
NotifyEventDeleteProc(Tcl_Event *evPtr, ClientData clientData)
{
	Pg_ConnectionId *connid = (Pg_ConnectionId *) clientData;

	if (evPtr->proc == Pg_Notify_EventProc)
	{
		NotifyEvent *event = (NotifyEvent *) evPtr;

		if (event->connid == connid && event->notify != NULL)
			event->connid = NULL;
	}
	return 0;
}

/* This version deletes on-connection-loss events too */
static int
AllNotifyEventDeleteProc(Tcl_Event *evPtr, ClientData clientData)
{
	Pg_ConnectionId *connid = (Pg_ConnectionId *) clientData;

	if (evPtr->proc == Pg_Notify_EventProc)
	{
		NotifyEvent *event = (NotifyEvent *) evPtr;

		if (event->connid == connid)
			event->connid = NULL;
	}
	return 0;
}
static int
Pg_Result_EventProc(Tcl_Event *evPtr, int flags)
{
    NotifyEvent *event = (NotifyEvent *) evPtr;

    /* Results can only come from file events. */
    if (!(flags & TCL_FILE_EVENTS))
       return 0;

    /* If connection's been closed, just forget the whole thing. */
    if (event->connid) {
       Pg_ConnectionId *connid = event->connid;
       Tcl_Obj *callbackPtr = connid->callbackPtr;
       Tcl_Interp *interp = connid->callbackInterp;

       /* Clear the result callback for this connection, so that the callback
        * script may safely establish a new one. */

       connid->callbackPtr = NULL;
       connid->callbackInterp = NULL;

       if (callbackPtr || interp) {
           if (TCL_OK != Tcl_EvalObjEx(interp, callbackPtr, TCL_EVAL_GLOBAL)) {
               Tcl_BackgroundError(interp);
           }
           Tcl_DecrRefCount(callbackPtr);
           Tcl_Release((ClientData) interp);
       }
    }
    /* never deliver this event twice */
    return 1;
}

/*
 * File handler callback: called when Tcl has detected read-ready on socket.
 * The clientData is a pointer to the associated connection.
 * We can ignore the condition mask since we only ever ask about read-ready.
 */

static void
Pg_Notify_FileHandler(ClientData clientData, int mask)
{
	Pg_ConnectionId *connid = (Pg_ConnectionId *) clientData;

	/*
	 * Consume any data available from the SQL server (this just buffers
	 * it internally to libpq; but it will clear the read-ready
	 * condition).
	 */
	if (PQconsumeInput(connid->conn))
	{
		/* Transfer notify events from libpq to Tcl event queue. */
		PgNotifyTransferEvents(connid);

               /* If the connection is still alive, and if there is a
                * callback for results, check if a result is ready. If it is,
                * transfer the event to the Tcl event queue.
                */

               if ((PQsocket(connid->conn) >= 0)
                       && connid->callbackPtr
                       && !PQisBusy(connid->conn)) {

                   NotifyEvent *event = (NotifyEvent *) ckalloc(sizeof(NotifyEvent));

                   event->header.proc = Pg_Result_EventProc;
                   event->notify = NULL;
                   event->connid = connid;
                   Tcl_QueueEvent((Tcl_Event *) event, TCL_QUEUE_TAIL);
                }


	}
	else
	{
		/*
		 * If there is no input but we have read-ready, assume this means
		 * we lost the connection.
		 */
		PgConnLossTransferEvents(connid);
	}
}


/*
 * Start and stop the notify event source for a connection.
 *
 * We do not bother to run the notifier unless at least one pg_listen
 * or pg_on_connection_loss has been executed on the connection.  Currently,
 * once started the notifier is run until the connection is closed.
 *
 * FIXME: if PQreset is executed on the underlying PGconn, the active
 * socket number could change.	How and when should we test for this
 * and update the Tcl file handler linkage?  (For that matter, we'd
 * also have to reissue LISTEN commands for active LISTENs, since the
 * new backend won't know about 'em.  I'm leaving this problem for
 * another day.)
 */

void
PgStartNotifyEventSource(Pg_ConnectionId * connid)
{
	/* Start the notify event source if it isn't already running */
	if (!connid->notifier_running)
	{
		int			pqsock = PQsocket(connid->conn);

		if (pqsock >= 0)
		{
			Tcl_CreateChannelHandler(connid->notifier_channel,
									 TCL_READABLE,
									 Pg_Notify_FileHandler,
									 (ClientData) connid);
			connid->notifier_running = 1;
		}
	}
}

void
PgStopNotifyEventSource(Pg_ConnectionId * connid, pqbool allevents)
{
	/* Remove the event source */
	if (connid->notifier_running)
	{
		Tcl_DeleteChannelHandler(connid->notifier_channel,
							  Pg_Notify_FileHandler, (ClientData)connid);
		connid->notifier_running = 0;
	}

	/* Kill queued Tcl events that reference this channel */

      if (allevents)
               Tcl_DeleteEvents(AllNotifyEventDeleteProc, (ClientData) connid);
       else
               Tcl_DeleteEvents(NotifyEventDeleteProc, (ClientData) connid);
}


void
PgDelCmdHandle(ClientData cData)
{
    Pg_ConnectionId *connid = (Pg_ConnectionId *) cData;
    Tcl_Channel conn_chan;
    Tcl_Obj         *tresult;
	Pg_resultid     *resultid;
	int              i = 0;

    conn_chan = Tcl_GetChannel(connid->interp, connid->id, 0);

    if (conn_chan == NULL)
    {
        tresult = Tcl_NewStringObj("conn->id", -1);
        Tcl_AppendStringsToObj(tresult, " is not a valid connection", NULL);
        Tcl_SetObjResult(connid->interp, tresult);

        return;
    }

    if (connid->conn == NULL)
        return;


    for (i = 0; i <= connid->res_last; i++)
    {
 
        resultid = connid->resultids[i];

        if (resultid)
        {
            Tcl_DeleteCommandFromToken(resultid->interp, resultid->cmd_token);
        }
    }
        
    
    Tcl_UnregisterChannel(connid->interp, conn_chan);

    return;
}

void
PgDelResultHandle(ClientData cData)
{

    PGresult       *result;
    Pg_resultid    *resultid = (Pg_resultid *) cData;
    char           *resstr;

    resstr = Tcl_GetStringFromObj(resultid->str, NULL);
    
    result = PgGetResultId(resultid->interp, resstr, NULL);

    PgDelResultId(resultid->interp, resstr);
    PQclear(result);

    return;
}
