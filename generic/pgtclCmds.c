/*-------------------------------------------------------------------------
 *
 * pgtclCmds.c
 *	  C functions which implement pg_* tcl commands
 *
 * Portions Copyright (c) 1996-2004, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Id$
 *
 *-------------------------------------------------------------------------
 */

#include <ctype.h>
#include <string.h>
#include <libpq-fe.h>
#include <assert.h>

#include "pgtclCmds.h"
#include "pgtclId.h"
#include "libpq/libpq-fs.h"		/* large-object interface */
#include "tokenize.h"

/*
 * Local function forward declarations
 */
static int execute_put_values(Tcl_Interp *interp, const char *array_varname,
				   PGresult *result, char *nullString, int tupno);

static int count_parameters(Tcl_Interp *interp, const char *queryString,
				    int *nParamsPtr);

static int expand_parameters(Tcl_Interp *interp, const char *queryString,
				    int nParams, char *paramArrayName,
				    char **newQueryStringPtr, const char ***paramValuesPtr,
				    const char **bufferPtr);

static int build_param_array(Tcl_Interp *interp, int nParams, Tcl_Obj *CONST objv[], const char ***paramValuesPtr, const char **bufferPtr);

static void report_connection_error(Tcl_Interp *interp, PGconn *conn);

static Tcl_Encoding utf8encoding = NULL;

/*
 * Initialize utf8encoding
 */
int pgtclInitEncoding(Tcl_Interp *interp) {
	utf8encoding = Tcl_GetEncoding(interp, "utf-8");
	if (utf8encoding != NULL)
		return TCL_OK;
	return TCL_ERROR;
}

// stringStorage is used to track the storage used by externalString and utfString.
//
// If you need more than one Dstring in flight at once, you need separate stringStorage objects
//
struct stringStorage {
	Tcl_DString tmpds;
	int allocated;
};

void initStorage(struct stringStorage *storage) {
	storage->allocated = 0;
}

void freeStorage(struct stringStorage *storage) {
	if(storage->allocated) Tcl_DStringFree(&storage->tmpds);
	storage->allocated = 0;
}

// The following two functions "waste" a DStrings storage by not freeing it until it's needed again
// This is a little sloppy but massively simplifies the use since just about every place it's used
// has to handle a possible early error return

/*
 * Convert one utf string at a time to an external string, hiding the DString management.
 */
char *externalString(struct stringStorage *storage, const char *utfString)
{
	if(storage->allocated) Tcl_DStringFree(&storage->tmpds);
	storage->allocated = 1;
	return Tcl_UtfToExternalDString(utf8encoding, utfString, -1, &storage->tmpds);
}

/*
 * Convert one external string at a time to a utf string, hiding the DString management.
 */
char *utfString(struct stringStorage *storage, const char *externalString)
{
	if(storage->allocated) Tcl_DStringFree(&storage->tmpds);
	storage->allocated = 1;
	return Tcl_ExternalToUtfDString(utf8encoding, externalString, -1, &storage->tmpds);
}

// TODO something to simplify an array worth of external or utf strings in flight at a time

/*
 * PGgetvalue()
 *
 * This function gets a field result string for a specified PGresult, tuple 
 * number and field number.  If the string is empty and the connection has
 * a non-empty null string value defined, the field is checked to see if
 * the returned field is actually null and, if so, the null string value
 * associated with the connection is returned.
 *
 */

static char *
PGgetvalue ( PGresult *result, char *nullString, int tupno, int fieldNumber )
{
    char *string;

    string = PQgetvalue (result, tupno, fieldNumber);

	/* if the returned string is empty, see if we have a non-empty null
	 * string value set for this connection and, if so, see if the
	 * value returned is null.  If it is, return the null string.
	 */
	if (*string == '\0') {
		if ((nullString != NULL) && (*nullString != '\0')) {
			if (PQgetisnull (result, tupno, fieldNumber)) {
				return nullString;
			}
		}
		/* string is empty but is either not null or null string is empty,
		 * return the empty string
		 */
		return string;
	}

	/* string is not empty */
	return string;
}

/**********************************
 * pg_conndefaults

 syntax:
 pg_conndefaults

 the return result is a list describing the possible options and their
 current default values for a call to pg_connect with the new -conninfo
 syntax. Each entry in the list is a sublist of the format:

	 {optname label dispchar dispsize value}

 **********************************/

int
Pg_conndefaults(ClientData cData, Tcl_Interp *interp, int objc,
				Tcl_Obj *CONST objv[])
{
	PQconninfoOption *options = PQconndefaults();
	PQconninfoOption *option;

	if (objc != 1)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "");
		return TCL_ERROR;
	}

	if (options)
	{
		Tcl_Obj    *resultList = Tcl_NewListObj(0, NULL);

		Tcl_SetListObj(resultList, 0, NULL);

		for (option = options; option->keyword != NULL; option++)
		{
			char	   *val = option->val ? option->val : "";

			/* start a sublist */
			Tcl_Obj    *subList = Tcl_NewListObj(0, NULL);

			if (Tcl_ListObjAppendElement(interp, subList,
					 Tcl_NewStringObj(option->keyword, -1)) == TCL_ERROR)
				return TCL_ERROR;

			if (Tcl_ListObjAppendElement(interp, subList,
					   Tcl_NewStringObj(option->label, -1)) == TCL_ERROR)
				return TCL_ERROR;

			if (Tcl_ListObjAppendElement(interp, subList,
					Tcl_NewStringObj(option->dispchar, -1)) == TCL_ERROR)
				return TCL_ERROR;

			if (Tcl_ListObjAppendElement(interp, subList,
						   Tcl_NewIntObj(option->dispsize)) == TCL_ERROR)
				return TCL_ERROR;

			if (Tcl_ListObjAppendElement(interp, subList,
								 Tcl_NewStringObj(val, -1)) == TCL_ERROR)
				return TCL_ERROR;

			if (Tcl_ListObjAppendElement(interp, resultList,
										 subList) == TCL_ERROR)
				return TCL_ERROR;
		}
        Tcl_SetObjResult(interp, resultList);
		PQconninfoFree(options);
	}
	return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Pg_connect --
 *
 *    make a connection to a backend.
 *    
 * Syntax:
 *    pg_connect dbName [-host hostName] [-port portNumber] [-tty pqtty]]
 *    pg_connect -conninfo "dbname=myydb host=myhost ..."
 *    pg_connect -connlist [list dbname mydb host myhost ...]
 *    pg_connect -connhandle myhandle
 *
 * Results:
 *    the return result is either an error message or a handle for 
 *    a database connection.  Handles start with the prefix "pgsql"
 *
 *----------------------------------------------------------------------
 */

int
Pg_connect(ClientData cData, Tcl_Interp *interp, int objc,
		   Tcl_Obj *CONST objv[])
{
    PGconn	    *conn;
    char	    *connhandle = NULL;
    int             optIndex, i, skip = 0;
    Tcl_DString     ds, utfds;
    Tcl_Obj         *tresult;
    int             async = 0;
        

    static const char *options[] = {
    	"-host", "-port", "-tty", "-options", "-user", 
        "-password", "-conninfo", "-connlist", "-connhandle",
        "-async", (char *)NULL
    };

    enum options
    {
    	OPT_HOST, OPT_PORT, OPT_TTY, OPT_OPTIONS, OPT_USER, 
        OPT_PASSWORD, OPT_CONNINFO, OPT_CONNLIST, OPT_CONNHANDLE,
        OPT_ASYNC
    };

    Tcl_DStringInit(&ds);

    if (objc == 1)
    {
        Tcl_DStringAppend(&ds, "pg_connect: database name missing\n", -1);
        Tcl_DStringAppend(&ds, "pg_connect databaseName [-host hostName] [-port portNumber] [-tty pgtty]\n", -1);
        Tcl_DStringAppend(&ds, "pg_connect -conninfo conninfoString\n", -1);
        Tcl_DStringAppend(&ds, "pg_connect -connlist [connlist]", -1);
        Tcl_DStringResult(interp, &ds);

        return TCL_ERROR;
    }



    i = objc%2 ? 1 : 2;

    while (i + 1 < objc)
    {
        char	   *nextArg = Tcl_GetString(objv[i + 1]);

        if (Tcl_GetIndexFromObj(interp, objv[i], options,
		   "option", TCL_EXACT, &optIndex) != TCL_OK)
		    return TCL_ERROR;

        switch ((enum options) optIndex)
        {
            case OPT_HOST:
            {
                Tcl_DStringAppend(&ds, " host=", -1);
                i += 2;
                break;
            }

            case OPT_PORT:
            {
                Tcl_DStringAppend(&ds, " port=", -1);
                i += 2;
                break;
            }

            case OPT_TTY:
            {
                Tcl_DStringAppend(&ds, " tty=", -1);
                i += 2;
                break;
            }

            case OPT_OPTIONS:
            {
                Tcl_DStringAppend(&ds, " options=", -1);
                i += 2;
                break;
            }
            case OPT_USER:
            {
                Tcl_DStringAppend(&ds, " user=", -1);
                i += 2;
                break;
            }
            case OPT_PASSWORD:
            {
                Tcl_DStringAppend(&ds, " password=", -1);
                i += 2;
                break;
            }
            case OPT_CONNINFO:
            {
                    i += 2;
                    break;
            }
            case OPT_CONNLIST:
            {
                Tcl_Obj    **elemPtrs;
                int        count, lelem;

                Tcl_ListObjGetElements(interp, objv[i + 1], &count, &elemPtrs);

                if (count % 2 != 0)
                {
	            Tcl_WrongNumArgs(interp,1,objv,"-connlist {opt val ...}");
                    Tcl_DStringFree(&ds);

		    return TCL_ERROR;
                }

                for (lelem = 0; lelem < count; lelem=lelem+2) {

                    Tcl_DStringAppend(&ds, " ", -1);
                    Tcl_DStringAppend(&ds, 
                        Tcl_GetString(elemPtrs[lelem]), -1);
                    Tcl_DStringAppend(&ds, "=", -1);
                    Tcl_DStringAppend(&ds, 
                        Tcl_GetString(elemPtrs[lelem+1]), -1);
                }
                i += 2;
                skip = 1;
                break;
            }
            case OPT_CONNHANDLE:
            {
                connhandle = nextArg;
                i += 2;
                skip = 1;
                break;
            }
            case OPT_ASYNC:
            {
				 if (Tcl_GetBooleanFromObj(interp, objv[i + 1], &async) == TCL_ERROR) {
					Tcl_AddErrorInfo (interp, " while converting -async argument");
					return TCL_ERROR;
				 }
                i += 2;
                skip = 1;
            }
        } /** end switch **/

        if (!skip)
        {
            Tcl_DStringAppend(&ds, nextArg, -1);
        }
        skip = 0;

    } /* end while */

    /*
     *    if even numbered args, then assume connect dbname ?option val? ...
     *    and put dbname into conn string
     */
    if (objc % 2 == 0)
    {
	    if ((i % 2 != 0) || i != objc)
	    {
	        Tcl_WrongNumArgs(interp, 1, objv, 
                    "databaseName ?-host hostName? ?-port portNumber? ?-tty pgtty? ?-options pgoptions?");
                Tcl_DStringFree(&ds);

	        return TCL_ERROR;
	    }

        Tcl_DStringAppend(&ds, " dbname=", -1);
        Tcl_DStringAppend(&ds, Tcl_GetString(objv[1]), -1);
    }

    Tcl_ExternalToUtfDString(NULL, Tcl_DStringValue(&ds), -1, &utfds);
    Tcl_DStringFree(&ds);

    if (async)
    {
        conn = PQconnectStart(Tcl_DStringValue(&utfds));
    } 
    else 
    {
        conn = PQconnectdb(Tcl_DStringValue(&utfds));
    }

    if (conn == NULL)
    {
        Tcl_SetResult(interp, "Could not allocate connection", TCL_STATIC);
        return TCL_ERROR;
    }

    Tcl_DStringFree(&utfds);

    if (PQstatus(conn) != CONNECTION_BAD)
    {
        if (PgSetConnectionId(interp, conn, connhandle))
        {
            return TCL_OK;
        }

    }

	tresult = Tcl_NewStringObj("Connection to database failed\n", -1);
        if (PQstatus(conn) != CONNECTION_OK)
	{
	    Tcl_AppendStringsToObj(tresult, PQerrorMessage(conn), NULL);
        }
	else
	{
            Tcl_AppendStringsToObj(tresult, "handle already exists", NULL);
	}

	Tcl_SetObjResult(interp, tresult);
        PQfinish(conn);

        return TCL_ERROR;
   
}


/**********************************
 * pg_disconnect
 close a backend connection

 syntax:
 pg_disconnect connection

 The argument passed in must be a connection pointer.

 **********************************/

int
Pg_disconnect(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Pg_ConnectionId *connid;
    Tcl_Channel conn_chan;
    const char	   *connString;
    Tcl_Obj         *tresult;

    if (objc != 2)
    {
	Tcl_WrongNumArgs(interp, 1, objv, "connection");
	return TCL_ERROR;
    }

    connString = Tcl_GetString(objv[1]);
    conn_chan = Tcl_GetChannel(interp, connString, 0);
    if (conn_chan == NULL)
    {
        tresult = Tcl_NewStringObj(connString, -1);
        Tcl_AppendStringsToObj(tresult, " is not a valid connection", NULL);
        Tcl_SetObjResult(interp, tresult);

	return TCL_ERROR;
    }

    /* Check that it is a PG connection and not something else */
    connid = (Pg_ConnectionId *) Tcl_GetChannelInstanceData(conn_chan);

    if (connid->conn == NULL)
	return TCL_ERROR;

    /*
     *    We use to call Tcl_UnregisterChannel here, but since
     *    we have a command deletion callback now, that gets
     *    taken care of there (PgDelCmdHandle), by deleting the command
     *    here.
     */
    if (connid->cmd_token != NULL)
    {
        Tcl_DeleteCommandFromToken(interp, connid->cmd_token);
    }

    return TCL_OK;
}

/* helper for build_param_array and other related functions.
** convert nParams strings in paramValues, lengths in paramLengths,
** return allocated buffer containing new strings in bufferPtr
*/
int array_to_utf8(Tcl_Interp *interp, const char **paramValues, int *paramLengths, int nParams, const char **bufferPtr)
{
	int param;
	int charsWritten;
	char *nextDestByte, *paramsBuffer;
	int remaining;
	int lengthRequired = 0;

	for (param = 0; param < nParams; param++) {
	    lengthRequired += paramLengths[param] + 1;
	}

	lengthRequired += 4; //(Tcl_UtfToExternal assumes it will need 4 bytes for the last character)

	nextDestByte = paramsBuffer = (char *)ckalloc(lengthRequired);
	remaining = lengthRequired;

	for(param = 0; param < nParams; param++) {
	    int errcode;
	    if(!paramLengths[param] || !paramValues[param]) {
		continue;
	    }
	    // the arguments to Tcl_UtfToExternal are hellish
	    if( TCL_OK != (errcode = Tcl_UtfToExternal(interp, utf8encoding, paramValues[param], paramLengths[param], 0, NULL, nextDestByte, remaining, NULL, &charsWritten, NULL))) {
		Tcl_Obj *tresult;
		char errmsg[256];

		sprintf(errmsg, "Errcode %d attempting to convert param %d: ", errcode, param);
		tresult = Tcl_NewStringObj(errmsg, -1);
		Tcl_AppendStringsToObj(tresult, paramValues[param], NULL);
		if(errcode == TCL_CONVERT_NOSPACE) { // CAN'T HAPPEN, check anyway
		    sprintf(errmsg, " (%d bytes needed, %d bytes available)", paramLengths[param], remaining);
		    Tcl_AppendStringsToObj(tresult, errmsg, NULL);
		}
		Tcl_SetObjResult(interp, tresult);

		ckfree(paramsBuffer);
		return errcode;
	    }
	    paramValues[param] = nextDestByte;
	    nextDestByte += charsWritten;
	    *nextDestByte++ = '\0';
	    remaining -= charsWritten + 1;
	}

	*bufferPtr = paramsBuffer;
	return TCL_OK;
}

/* build_param_array - helper for pg_exec and pg_sendquery */
/* If there are any extra params, allocate paramValues and fill it
 * with the string representations of all of the extra parameters
 * substituted on the command line.  Otherwise nParams will be 0,
 * and PQexecParams will work just like PQexec (no $-substitutions).
 * The magic string NULL is replaced by a null value! // TODO - make this use null value string
 */
int build_param_array(Tcl_Interp *interp, int nParams, Tcl_Obj *CONST objv[], const char ***paramValuesPtr, const char **bufferPtr)
{
	const char **paramValues  = NULL;
	int         *paramLengths = NULL;
	int          param;

	if(nParams == 0)
	    return TCL_OK;

	paramValues = (const char **)ckalloc (nParams * sizeof (char *));
	paramLengths = (int *)ckalloc(nParams * sizeof(int));

	for (param = 0; param < nParams; param++) {
	    int newLength = 0;
	    paramValues[param] = Tcl_GetStringFromObj(objv[param], &newLength);
	    if (strcmp(paramValues[param], "NULL") == 0)
            {
                paramValues[param] = NULL;
		paramLengths[param] = 0;
            }
	    else
	    {
		paramLengths[param] = newLength;
	    }
	}

	if (array_to_utf8(interp, paramValues, paramLengths, nParams, bufferPtr) != TCL_OK) {
		ckfree(paramValues);
		ckfree(paramLengths);
		return TCL_ERROR;
	}

	*paramValuesPtr = paramValues;

	return TCL_OK;
}

/**********************************
 * pg_exec
 send a query string to the backend connection

 syntax:
 pg_exec connection query [var1] [var2]...

 the return result is either an error message or a handle for a query
 result.  Handles start with the prefix "pgsql"
 **********************************/

int
Pg_exec(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	Pg_ConnectionId *connid;
	PGconn	        *conn;
	PGresult        *result;
	const char    *connString = NULL;
	const char      *execString = NULL;
	char            *newExecString = NULL;
	const char     **paramValues = NULL;
	char		*paramArrayName = NULL;
	const char      *paramsBuffer = NULL;
	int              nParams;
	int              index;
	int              useVariables = 0;

	enum             positionalArgs {EXEC_ARG_CONN, EXEC_ARG_SQL, EXEC_ARGS};
	int              nextPositionalArg = EXEC_ARG_CONN;
	struct stringStorage    storage;

	for(index = 1; index < objc && nextPositionalArg != EXEC_ARGS; index++) {
	    char *arg = Tcl_GetString(objv[index]);
	    if (arg[0] == '-' && arg[1] != '-') { // a legal SQL statement can start with "--"
		if(strcmp(arg, "-paramarray") == 0) {
		    index++;
		    paramArrayName = Tcl_GetString(objv[index]);
		} else if(strcmp(arg, "-variables") == 0) {
		    useVariables = 1;
		} else {
		    goto wrong_args;
		}
	    } else {
		switch(nextPositionalArg) {
		    case EXEC_ARG_CONN:
			connString = Tcl_GetString(objv[index]);
			nextPositionalArg = EXEC_ARG_SQL;
			break;
		    case EXEC_ARG_SQL:
			execString = Tcl_GetString(objv[index]);
			nextPositionalArg = EXEC_ARGS;
			break;
		}
	    }
	}

	if (nextPositionalArg != EXEC_ARGS)
	{
	    wrong_args:
		Tcl_WrongNumArgs(interp, 1, objv, "?-variables? ?-paramarray var? connection queryString ?parm...?");
		return TCL_ERROR;
	}

	/* figure out the connect string and get the connection ID */
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (connid->res_copyStatus != RES_COPY_NONE)
	{
	    Tcl_SetResult(interp, "Attempt to query while COPY in progress", TCL_STATIC);
	    return TCL_ERROR;
	}

        if (connid->callbackPtr || connid->callbackInterp)
        {
            Tcl_SetResult(interp, "Attempt to query while waiting for callback", TCL_STATIC);
	    return TCL_ERROR;
        }

	/* extra params will substitute for $1, $2, etc, in the statement */
	/* objc must be 3 or greater at this point */
	nParams = objc - index;

	if (useVariables) {
		if(paramArrayName || nParams) {
			Tcl_SetResult(interp, "-variables can not be used with positional or named parameters", TCL_STATIC);
			return TCL_ERROR;
		}
		if (handle_substitutions(interp, execString, &newExecString, &paramValues, &nParams, &paramsBuffer) != TCL_OK) {
			return TCL_ERROR;
		}
		if(nParams)
			execString = newExecString;
	} else if (paramArrayName) {
	    // Can't combine positional params and -paramarray
	    if (nParams) {
		Tcl_SetResult(interp, "Can't use both positional and named parameters", TCL_STATIC);
		return TCL_ERROR;
	    }
	    if (count_parameters(interp, execString, &nParams) == TCL_ERROR) {
		return TCL_ERROR;
	    }
	    if(nParams) {
		// After this point we must free newExecString and paramValues before exiting
		if (expand_parameters(interp, execString, nParams, paramArrayName, &newExecString, &paramValues, &paramsBuffer) == TCL_ERROR) {
		    return TCL_ERROR;
		}
		execString = newExecString;
	    }
	} else if (nParams) {
	    if (build_param_array(interp, nParams, &objv[index], &paramValues, &paramsBuffer) != TCL_OK) {
		return TCL_ERROR;
	    }
	    // After this point we must free paramValues and paramsBufferbefore exiting
        }

	initStorage(&storage);
	/* we could call PQexecParams when nParams is 0, but PQexecParams
	 * will not accept more than one SQL statement per call, while
	 * PQexec will.  by checking and using PQexec when no parameters
	 * are included, we maintain compatibility for code that doesn't
	 * use params and might have had multiple statements in a single
	 * request */
	if (nParams == 0) {
	    result = PQexec(conn, externalString(&storage, execString));
	} else {
	    result = PQexecParams(conn, externalString(&storage, execString), nParams, NULL, paramValues, NULL, NULL, 0);
	}
	freeStorage(&storage);

	if(paramValues) {
	    ckfree ((void *)paramValues);
	    paramValues = NULL;
	}
	if(newExecString) {
	    ckfree((void *)newExecString);
	    newExecString = NULL;
	}
	if(paramsBuffer) {
	    ckfree ((void *)paramsBuffer);
	    paramsBuffer = NULL;
	}

	connid->sql_count++;

	/* REPLICATED IN pg_exec_prepared -- NEEDS TO BE FACTORED */
	/* Transfer any notify events from libpq to Tcl event queue. */
	PgNotifyTransferEvents(connid);

	if (result)
	{
	    int	rId;
	    if(PgSetResultId(interp, connString, result, &rId) != TCL_OK) {
		PQclear(result);
		// Reconnect if the connection is bad.
		PgCheckConnectionState(connid);
		return TCL_ERROR;
	    }

	    ExecStatusType rStat = PQresultStatus(result);

	    if (rStat == PGRES_COPY_IN || rStat == PGRES_COPY_OUT)
	    {
		connid->res_copyStatus = RES_COPY_INPROGRESS;
		connid->res_copy = rId;
	    }
	    return TCL_OK;
	}
	else
	{
	    /* error occurred during the query */
	    report_connection_error(interp, conn);

	    // Reconnect if the connection is bad.
	    PgCheckConnectionState(connid);

	    return TCL_ERROR;
	}
}

/**********************************
 * report_connection_error
 Generate a proper Tcl errorCode and return error meaage from an error during a request
 */
static void report_connection_error(Tcl_Interp *interp, PGconn *conn)
{
	char *errString = "";

	if(conn) {
		errString = PQerrorMessage(conn);
	}

	if(errString[0] != '\0') {
		char *nl = strchr(errString, '\n');
		if(nl) *nl = '\0';
		Tcl_SetErrorCode(interp, "POSTGRESQL", "REQUEST_FAILED", errString, (char *)NULL);
		if(nl) *nl = '\n';
		Tcl_SetResult(interp, errString, TCL_VOLATILE);
	} else {
		Tcl_SetResult(interp, "Unknown error from Exec or SendQuery", TCL_STATIC);
	}
}

/**********************************
 * pg_exec_prepared
 send a request to executed a prepared statement with given parameters  
 to the backend connection

 syntax:
 pg_exec_prepared connection statement_name [var1] [var2]...

 the return result is either an error message or a handle for a query
 result.  Handles start with the prefix "pgp"
 **********************************/

int
Pg_exec_prepared(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	PGresult   *result;
	const char	   *connString;
	const char *statementNameString;
	const char **paramValues = NULL;
	const char *paramsBuffer = NULL;
	struct stringStorage storage;

	int         nParams;

	if (objc < 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection statementName [parm...]");
		return TCL_ERROR;
	}

	/* figure out the connect string and get the connection ID */

	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (connid->res_copyStatus != RES_COPY_NONE)
	{
		Tcl_SetResult(interp, "Attempt to query while COPY in progress", TCL_STATIC);
		return TCL_ERROR;
	}

        if (connid->callbackPtr || connid->callbackInterp)
        {
               Tcl_SetResult(interp, "Attempt to query while waiting for callback", TCL_STATIC);
               return TCL_ERROR;
         }

	/* extra params will substitute for $1, $2, etc, in the statement */
	/* objc must be 3 or greater at this point */
	nParams = objc - 3;

	if (nParams > 0) {
	    if (build_param_array(interp, nParams, &objv[3], &paramValues, &paramsBuffer) != TCL_OK) {
		return TCL_ERROR;
	    }
	    // After this point we must free paramValues and paramsBuffer before exiting
	}

	statementNameString = Tcl_GetString(objv[2]);

	initStorage(&storage);
	result = PQexecPrepared(conn, externalString(&storage, statementNameString), nParams, paramValues, NULL, NULL, 0);
	freeStorage(&storage);

	if (paramValues != (const char **)NULL) {
	    ckfree ((void *)paramValues);
	}

	if(paramsBuffer) {
		ckfree ((void *)paramsBuffer);
		paramsBuffer = NULL;
	}

	connid->sql_count++;

	/* REPLICATED IN pg_exec -- NEEDS TO BE FACTORED */
	/* Transfer any notify events from libpq to Tcl event queue. */
	PgNotifyTransferEvents(connid);

	if (result)
	{
		int	rId;
		if(PgSetResultId(interp, connString, result, &rId) != TCL_OK) {
			PQclear(result);
			return TCL_ERROR;
		}

		ExecStatusType rStat = PQresultStatus(result);

		if (rStat == PGRES_COPY_IN || rStat == PGRES_COPY_OUT)
		{
			connid->res_copyStatus = RES_COPY_INPROGRESS;
			connid->res_copy = rId;
		}
		return TCL_OK;
	}
	else
	{
		/* error occurred during the query */
		report_connection_error(interp, conn);

		// Reconnect if the connection is bad.
		PgCheckConnectionState(connid);

		return TCL_ERROR;
	}
}

/**********************************
 * Pg_result_foreach - iterate Tcl code over a result handle
 */

int
Pg_result_foreach(Tcl_Interp *interp, PGresult *result, Tcl_Obj *arrayNameObj, Tcl_Obj *code)
{
    int retval = TCL_OK;
    int tupno;
    int column;
    char *arrayName = Tcl_GetString (arrayNameObj);
    struct stringStorage storage;

    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
	    /* query failed, or it wasn't SELECT */
	    Tcl_SetResult(interp, (char *)PQresultErrorMessage(result),
				      TCL_VOLATILE);
	    return TCL_ERROR;
    }

    int ncols = PQnfields(result);

    initStorage(&storage);
    for (tupno = 0; tupno < PQntuples(result); tupno++)
    {
	    for (column = 0; column < ncols; column++)
	    {
		    char *columnName = PQfname (result, column);

		    if (PQgetisnull (result, tupno, column)) {
			Tcl_UnsetVar2 (interp, arrayName, columnName, 0);
			continue;
		    }

		    char *string = PQgetvalue (result, tupno, column);

		    if (Tcl_SetVar2(interp, arrayName, columnName, utfString(&storage, string), (TCL_LEAVE_ERR_MSG)) == NULL) 
		    {
			retval = TCL_ERROR;
			goto cleanup;
		    }
	    }

	    int r = Tcl_EvalObjEx(interp, code, 0);

	    if ((r != TCL_OK) && (r != TCL_CONTINUE))
	    {
		    if (r == TCL_BREAK)
			    break;			/* exit loop, but return TCL_OK */

		    if (r == TCL_ERROR)
		    {
			    char		msg[60];

			    sprintf(msg, "\n    (\"pg_result_foreach\" body line %d)",
					    Tcl_GetErrorLine(interp));
			    Tcl_AddErrorInfo(interp, msg);
		    }

		    retval = r;
		    break;
	    }
    }
cleanup:
    freeStorage(&storage);
    return retval;
}

/**********************************
 * pg_result
 get information about the results of a query

 syntax:

	pg_result result ?option?

 the options are:

	-status the status of the result

	-error	the error message, if the status indicates error; otherwise
		an empty string

	-conn	the connection that produced the result

	-oid	if command was an INSERT, the OID of the inserted tuple

	-numTuples	the number of tuples in the query

	-cmdTuples	Same as -numTuples, but for DELETE and UPDATE

	-numAttrs	returns the number of attributes returned by the query

	-assign arrayName
		assign the results to an array, using subscripts of the form
			(tupno,attributeName)

	-foreach arrayName code
		for each tuple assigns the results to the named array, using
		subscripts matching the column names, executing the code body.

	-assignbyidx arrayName ?appendstr?
		assign the results to an array using the first field's value
		as a key.
		All but the first field of each tuple are stored, using
		subscripts of the form (field0value,attributeNameappendstr)

	-getTuple tupleNumber
		returns the values of the tuple in a list

	-tupleArray tupleNumber arrayName
		stores the values of the tuple in array arrayName, indexed
		by the attributes returned.  If a value is null, sets an
		empty string or the default string into the array, if
		a default string has been defined.

	-tupleArrayWithoutNulls tupleNumber arrayName
		...stores the values of the tuple in array arrayName, indexed
		by the attributes returned.  If a value is null, unsets the
		field from the array.

	-attributes
		returns a list of the name/type pairs of the tuple attributes

	-lAttributes
		returns a list of the {name type len} entries of the tuple
		attributes

        -list
                returns one list of all of the data

        -llist  returns a list of lists, where each embedded list represents 
                a tuple in the result

	-clear	clear the result buffer. Do not reuse after this

	-null_value_string	Set the value returned for fields that are null
		                (defaults to connection setting, default "")

 **********************************/
int
Pg_result(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	PGresult   *result;
	int			i;
	int			tupno;
	Tcl_Obj    *arrVarObj;
	Tcl_Obj    *appendstrObj;
	char	   *queryResultString;
	int			optIndex;
	int			errorOptIndex;
	int        retval = TCL_OK;

	Tcl_Obj* listObj;
	Tcl_Obj* subListObj;
	Tcl_Obj* fieldObj = NULL;
	Tcl_Obj* tresult;

	struct stringStorage storage;

	Pg_resultid        *resultid;


	static const char *options[] = {
		"-status", "-error", "-foreach", "-conn", "-oid",
		"-numTuples", "-cmdTuples", "-numAttrs", "-assign", "-assignbyidx",
		"-getTuple", "-tupleArray", "-tupleArrayWithoutNulls", "-attributes", "-lAttributes",
		"-clear", "-list", "-llist", "-dict", "-null_value_string", (char *)NULL
	};

	enum options
	{
		OPT_STATUS, OPT_ERROR, OPT_FOREACH, OPT_CONN, OPT_OID,
		OPT_NUMTUPLES, OPT_CMDTUPLES, OPT_NUMATTRS, OPT_ASSIGN, OPT_ASSIGNBYIDX,
		OPT_GETTUPLE, OPT_TUPLEARRAY, OPT_TUPLEARRAY_WITHOUT_NULLS, OPT_ATTRIBUTES, OPT_LATTRIBUTES,
		OPT_CLEAR, OPT_LIST, OPT_LLIST, OPT_DICT, OPT_NULL_VALUE_STRING
	};

	static const char *errorOptions[] = {
		"severity", "sqlstate", "primary", "detail",
		"hint", "position", "internal_position", "internal_query",
		"context", "file", "line", "function", (char *)NULL
	};

	static CONST char pgDiagCodes[] = {
		PG_DIAG_SEVERITY,
		PG_DIAG_SQLSTATE, 
		PG_DIAG_MESSAGE_PRIMARY,
		PG_DIAG_MESSAGE_DETAIL, 
		PG_DIAG_MESSAGE_HINT, 
		PG_DIAG_STATEMENT_POSITION, 
		PG_DIAG_INTERNAL_POSITION,
		PG_DIAG_INTERNAL_QUERY,
		PG_DIAG_CONTEXT,
		PG_DIAG_SOURCE_FILE, 
		PG_DIAG_SOURCE_LINE, 
		PG_DIAG_SOURCE_FUNCTION
	};

	if (objc < 3 || objc > 5)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "");
		goto Pg_result_errReturn;		/* append help info */
	}

	/* figure out the query result handle and look it up */
	queryResultString = Tcl_GetString(objv[1]);
	result = PgGetResultId(interp, queryResultString, &resultid);
	if (result == (PGresult *)NULL)
	{
        tresult = Tcl_NewStringObj(queryResultString, -1);
        Tcl_AppendStringsToObj(tresult, " is not a valid query result", NULL);
        Tcl_SetObjResult(interp, tresult);

		return TCL_ERROR;
	}

	/* process command options */
	if (Tcl_GetIndexFromObj(interp, objv[2], options, "option", TCL_EXACT,
							&optIndex) != TCL_OK)
		return TCL_ERROR;

	switch ((enum options) optIndex)
	{
		case OPT_STATUS:
			{
				char	   *resultStatus;

				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				resultStatus = PQresStatus(PQresultStatus(result));
				Tcl_SetObjResult(interp, Tcl_NewStringObj(resultStatus, -1));
				// Reconnect if the connection is bad.
				if (strcmp(resultStatus, "PGRES_COMMAND_OK") != 0) {
					PgCheckConnectionState(resultid->connid);
				}
				return TCL_OK;
			}

		case OPT_ERROR:
			{
				if (objc < 3 || objc > 4)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "[subcode]");
					return TCL_ERROR;
				}

				/* if there's no subfield (objc == 3), just get the result
				 * error message */
				if (objc == 3) {
					Tcl_SetObjResult(interp,
						 Tcl_NewStringObj(PQresultErrorMessage(result), -1));
					return TCL_OK;
				}

				if (Tcl_GetIndexFromObj(interp, objv[3], errorOptions, 
				    "error suboption", TCL_EXACT, &errorOptIndex) != TCL_OK) {
					return TCL_ERROR;
				}

				Tcl_SetObjResult(interp, Tcl_NewStringObj(
                    PQresultErrorField(result,pgDiagCodes[errorOptIndex]),-1));

				return TCL_OK;
			}

		case OPT_FOREACH:
			{
			    if (objc != 5)
			    {
				    Tcl_WrongNumArgs(interp, 3, objv, "array code");
				    return TCL_ERROR;
			    }

			    int resultStatus =  Pg_result_foreach(interp, result, objv[3], objv[4]);
			    if(resultStatus != TCL_OK) {
				if(PgCheckConnectionState(resultid->connid) != TCL_OK) {
					report_connection_error(interp, resultid->connid->conn);
					return TCL_ERROR;
				}
			    }
			    return resultStatus;
			}

		case OPT_CONN:
			{
				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				return PgGetConnByResultId(interp, queryResultString);
			}

		case OPT_OID:
			{
				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				Tcl_SetObjResult(interp, Tcl_NewLongObj(PQoidValue(result)));
				return TCL_OK;
			}

		case OPT_CLEAR:
			{
				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				/* This will take care of the cleanup */
				Tcl_DeleteCommandFromToken(interp, resultid->cmd_token);
				return TCL_OK;
			}

		case OPT_NUMTUPLES:
			{
				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				Tcl_SetObjResult(interp, Tcl_NewIntObj(PQntuples(result)));
				return TCL_OK;
			}
		case OPT_CMDTUPLES:
			{
				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				Tcl_SetObjResult(interp, Tcl_NewStringObj(
					PQcmdTuples(result), -1));
				return TCL_OK;
			}

		case OPT_NUMATTRS:
			{
				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				Tcl_SetObjResult(interp, Tcl_NewIntObj(PQnfields(result)));
				return TCL_OK;
			}

		case OPT_ASSIGN:
			{
				if (objc != 4)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "arrayName");
					return TCL_ERROR;
				}

				arrVarObj = objv[3];

				initStorage(&storage);
				/*
				 * this assignment assigns the table of result tuples into
				 * a giant array with the name given in the argument. The
				 * indices of the array are of the form (tupno,attrName).
				 */
				for (tupno = 0; tupno < PQntuples(result); tupno++)
				{
					for (i = 0; i < PQnfields(result); i++)
					{
						Tcl_Obj    *fieldNameObj;

						/*
						 * construct the array element name consisting
						 * of the tuple number, a comma, and the field
						 * name.
						 * this is a little kludgey -- we set the obj
						 * to an int but the append following will force a
						 * string conversion.
						 */
						fieldNameObj = Tcl_NewObj ();
						Tcl_SetIntObj(fieldNameObj, tupno);
						Tcl_AppendToObj(fieldNameObj, ",", 1);
						Tcl_AppendToObj(fieldNameObj, PQfname(result, i), -1);


						if (Tcl_ObjSetVar2(interp, arrVarObj, fieldNameObj,
										   Tcl_NewStringObj(
											 utfString(&storage, PGgetvalue(result, resultid->nullValueString, tupno, i)),
											 -1), TCL_LEAVE_ERR_MSG) == NULL) {
							retval = TCL_ERROR;
							Tcl_DecrRefCount (fieldNameObj);
							goto cleanupStorage;
						}
					}
				}
				goto cleanupStorage;
			}

		case OPT_ASSIGNBYIDX:
			{
				if ((objc != 4) && (objc != 5))
				{
					Tcl_WrongNumArgs(interp, 3, objv, "arrayName ?append_string?");
					return TCL_ERROR;
				}

				arrVarObj = objv[3];

				if (objc == 5)
					appendstrObj = objv[4];
				else
					appendstrObj = NULL;

				initStorage(&storage);
				/*
				 * this assignment assigns the table of result tuples into
				 * a giant array with the name given in the argument.  The
				 * indices of the array are of the form
				 * (field0Value,attrNameappendstr). Here, we still assume
				 * PQfname won't exceed 200 characters, but we dare not
				 * make the same assumption about the data in field 0 nor
				 * the append string.
				 */
				for (tupno = 0; tupno < PQntuples(result); tupno++)
				{
					const char *field0 = utfString(&storage, PGgetvalue(result, resultid->nullValueString, tupno, 0));
					char *dupfield0 = ckalloc(strlen(field0)+1);

					strcpy(dupfield0, field0);

					for (i = 1; i < PQnfields(result); i++)
					{
						Tcl_Obj    *fieldNameObj;

						fieldNameObj = Tcl_NewObj ();
						Tcl_SetStringObj(fieldNameObj, dupfield0, -1);
						Tcl_AppendToObj(fieldNameObj, ",", 1);
						Tcl_AppendToObj(fieldNameObj, PQfname(result, i), -1);

						if (appendstrObj != NULL)
							Tcl_AppendObjToObj(fieldNameObj, appendstrObj);

						char *val = utfString(&storage, PGgetvalue(result, resultid->nullValueString, tupno, i));
						if (Tcl_ObjSetVar2(interp, arrVarObj, fieldNameObj,
						      Tcl_NewStringObj( val, -1), TCL_LEAVE_ERR_MSG) == NULL)
						{
							Tcl_DecrRefCount(fieldNameObj);
							ckfree(dupfield0);
							retval = TCL_ERROR;
							goto cleanupStorage;
						}
					}
					ckfree(dupfield0);
				}
				goto cleanupStorage;
			}

		case OPT_GETTUPLE:
			{
				Tcl_Obj    *resultObj;

				if (objc != 4)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "tuple_number");
					return TCL_ERROR;
				}

				if (Tcl_GetIntFromObj(interp, objv[3], &tupno) == TCL_ERROR)
					return TCL_ERROR;

				if (tupno < 0 || tupno >= PQntuples(result))
				{
					tresult = Tcl_NewStringObj("argument to getTuple cannot exceed ", -1);
					Tcl_AppendStringsToObj(tresult, "number of tuples - 1", NULL);
					Tcl_SetObjResult(interp, tresult);
					/*
					Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
						"argument to getTuple cannot exceed ",
						"number of tuples - 1", NULL);
					*/
					return TCL_ERROR;
				}

				/* set the result object to be an empty list */
				resultObj = Tcl_NewListObj(0, NULL);

				/* build up a return list, Tcl-object-style */
				for (i = 0; i < PQnfields(result); i++)
				{
					char	   *value;

					value = utfString(&storage, PGgetvalue(result, resultid->nullValueString, tupno, i));

					if (Tcl_ListObjAppendElement(interp, resultObj, 
							   Tcl_NewStringObj(value, -1)) == TCL_ERROR) {
						retval = TCL_ERROR;
						goto cleanupStorage;
					}
				}
				Tcl_SetObjResult(interp, resultObj);
				goto cleanupStorage;
			}

		case OPT_TUPLEARRAY:
		case OPT_TUPLEARRAY_WITHOUT_NULLS:
			{
				char	   *arrayName;

				if (objc != 5)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "tuple_number array_name");
					return TCL_ERROR;
				}

				if (Tcl_GetIntFromObj(interp, objv[3], &tupno) == TCL_ERROR)
					return TCL_ERROR;

				if (tupno < 0 || tupno >= PQntuples(result))
				{
					tresult = Tcl_NewStringObj("argument to tupleArray cannot exceed number of tuples - 1", -1);
					Tcl_SetObjResult(interp, tresult);
					return TCL_ERROR;
				}

				arrayName = Tcl_GetString(objv[4]);

				initStorage(&storage);
				if (optIndex == OPT_TUPLEARRAY)
				{
					/* it's the -array variant, if the field is null,
					 * set it in the array as the empty string or
					 * as the set null value string if one is set
					 */
					for (i = 0; i < PQnfields(result); i++)
					{
						if (Tcl_SetVar2(interp, arrayName, PQfname(result, i),
							utfString(&storage,
							    PGgetvalue(result, resultid->nullValueString, 
								tupno, i)
							), TCL_LEAVE_ERR_MSG) == NULL) {
							retval = TCL_ERROR;
							goto cleanupStorage;
						}
					}
				} else
				{
					/* it's the array_without_nulls variant,
					 * unset the field name from the array
					 * if it's null, else set it.
					 */
					for (i = 0; i < PQnfields(result); i++)
					{
						char *string;

						string = PQgetvalue (result, tupno, i);
						if (*string == '\0') {
							if (PQgetisnull (result, tupno, i)) {
							   Tcl_UnsetVar2 (interp, arrayName, PQfname(result, i), 0);
							   continue;
							}
						}

						if (Tcl_SetVar2(interp, arrayName, PQfname(result, i),
									 utfString(&storage, string),
										TCL_LEAVE_ERR_MSG) == NULL) {
							retval = TCL_ERROR;
							goto cleanupStorage;
						}
					}
				}
				goto cleanupStorage;
			}

		case OPT_ATTRIBUTES:
			{
				Tcl_Obj    *resultObj = Tcl_NewListObj(0, NULL);

				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				for (i = 0; i < PQnfields(result); i++)
				{
					Tcl_ListObjAppendElement(interp, resultObj,
							   Tcl_NewStringObj(PQfname(result, i), -1));
				}
				Tcl_SetObjResult(interp, resultObj);
				return TCL_OK;
			}

		case OPT_LATTRIBUTES:
			{
				Tcl_Obj    *resultObj = Tcl_NewListObj(0, NULL);

				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				for (i = 0; i < PQnfields(result); i++)
				{

					/* start a sublist */
					Tcl_Obj    *subList = Tcl_NewListObj(0, NULL);

					if (Tcl_ListObjAppendElement(interp, subList,
												 Tcl_NewStringObj(PQfname(result, i), -1)) == TCL_ERROR)
						return TCL_ERROR;

					if (Tcl_ListObjAppendElement(interp, subList,
												 Tcl_NewLongObj((long)PQftype(result, i))) == TCL_ERROR)
						return TCL_ERROR;

					if (Tcl_ListObjAppendElement(interp, subList,
												 Tcl_NewLongObj((long)PQfsize(result, i))) == TCL_ERROR)
						return TCL_ERROR;

					/* end the sublist, append to the result list */

					if (Tcl_ListObjAppendElement(interp, resultObj, subList)
						== TCL_ERROR)
						return TCL_ERROR;
				}
				Tcl_SetObjResult(interp, resultObj);
				return TCL_OK;
			}

		case OPT_LIST: 
		{
 	
			listObj = Tcl_NewListObj(0, (Tcl_Obj **) NULL);

			initStorage(&storage);
			/*
			**	Loop through the tuple, and append each 
			**	attribute to the list
			**
			**	This option appends all of the attributes
			**	for each tuple to the same list
			*/
			for (tupno = 0; tupno < PQntuples(result); tupno++)
			{

				/*
				**	Loop over the attributes for the tuple, 
				**	and append them to the list
				*/
				for (i = 0; i < PQnfields(result); i++)
				{
				    fieldObj = Tcl_NewObj();

				    Tcl_SetStringObj(fieldObj, utfString(&storage, PGgetvalue(result, resultid->nullValueString, tupno, i)), -1);
				    if (Tcl_ListObjAppendElement(interp, listObj, fieldObj) != TCL_OK)
					{
						Tcl_DecrRefCount(listObj);
						Tcl_DecrRefCount(fieldObj);
						retval = TCL_ERROR;
						goto cleanupStorage;
					}
	
				}
			}
	
			Tcl_SetObjResult(interp, listObj);
			
			goto cleanupStorage;
		}
		case OPT_LLIST: 
		{
			listObj = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
	
			initStorage(&storage);
			/*
			**	This is the top level list. This
			**	contains the other lists
			**
			**	This option contructs a list of
			**	attributes for each tuple, and
			**	appends that to the main list.
			**	This is a list of lists
			*/
			for (tupno = 0; tupno < PQntuples(result); tupno++)
			{
				subListObj = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
	
				/*
				**	This is the inner list. This contains
				**	the actual row values
				*/
				for (i = 0; i < PQnfields(result); i++)
				{
	
					fieldObj = Tcl_NewObj();

					Tcl_SetStringObj(fieldObj, utfString(&storage, PGgetvalue(result, resultid->nullValueString, tupno, i)), -1);
	
					if (Tcl_ListObjAppendElement(interp, subListObj, fieldObj) != TCL_OK)
					{
						Tcl_DecrRefCount(listObj);
						Tcl_DecrRefCount(fieldObj);
						retval = TCL_ERROR;
						goto cleanupStorage;
					}
	
				}
				if (Tcl_ListObjAppendElement(interp, listObj, subListObj) != TCL_OK)
				{
					Tcl_DecrRefCount(listObj);
					Tcl_DecrRefCount(fieldObj);
					retval = TCL_ERROR;
					goto cleanupStorage;
				}
			}
	
			Tcl_SetObjResult(interp, listObj);
		
			goto cleanupStorage;
		}

		case OPT_DICT: 
                {
			listObj = Tcl_NewDictObj();
	
			initStorage(&storage);
			/*
			**	This is the top level list. This
			**	contains the other lists
			**
			**	This option contructs a list of
			**	attributes for each tuple, and
			**	appends that to the main list.
			**	This is a list of lists
			*/
			for (tupno = 0; tupno < PQntuples(result); tupno++)
			{
				subListObj = Tcl_NewDictObj();
	
				/*
				**	This is the inner list. This contains
				**	the actual row values
				*/
				for (i = 0; i < PQnfields(result); i++)
				{
					Tcl_Obj    *fieldNameObj;
	
					fieldObj = Tcl_NewObj();
					fieldNameObj = Tcl_NewObj();

					Tcl_SetStringObj(fieldNameObj, PQfname(result, i), -1);
					Tcl_SetStringObj(fieldObj, utfString(&storage, PGgetvalue(result, resultid->nullValueString, tupno, i)), -1);
					if (Tcl_DictObjPut(interp, subListObj, fieldNameObj, fieldObj) != TCL_OK)
					{
						Tcl_DecrRefCount(listObj);
						Tcl_DecrRefCount(fieldObj);
						retval = TCL_ERROR;
						goto cleanupStorage;
					}
	
				}
				if (Tcl_DictObjPut(interp, listObj, Tcl_NewIntObj(tupno), subListObj) != TCL_OK)
				{
					Tcl_DecrRefCount(listObj);
					Tcl_DecrRefCount(fieldObj);
					retval = TCL_ERROR;
					goto cleanupStorage;
				}
			}
	
			Tcl_SetObjResult(interp, listObj);
			goto cleanupStorage;
                }

		case OPT_NULL_VALUE_STRING:
			{
				char       *nullValueString;
				int         length;

				if ((objc < 3) || (objc > 4))
				{
					Tcl_WrongNumArgs(interp, 3, objv, "?nullValueString?");
					return TCL_ERROR;
				}

				if (objc == 3)
				{
					if (resultid->nullValueString == NULL || 
					    *resultid->nullValueString == '\0') {
						Tcl_SetObjResult(interp, Tcl_NewStringObj("", 0));
					} else {
						Tcl_SetObjResult(interp, 
							Tcl_NewStringObj(resultid->nullValueString, -1));
					}
					return TCL_OK;
				}

				/* objc == 4, they're setting it */
				if (resultid->nullValueString != NULL) {
					if (resultid->connid->nullValueString != resultid->nullValueString)
					ckfree((void *)resultid->nullValueString);
				}

				nullValueString = Tcl_GetStringFromObj (objv[3], &length);
				resultid->nullValueString = ckalloc (length + 1);
				strcpy (resultid->nullValueString, nullValueString);

				Tcl_SetObjResult(interp, objv[3]);
				return TCL_OK;
			}

		default:
			{
				Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid option\n", -1));
				goto Pg_result_errReturn;		/* append help info */
			}
	}

Pg_result_errReturn:

	tresult = Tcl_NewStringObj("pg_result result ?option? where option is\n", -1);
	Tcl_AppendStringsToObj(tresult, "\t-status\n",
					 "\t-error ?subCode?\n",
					 "\t-foreach array code\n",
					 "\t-conn\n",
					 "\t-oid\n",
					 "\t-numTuples\n",
					 "\t-cmdTuples\n",
					 "\t-numAttrs\n"
					 "\t-assign arrayVarName\n",
					 "\t-assignbyidx arrayVarName ?appendstr?\n",
					 "\t-getTuple tupleNumber\n",
					 "\t-tupleArray tupleNumber arrayVarName\n",
					 "\t-attributes\n"
					 "\t-lAttributes\n"
					 "\t-list\n",
					 "\t-llist\n",
					 "\t-clear\n",
					 "\t-dict\n",
					 "\t-null_value_string ?nullValueString?\n",
					 (char *)NULL);
        Tcl_SetObjResult(interp, tresult);
	return TCL_ERROR;

cleanupStorage:
	freeStorage(&storage);
	return retval;
}

/**********************************
 * pg_execute
 send a query string to the backend connection and process the result

 syntax:
 pg_execute ?-array name? ?-oid varname? connection query ?loop_body?

 the return result is the number of tuples processed. If the query
 returns tuples (i.e. a SELECT statement), the result is placed into
 variables
 **********************************/

int
Pg_execute(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	PGresult   *result;
	int			i;
	int			tupno;
	int			ntup;
	int			loop_rc;
	const char	   *array_varname = NULL;
	char	   *arg;
	struct stringStorage storage;

	Tcl_Obj    *oid_varnameObj = NULL;
	Tcl_Obj    *evalObj;
	Tcl_Obj    *resultObj;

	char	   *usage = "?-array arrayname? ?-oid varname? "
	"connection queryString ?loop_body?";

	/*
	 * First we parse the options
	 */
	i = 1;
	while (i < objc)
	{
		arg = Tcl_GetString(objv[i]);
		if (arg[0] != '-' || arg[1] == '-')
                {
		    break;
                }

		if (strcmp(arg, "-array") == 0)
		{
			/*
			 * The rows should appear in an array vs. to single variables
			 */
			i++;
			if (i == objc)
			{
				Tcl_WrongNumArgs(interp, 1, objv, usage);
				return TCL_ERROR;
			}

			array_varname = Tcl_GetString(objv[i++]);
			continue;
		}

		if (strcmp(arg, "-oid") == 0)
		{
			/*
			 * We should place PQoidValue() somewhere
			 */
			i++;
			if (i == objc)
			{
				Tcl_WrongNumArgs(interp, 1, objv, usage);
				return TCL_ERROR;
			}
			oid_varnameObj = objv[i++];
			continue;
		}

		Tcl_WrongNumArgs(interp, 1, objv, usage);
		return TCL_ERROR;
	}

	/*
	 * Check that after option parsing at least 'connection' and 'query'
	 * are left
	 */
	if (objc - i < 2)
	{
		Tcl_WrongNumArgs(interp, 1, objv, usage);
		return TCL_ERROR;
	}

	/*
	 * Get the connection and make sure no COPY command is pending
	 */
	conn = PgGetConnectionId(interp, Tcl_GetString(objv[i++]), &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (connid->res_copyStatus != RES_COPY_NONE)
	{
            Tcl_SetObjResult(interp, 
              Tcl_NewStringObj("Attempt to query while COPY in progress", -1));

	    return TCL_ERROR;
	}

        if (connid->callbackPtr || connid->callbackInterp)
        {
               Tcl_SetResult(interp, "Attempt to query while waiting for callback", TCL_STATIC);
               return TCL_ERROR;
        }


	/*
	 * Execute the query
	 */
	initStorage(&storage);
	result = PQexec(conn, externalString(&storage, Tcl_GetString(objv[i++])));
	freeStorage(&storage);
	connid->sql_count++;

	/*
	 * Transfer any notify events from libpq to Tcl event queue.
	 */
	PgNotifyTransferEvents(connid);

	/*
	 * Check for errors
	 */
	if (result == NULL)
	{
		report_connection_error(interp, conn);

		// Look for a failed connection and re-open it.
		PgCheckConnectionState(connid);

		return TCL_ERROR;
	}

	/*
	 * Set the oid variable to the returned oid of an INSERT statement if
	 * requested (or 0 if it wasn't an INSERT)
	 */
	if (oid_varnameObj != NULL)
	{
		if (Tcl_ObjSetVar2(interp, oid_varnameObj, NULL,
						   Tcl_NewLongObj((long)PQoidValue(result)), TCL_LEAVE_ERR_MSG) == NULL)
		{
			PQclear(result);
			return TCL_ERROR;
		}
	}

	/*
	 * Decide how to go on based on the result status
	 */
	switch (PQresultStatus(result))
	{
		case PGRES_TUPLES_OK:
			/* fall through if we have tuples */
			break;

		case PGRES_EMPTY_QUERY:
		case PGRES_COMMAND_OK:
		case PGRES_COPY_IN:
		case PGRES_COPY_OUT:
		/* tell the number of affected tuples for non-SELECT queries */
                    Tcl_SetObjResult(interp,
                        Tcl_NewStringObj(PQcmdTuples(result), -1));
                    PQclear(result);
                    return TCL_OK;

		default:
			/* anything else must be an error */
			/* set the result object to be an empty list */
			resultObj = Tcl_NewListObj(0, NULL);
			if (Tcl_ListObjAppendElement(interp, resultObj,
			   Tcl_NewStringObj(PQresStatus(PQresultStatus(result)), -1))
				== TCL_ERROR)
				return TCL_ERROR;

			if (Tcl_ListObjAppendElement(interp, resultObj,
					  Tcl_NewStringObj(PQresultErrorMessage(result), -1))
				== TCL_ERROR)
				return TCL_ERROR;

			Tcl_SetObjResult(interp, resultObj);
			PQclear(result);
			return TCL_ERROR;
	}

	/*
	 * We reach here only for queries that returned tuples
	 */
	if (i == objc)
	{
		/*
		 * We don't have a loop body. If we have at least one result row,
		 * we set all the variables to the first one and return.
		 */
		if (PQntuples(result) > 0)
		{
			if (execute_put_values(interp, array_varname, result, connid->nullValueString, 0) != TCL_OK)
			{
				PQclear(result);
				return TCL_ERROR;
			}
		}

		Tcl_SetObjResult(interp, Tcl_NewIntObj(PQntuples(result)));
		PQclear(result);
		return TCL_OK;
	}

	/*
	 * We have a loop body. For each row in the result set, put the values
	 * into the Tcl variables and execute the body.
	 */
	ntup = PQntuples(result);
	evalObj = objv[i];
	for (tupno = 0; tupno < ntup; tupno++)
	{
		if (execute_put_values(interp, array_varname, result, connid->nullValueString, tupno) != TCL_OK)
		{
			PQclear(result);
			return TCL_ERROR;
		}

		loop_rc = Tcl_EvalObjEx(interp, evalObj, 0);

		/* The returncode of the loop body controls the loop execution */
		if (loop_rc == TCL_OK || loop_rc == TCL_CONTINUE)
		{
			/* OK or CONTINUE means start next loop invocation */
			continue;
		}

		if (loop_rc == TCL_RETURN)
		{
			/* RETURN means hand up the given interpreter result */
			PQclear(result);
			return TCL_RETURN;
		}

		if (loop_rc == TCL_BREAK)
		{
			/* BREAK means leave the loop */
			break;
		}

		PQclear(result);
		return TCL_ERROR;
	}

	/*
	 * At the end of the loop we put the number of rows we got into the
	 * interpreter result and clear the result set.
	 */
	Tcl_SetObjResult(interp, Tcl_NewIntObj(ntup));
	PQclear(result);
	return TCL_OK;
}


/**********************************
 * execute_put_values

 Put the values of one tuple into Tcl variables named like the
 column names, or into an array indexed by the column names.
 **********************************/
static int
execute_put_values(Tcl_Interp *interp, const char *array_varname,
				   PGresult *result, char *nullValueString, int tupno)
{
	int	    i;
	int	    n;
	char	   *fname;
	char	   *value;
	struct stringStorage storage;
	int         retval = TCL_OK;

	/*
	 * For each column get the column name and value and put it into a Tcl
	 * variable (either scalar or array item)
	 */
	initStorage(&storage);
	n = PQnfields(result);
	for (i = 0; i < n; i++)
	{
		fname = PQfname(result, i);
		value = utfString(&storage, PGgetvalue(result, nullValueString, tupno, i));

		if (array_varname != NULL)
		{
			if (Tcl_SetVar2(interp, array_varname, fname, value,
							TCL_LEAVE_ERR_MSG) == NULL) {
				retval = TCL_ERROR;
				goto cleanup;
			}
		}
		else
		{
			if (Tcl_SetVar(interp, fname, value, TCL_LEAVE_ERR_MSG) == NULL) {
				retval = TCL_ERROR;
				goto cleanup;
			}
		}
	}
cleanup:
	freeStorage(&storage);
	return retval;
}

/**********************************
 * pg_lo_open
	 open a large object

 syntax:
 pg_lo_open conn objOid mode

 where mode can be either 'r', 'w', or 'rw'
**********************/

int
Pg_lo_open(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	PGconn	   *conn;
	int			lobjId;
	int			mode;
	int			fd;
	char	   *connString;
	char	   *modeString;
	int			modeStringLen;
	Pg_ConnectionId *connid;

	if (objc != 4)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection lobjOid mode");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString,  &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &lobjId) == TCL_ERROR)
		return TCL_ERROR;

	modeString = Tcl_GetStringFromObj(objv[3], &modeStringLen);
	if ((modeStringLen < 1) || (modeStringLen > 2))
	{

        Tcl_SetObjResult(interp, 
            Tcl_NewStringObj("mode argument must be 'r', 'w', or 'rw'", -1));

		return TCL_ERROR;
	}

	switch (modeString[0])
	{
		case 'r':
		case 'R':
			mode = INV_READ;
			break;
		case 'w':
		case 'W':
			mode = INV_WRITE;
			break;
		default:
            Tcl_SetObjResult(interp, 
             Tcl_NewStringObj("mode argument must be 'r', 'w', or 'rw'", -1));
			return TCL_ERROR;
	}

	switch (modeString[1])
	{
		case '\0':
			break;
		case 'r':
		case 'R':
			mode |= INV_READ;
			break;
		case 'w':
		case 'W':
			mode |= INV_WRITE;
			break;
		default:
            Tcl_SetObjResult(interp, 
             Tcl_NewStringObj("mode argument must be 'r', 'w', or 'rw'", -1));
			return TCL_ERROR;
	}

	fd = lo_open(conn, lobjId, mode);

        // Reconnect if the connection is bad.
        if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, conn);
		return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(fd));
	return TCL_OK;
}

/**********************************
 * pg_lo_close
	 close a large object

 syntax:
 pg_lo_close conn fd

**********************/
int
Pg_lo_close(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	PGconn	   *conn;
	int			fd;
	char	   *connString;
	Pg_ConnectionId *connid;

	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection fd");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &fd) != TCL_OK)
		return TCL_ERROR;

	int result = lo_close(conn, fd);

        // Reconnect if the connection is bad.
        if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, conn);
		return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(result));
	return TCL_OK;
}

/**********************************
 * pg_lo_read
	 reads at most len bytes from a large object into a variable named
 bufVar

 syntax:
 pg_lo_read conn fd bufVar len

 bufVar is the name of a variable in which to store the contents of the read

**********************/
int
Pg_lo_read(ClientData cData, Tcl_Interp *interp, int objc,
		   Tcl_Obj *CONST objv[])
{
	PGconn	   *conn;
	int			fd;
	int			nbytes = 0;
	char	   *buf;
	Tcl_Obj    *bufVar;
	Tcl_Obj    *bufObj;
	int			len;
	int			rc = TCL_OK;
	Pg_ConnectionId *connid;

	if (objc != 5)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn fd bufVar len");
		return TCL_ERROR;
	}

	conn = PgGetConnectionId(interp, Tcl_GetString(objv[1]),
							 &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &fd) != TCL_OK)
		return TCL_ERROR;

	bufVar = objv[3];

	if (Tcl_GetIntFromObj(interp, objv[4], &len) != TCL_OK)
		return TCL_ERROR;

	if (len <= 0)
	{
		Tcl_SetObjResult(interp, Tcl_NewIntObj(nbytes));
		return TCL_OK;
	}

	buf = ckalloc(len + 1);

	nbytes = lo_read(conn, fd, buf, len);

        // Reconnect if the connection is bad.
        if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, conn);
		rc = TCL_ERROR;
	}
	else if (nbytes >= 0)
        {
	        bufObj = Tcl_NewByteArrayObj((unsigned char*)buf, nbytes);

	    if (Tcl_ObjSetVar2(interp, bufVar, NULL, bufObj,
					   TCL_LEAVE_ERR_MSG | TCL_PARSE_PART1) == NULL)
		rc = TCL_ERROR;
        }
   
        if (rc == TCL_OK)
		Tcl_SetObjResult(interp, Tcl_NewIntObj(nbytes));

	ckfree((void *)buf);
	return rc;
}

/***********************************
Pg_lo_write
   write at most len bytes to a large object

 syntax:
 pg_lo_write conn fd buf len

***********************************/
int
Pg_lo_write(ClientData cData, Tcl_Interp *interp, int objc,
			Tcl_Obj *CONST objv[])
{
	PGconn	   *conn;
	char	   *buf;
	int			fd;
	int			nbytes = 0;
	int			len;
	Pg_ConnectionId *connid;

	if (objc != 5)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn fd buf len");
		return TCL_ERROR;
	}

	conn = PgGetConnectionId(interp, Tcl_GetString(objv[1]),
							 &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &fd) != TCL_OK)
		return TCL_ERROR;

	buf = (char*)Tcl_GetByteArrayFromObj(objv[3], &nbytes);

	if (Tcl_GetIntFromObj(interp, objv[4], &len) != TCL_OK)
		return TCL_ERROR;

	if (len > nbytes)
		len = nbytes;

	if (len <= 0)
	{
		Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
		return TCL_OK;
	}

	nbytes = lo_write(conn, fd, (char*)buf, len);

        // Reconnect if the connection is bad.
        if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, conn);
		return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(nbytes));
	return TCL_OK;
}

/***********************************
Pg_lo_lseek
	seek to a certain position in a large object

syntax
  pg_lo_lseek conn fd offset whence

whence can be either
"SEEK_CUR", "SEEK_END", or "SEEK_SET"
***********************************/
int
Pg_lo_lseek(ClientData cData, Tcl_Interp *interp, int objc,
			Tcl_Obj *CONST objv[])
{
	PGconn	   *conn;
	int			fd;
	char	   *whenceStr;
	int			offset;
	int			whence;
	char	   *connString;
	Pg_ConnectionId *connid;
        Tcl_Obj    *tresult;

	if (objc != 5)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn fd offset whence");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &fd) != TCL_OK)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[3], &offset) == TCL_ERROR)
		return TCL_ERROR;

	whenceStr = Tcl_GetString(objv[4]);

	if (strcmp(whenceStr, "SEEK_SET") == 0)
		whence = SEEK_SET;
	else if (strcmp(whenceStr, "SEEK_CUR") == 0)
		whence = SEEK_CUR;
	else if (strcmp(whenceStr, "SEEK_END") == 0)
		whence = SEEK_END;
	else
	{
            tresult = Tcl_NewStringObj("'whence' must be SEEK_SET, SEEK_CUR or SEEK_END", -1);
            Tcl_SetObjResult(interp, tresult);

            return TCL_ERROR;
	}

	int result = lo_lseek(conn, fd, offset, whence);

        // Reconnect if the connection is bad.
        if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, conn);
		return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(result));
	return TCL_OK;
}


/***********************************
Pg_lo_creat
   create a new large object with mode

 syntax:
   pg_lo_creat conn mode

mode can be any OR'ing together of INV_READ, INV_WRITE,
for now, we don't support any additional storage managers.

***********************************/
int
Pg_lo_creat(ClientData cData, Tcl_Interp *interp, int objc,
			Tcl_Obj *CONST objv[])
{
	PGconn	   *conn;
	char	   *modeStr;
	char	   *modeWord;
	int			mode;
	char	   *connString;
        Tcl_Obj    *tresult;
	Pg_ConnectionId *connid;

	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn mode");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	modeStr = Tcl_GetString(objv[2]);

	modeWord = strtok(modeStr, "|");
	if (strcmp(modeWord, "INV_READ") == 0)
		mode = INV_READ;
	else if (strcmp(modeWord, "INV_WRITE") == 0)
		mode = INV_WRITE;
	else
	{
            tresult = Tcl_NewStringObj("mode must be some OR'd combination of INV_READ, and INV_WRITE", -1);
            Tcl_SetObjResult(interp, tresult);

	    return TCL_ERROR;
	}

	while ((modeWord = strtok(NULL, "|")) != NULL)
	{
		if (strcmp(modeWord, "INV_READ") == 0)
			mode |= INV_READ;
		else if (strcmp(modeWord, "INV_WRITE") == 0)
			mode |= INV_WRITE;
		else
		{
                    tresult = Tcl_NewStringObj("mode must be some OR'd combination of INV_READ, and INV_WRITE", -1);
                    Tcl_SetObjResult(interp, tresult);

			return TCL_ERROR;
		}
	}

	int result = lo_creat(conn, mode);

        // Reconnect if the connection is bad.
        if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, conn);
		return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(result));
	return TCL_OK;
}

/***********************************
Pg_lo_tell
	returns the current seek location of the large object

 syntax:
   pg_lo_tell conn fd

***********************************/
int
Pg_lo_tell(ClientData cData, Tcl_Interp *interp, int objc,
		   Tcl_Obj *CONST objv[])
{
	PGconn	   *conn;
	int			fd;
	char	   *connString;
	Pg_ConnectionId *connid;

	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn fd");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &fd) != TCL_OK)
		return TCL_ERROR;

	int result = lo_tell(conn, fd);

        // Reconnect if the connection is bad.
        if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, conn);
		return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(result));
	return TCL_OK;
}

/***********************************
Pg_lo_truncate
	truncates a large object to the given length.  If length is greater
	than the current large object length, the large object is extended
	with null bytes.

 syntax:
   pg_lo_truncate conn fd len

***********************************/
int
Pg_lo_truncate(ClientData cData, Tcl_Interp *interp, int objc,
		   Tcl_Obj *CONST objv[])
{
	PGconn	   *conn;
	int			fd;
	int			len = 0;
	char	   *connString;
	Pg_ConnectionId *connid;

	if ((objc < 3) || (objc > 4))
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn fd ?len?");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &fd) != TCL_OK)
		return TCL_ERROR;

	if (objc == 4) {
		if (Tcl_GetIntFromObj(interp, objv[3], &len) != TCL_OK)
			return TCL_ERROR;
	}

	int result = lo_truncate(conn, fd, len);

        // Reconnect if the connection is bad.
        if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, conn);
		return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(result));

	return TCL_OK;
}

/***********************************
Pg_lo_unlink
	unlink a file based on lobject id

 syntax:
   pg_lo_unlink conn lobjId


***********************************/
int
Pg_lo_unlink(ClientData cData, Tcl_Interp *interp, int objc,
			 Tcl_Obj *CONST objv[])
{
	PGconn	   *conn;
	int			lobjId;
	int			retval;
	char	   *connString;
        Tcl_Obj    *tresult;
	Pg_ConnectionId *connid;

	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn fd");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &lobjId) == TCL_ERROR)
		return TCL_ERROR;

	retval = lo_unlink(conn, lobjId);
	if (retval == -1)
	{
            tresult = Tcl_NewStringObj("unlink of '", -1);
            Tcl_AppendStringsToObj(tresult, lobjId, NULL);
            Tcl_AppendStringsToObj(tresult, "' failed", NULL);
            Tcl_SetObjResult(interp, tresult);

	    // Reconnect if the connection is bad.
	    PgCheckConnectionState(connid);

            return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(retval));
	return TCL_OK;
}

/***********************************
Pg_lo_import
	import a Unix file into an (inversion) large objct
 returns the oid of that object upon success
 returns InvalidOid upon failure

 syntax:
   pg_lo_import conn filename

***********************************/

int
Pg_lo_import(ClientData cData, Tcl_Interp *interp, int objc,
			 Tcl_Obj *CONST objv[])
{
	PGconn	   *conn;
	const char	   *filename;
	Oid			lobjId;
	char	   *connString;
        Tcl_Obj    *tresult;
	Pg_ConnectionId *connid;

	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn filename");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	filename = Tcl_GetString(objv[2]);

	lobjId = lo_import(conn, filename);
	if (lobjId == InvalidOid)
	{
            tresult = Tcl_NewStringObj("import of '", -1);
            Tcl_AppendStringsToObj(tresult, filename, NULL);
            Tcl_AppendStringsToObj(tresult, "' failed", NULL);
            Tcl_SetObjResult(interp, tresult);

	    // Reconnect if the connection is bad.
	    PgCheckConnectionState(connid);

            return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewLongObj((long)lobjId));
	return TCL_OK;
}

/***********************************
Pg_lo_export
	export an Inversion large object to a Unix file

 syntax:
   pg_lo_export conn lobjId filename

***********************************/

int
Pg_lo_export(ClientData cData, Tcl_Interp *interp, int objc,
			 Tcl_Obj *CONST objv[])
{
	PGconn	   *conn;
	const char	   *filename;
	Oid			lobjId;
	int			retval;
	char	   *connString;
        Tcl_Obj    *tresult;
	Pg_ConnectionId *connid;

	if (objc != 4)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn lobjId filename");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], (int *)&lobjId) == TCL_ERROR)
		return TCL_ERROR;

	filename = Tcl_GetString(objv[3]);

	retval = lo_export(conn, lobjId, filename);
	if (retval == -1)
	{
            tresult = Tcl_NewStringObj("export failed", -1);
            Tcl_SetObjResult(interp, tresult);

	    // Reconnect if the connection is bad.
	    if(PgCheckConnectionState(connid) != TCL_OK)
		report_connection_error(interp, conn);

            return TCL_ERROR;
	}
	return TCL_OK;
}

/*********************************
 * Helper functions for Pg_select()
 *********************************/

/* count_parameters

 Returns TCL_OK or TCL_ERROR

 If successful, sets nParams to the number of expected parameters in queryString
 */
static int count_parameters(Tcl_Interp *interp, const char *queryString, int *nParamsPtr)
{

    int nQuotes = 0;
    int nParams;
    const char *cursor = queryString;
    while(*cursor) {
	if(*cursor == '`') {
	    nQuotes++;
	}
	cursor++;
    }
    if (nQuotes & 1) {
	Tcl_SetResult(interp, "Unmatched substitution back-quotes in SQL query", TCL_STATIC);
	return TCL_ERROR;
    }
    nParams = nQuotes / 2;
    if(nParams >= 100000) {
	Tcl_SetResult(interp, "Too many parameter substitutions requested (max 100,000)", TCL_STATIC);
	return TCL_ERROR;
    }
    *nParamsPtr = nParams;
    return TCL_OK;
}

/* expand_paremeters

 Returns TCL_OK or TCL_ERROR

 If successful, allocates newQueryString and paramValues array.

 If not, does not modify the arguments.
 */
static int expand_parameters(Tcl_Interp *interp, const char *queryString, int nParams, char *paramArrayName,
				char **newQueryStringPtr, const char ***paramValuesPtr, const char **bufferPtr)
{
	// Allocating space for parameter IDs up to 100,000 (5 characters)
	char        *newQueryString = (char *)ckalloc(strlen(queryString) + 5 * nParams);
	const char **paramValues    = (const char **)ckalloc(nParams * sizeof (*paramValues));
	int         *paramLengths   = (int *)ckalloc(nParams * sizeof (int));
	const char   *input         = queryString;
	char         *output        = newQueryString;
	int           paramIndex    = 0;

	while(*input) {
	    if(*input == '`') {
		// Step over quote
		++input;

		// Defense, make sure we're not about to stomp over the end of the array
		assert(paramIndex < nParams);

		// base and length for name string
		const char *nameMarker = input;
		int paramNameLength = 0;

		// Step over name, making sure it's legit
		while(*input && *input != '`') {
		    if (!isalnum(*input) && *input != '_') {
			Tcl_SetResult(interp, "Invalid name between back-quotes", TCL_STATIC);
			goto error_return;
		    }
		    input++;
		    paramNameLength++;
		}

		// More likely a mistake than some bizarre attempt to use the null name in the param array.
		if(paramNameLength == 0) {
		    Tcl_SetResult(interp, "Parameter name must not be empty", TCL_STATIC);
		    goto error_return;
		}

		// Should never happen because we already know the back-quotes are paired
		// but check anyway
		assert(*input != 0);

		// Copy name out so we can null terminate it
		char *paramName = ckalloc(paramNameLength+1);
		strncpy(paramName, nameMarker, paramNameLength);
		paramName[paramNameLength] = 0;

		// Get name from array. Ignore errors. Maybe we want to trap some errors?
		// Think about that later.
		Tcl_Obj *paramValueObj = Tcl_GetVar2Ex(interp, paramArrayName, paramName, 0);

		// This has done its work, ditch it;
		ckfree((void *)paramName);
		paramName = NULL;

		// If the name is not present in the parameter array, then treat it as a NULL
		// in the SQL sense, represented by a literal NULL in the parameter list
		if(paramValueObj) {
		    paramValues[paramIndex] = Tcl_GetStringFromObj(paramValueObj, &paramLengths[paramIndex]);
		} else {
		    paramValues[paramIndex] = NULL;
		    paramLengths[paramIndex] = 0;
		}

		// First param (paramValues[0]) is $1, etc...
		sprintf(output, "$%d", paramIndex + 1);
		output += strlen(output);

		// step into next parameter
		paramIndex++;

		// step over closing back-quote
		input++;
	    } else {
		// Literally copy everything outside `name`
		*output = *input;
		output++;
		input++;
	    }
	}

	// Null terminate that puppy
	*output = 0;

	// If this triggers then something is very wrong with the logic above.
	assert(paramIndex == nParams);

	if (array_to_utf8(interp, paramValues, paramLengths, nParams, bufferPtr) != TCL_OK) {
		goto error_return;
	}

	// Normal return, push parameters and return OK.
	*paramValuesPtr = paramValues;
	*newQueryStringPtr = newQueryString;
	return TCL_OK;

error_return:
	// Something went wrong, clean up and return ERROR.
	if(paramValues) ckfree((void *)paramValues);
	if(paramLengths) ckfree((void *)paramLengths);
	if(newQueryString) ckfree((void *)newQueryString);
	return TCL_ERROR;
}

/**********************************
 * pg_select
 send a select query string to the backend connection

 syntax:
 pg_select ?-nodotfields? ?-withoutnulls? ?-variables? ?-paramarray var? ?-count var? ?-params list? connection query var proc

 The query must be a select statement

 The var is used in the proc as an array

 The proc is run once for each row found

 .headers, .numcols and .tupno are not set if -nodotfields is specified

 null variables are set as empty strings unless -withoutnulls is specified,
 in which case null variables are made to simply be absent from the
 array

 If -params is provided, then it is a list of parameters that will replace "$1" "$2" and so on in
 the query. Don't forget to escape the "$" signs or {brace-enclose} the query. :)

 If -paramarray is provided, then occurrences of `name` will be replaced (via PQexecParams)
 with the corresponding value from paramArray. If the array doesn't contain the name
 then NULL will be replaced instead.

  * `name` must occur in a location where a value or field name could occur.

  * There are a maximum of 99,999 substitutions.

  * The name must contain only alphanumercis and underscores.

 Originally I was also going to update changes but that has turned out
 to be not so simple.  Instead, the caller should get the OID of any
 table they want to update and update it themself in the loop.	I may
 try to write a simplified table lookup and update function to make
 that task a little easier.

 The return is either TCL_OK, TCL_ERROR or TCL_RETURN and interp->result
 may contain more information.
 **********************************/

int
Pg_select(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	Pg_ConnectionId *connid;
	PGconn	    *conn;
	PGresult    *result;
	int          r,
	             retval = TCL_ERROR;
	int          tupno,
	             column,
	             ncols = 0;
	int          withoutNulls = 0;
	int          noDotFields = 0;
	int          rowByRow = 0;
	int          firstPass = 1;
	int          index = 1;
	int          nParams = 0;
	char        *connString     = NULL;
	const char  *queryString    = NULL;
	char        *varNameString  = NULL;
	char        *paramArrayName = NULL;
	const char  *paramsBuffer   = NULL;
	Tcl_Obj     *varNameObj     = NULL;
	Tcl_Obj     *procStringObj  = NULL;
	Tcl_Obj     *columnListObj  = NULL;
	Tcl_Obj    **columnNameObjs = NULL;
	const char **paramValues    = NULL;
	char        *newQueryString = NULL;
	Tcl_Obj     *paramListObj   = NULL;
	int          useVariables = 0;
	int          tuplesProcessed = 0;
	Tcl_Obj     *tuplesVarObj  = NULL;

	enum         positionalArgs {SELECT_ARG_CONN, SELECT_ARG_QUERY, SELECT_ARG_VAR, SELECT_ARG_PROC, SELECT_ARGS};
	int          nextPositionalArg = SELECT_ARG_CONN;

	struct stringStorage storage;

	for(index = 1; index < objc && nextPositionalArg != SELECT_ARGS; index++) {
	    char *arg = Tcl_GetString(objv[index]);
	    if (arg[0] == '-' && arg[1] != '-') {
		if        (strcmp(arg, "-withoutnulls") == 0) {
		    withoutNulls = 1;
		} else if (strcmp(arg, "-rowbyrow") == 0) {
	            rowByRow = 1;
		} else if (strcmp(arg, "-nodotfields") == 0) {
	            noDotFields = 1;
		} else if(strcmp(arg, "-variables") == 0) {
		    if(paramListObj || paramArrayName)
			goto parameter_conflict;
		    useVariables = 1;
		} else if (strcmp(arg, "-paramarray") == 0) {
		    if(paramListObj || useVariables)
			goto parameter_conflict;
		    index++;
		    paramArrayName = Tcl_GetString(objv[index]);
		} else if (strcmp(arg, "-count") == 0) {
		    index++;
		    tuplesVarObj = objv[index];
		    Tcl_UnsetVar(interp, Tcl_GetString(tuplesVarObj), 0);
		} else if (strcmp(arg, "-params") == 0) {
		    if(paramArrayName || useVariables) {
		      parameter_conflict:
			Tcl_SetResult(interp, "Can't combine multiple parameter flags (-variables, -paramarray, -params)", TCL_STATIC);
			return TCL_ERROR;
		    }
		    index++;
		    paramListObj = objv[index];
		} else {
			Tcl_SetObjResult(interp, Tcl_NewStringObj ("-arg argument isn't one of \"-nodotfields\", \"-variables\", \"-paramarray\", \"-params\", \"-rowbyrow\", \"-count\", or \"-withoutnulls\"", -1));
			return TCL_ERROR;
		}
	    } else {
		switch(nextPositionalArg) {
		    case SELECT_ARG_CONN:
			connString = Tcl_GetString(objv[index]);
			nextPositionalArg = SELECT_ARG_QUERY;
			break;
		    case SELECT_ARG_QUERY:
			queryString = Tcl_GetString(objv[index]);
			nextPositionalArg = SELECT_ARG_VAR;
			break;
		    case SELECT_ARG_VAR:
			varNameObj = objv[index];
			varNameString = Tcl_GetString(varNameObj);
			nextPositionalArg = SELECT_ARG_PROC;
			break;
		    case SELECT_ARG_PROC:
			procStringObj = objv[index];
			nextPositionalArg = SELECT_ARGS;
			break;
		}
	    }
	}
	
	if (index < objc || nextPositionalArg != SELECT_ARGS) {
		Tcl_WrongNumArgs(interp, 1, objv, "?-nodotfields? ?-rowbyrow? ?-withoutnulls? ?-variables? ?-paramarray var? ?-params list? ?-count var? connection queryString var proc");
		return TCL_ERROR;
	}

	if (useVariables) {
		if (handle_substitutions(interp, queryString, &newQueryString, &paramValues, &nParams, &paramsBuffer) != TCL_OK) {
			return TCL_ERROR;
		}
		if(nParams)
			queryString = newQueryString;
		else { // No variables being substituted, fall back to simple code path
			ckfree((void *)newQueryString);
			newQueryString = NULL;
			ckfree((void *)paramValues);
			paramValues = NULL;
		}
	}

	if(paramListObj) {
	    Tcl_Obj **listObjv;

	    if (Tcl_ListObjGetElements(interp, paramListObj, &nParams, &listObjv) == TCL_ERROR) {
		return TCL_ERROR;
	    }
	    if (build_param_array(interp, nParams, listObjv, &paramValues, &paramsBuffer) != TCL_OK) {
		return TCL_ERROR;
	    }
        }

	if(paramArrayName) {
	    // Count and validate parameters for PQexecParams(...paramValues...).
	    if(count_parameters(interp, queryString, &nParams) == TCL_ERROR) {
		return TCL_ERROR;
	    }

	    if (nParams) {
		if(expand_parameters(interp, queryString, nParams, paramArrayName, &newQueryString, &paramValues, &paramsBuffer) == TCL_ERROR) {
		    return TCL_ERROR;
		}
		queryString = newQueryString;
	    }
	}

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL) {
	    cleanup_params_and_return_error: {
		if(paramValues) ckfree((void *)paramValues);
		if(paramsBuffer) ckfree((void *)paramsBuffer);
		if(newQueryString) ckfree((void *)newQueryString);
		return TCL_ERROR;
	    }
	}

	if(nParams) {
		// TODO convert parameters to external
	}

	connid->sql_count++;
	if (rowByRow)
	{
		int status;

		initStorage(&storage);
		// Make the call
		if (nParams) {
			status = PQsendQueryParams(conn, externalString(&storage, queryString), nParams,
				NULL, paramValues, NULL, NULL, 0);
		} else {
			status = PQsendQuery(conn, externalString(&storage, queryString));
		}
		freeStorage(&storage);

		if(status == 0) {
			/* error occurred sending the query */
			report_connection_error(interp, conn);

			// Reconnect if the connection is bad.
			PgCheckConnectionState(connid);

			goto cleanup_params_and_return_error;
		}

		// It doesn't matter if this fails, the logic for handling the results is the same, we'll
		// just have a big wait before the first result comes out.
		PQsetSingleRowMode (conn);

		// Queue up the result.
		result = PQgetResult (conn);

		if(result == 0) {
			/* error occurred sending the query */
			report_connection_error(interp, conn);
			// Reconnect if the connection is bad.
			PgCheckConnectionState(connid);
			goto cleanup_params_and_return_error;
		}
	} else {
		initStorage(&storage);
		// Make the call AND queue up the result.
		if (nParams) {
			result = PQexecParams(conn, externalString(&storage, queryString), nParams,
				NULL, paramValues, NULL, NULL, 0);
		} else {
			result = PQexec(conn, externalString(&storage, queryString));
		}
		freeStorage(&storage);

		if (result == 0) {
			/* error occurred sending the query */
			report_connection_error(interp, conn);

			// Reconnect if the connection is bad.
			PgCheckConnectionState(connid);

			goto cleanup_params_and_return_error;
		}
	}

	// Register on the connection channel to hold it open (eg, in case a user
	// issues a pg_disconnect inside the select).
	Tcl_Channel conn_chan = Tcl_GetChannel(interp, connString, 0);
	Tcl_RegisterChannel(NULL, conn_chan);

	// At this point we no longer need these. Zap them so we don't have to worry about them
	// in the big loop.
	if(paramValues) {
		ckfree((void *)paramValues);
		paramValues = NULL;
	}
	if(newQueryString) {
		ckfree((void *)newQueryString);
		newQueryString = NULL;
	}

	if(paramsBuffer) {
		ckfree((void *)paramsBuffer);
		paramsBuffer = NULL;
	}

	/* Transfer any notify events from libpq to Tcl event queue. */
	// TODO: why was this commented out?
	// PgNotifyTransferEvents(connid);

	// Invariant from this point: If retval != TCL_OK we're exiting
	retval = TCL_OK;

	// Invariant from this point - result is NULL or result needs to be PQclear()ed
	while (result != NULL)
	{
		int resultStatus = PQresultStatus(result);

		// Don't care if it's row-by-row or not, these are the only good result statuses either way.
		if (resultStatus != PGRES_TUPLES_OK && resultStatus != PGRES_SINGLE_TUPLE)
		{
			/* query failed, or it wasn't SELECT */
			/* NB FIX there isn't necessarily an error here,
			 * meaning we can get an empty string */
			char *errString = PQresultErrorMessage(result);
			char *errStatus = PQresStatus (resultStatus);

			if (*errString == '\0')
			{
				errString = errStatus;
				Tcl_SetErrorCode(interp, "POSTGRESQL", errStatus, (char *)NULL);
			} else {
				char *nl = strchr(errString, '\n');
				if(nl) *nl = '\0';
				Tcl_SetErrorCode(interp, "POSTGRESQL", errStatus, errString, (char *)NULL);
				if(nl) *nl = '\n';
			}

			// Reconnect if the connection is bad (can only happen here in rowbyrow or first pass)
			if(rowByRow || firstPass)
				PgCheckConnectionState(connid);

			Tcl_SetResult(interp, errString, TCL_VOLATILE);
			retval = TCL_ERROR;
			goto done;
		}

		// Save the list of column names.
		if (firstPass)
		{
			ncols = PQnfields(result);
			columnNameObjs = (Tcl_Obj **)ckalloc(sizeof(Tcl_Obj *) * ncols);

			for (column = 0; column < ncols; column++) {
				char *colName = PQfname(result, column);
				if (colName == NULL) {
					// PQfname failed, shouldn't happen, but we've seen it
					char		msg[64];

					sprintf(msg, "PQfname() returned NULL for column %d, ncols %d",
								column, ncols);
					Tcl_SetResult(interp, msg, TCL_VOLATILE);
					retval = TCL_ERROR;
					goto done;
				} else {
					columnNameObjs[column] = Tcl_NewStringObj(colName, -1);
				}
			}

			columnListObj = Tcl_NewListObj(ncols, columnNameObjs);
			Tcl_IncrRefCount (columnListObj);

			firstPass = 0;
		}

		int numTuples = PQntuples(result);
		if(tuplesVarObj)
			Tcl_ObjSetVar2(interp, tuplesVarObj, NULL, Tcl_NewIntObj(numTuples), 0);

		// Loop over the result, even if it's a single row.
		for (tupno = 0; tupno < numTuples; tupno++)
		{
			// Clear array before filling it in. Ignore failure because it's
			// OK for the array not to exist at this point.
			Tcl_UnsetVar2(interp, varNameString, NULL, 0);

			// Set the dot fields in the array.
			if (!noDotFields)
			{
				if (Tcl_SetVar2Ex(interp, varNameString, ".headers",
						  columnListObj, TCL_LEAVE_ERR_MSG) == NULL ||
				    Tcl_SetVar2Ex(interp, varNameString, ".numcols",
						  Tcl_NewIntObj(ncols), TCL_LEAVE_ERR_MSG) == NULL ||
				    Tcl_SetVar2Ex(interp, varNameString, ".tupno",
						  Tcl_NewIntObj(tupno), TCL_LEAVE_ERR_MSG) == NULL)
				{
					retval = TCL_ERROR;
					goto done;
				}
			}

			// Set all of the column values for this row.
			for (column = 0; column < ncols; column++)
			{
				Tcl_Obj    *valueObj = NULL;
				char *string;

				string = PQgetvalue (result, tupno, column);
				if (*string == '\0') {
					if (PQgetisnull (result, tupno, column)) {
						if (withoutNulls) {
							// Don't need to unset because the array was cleared.
							continue;
						}

						if ((connid->nullValueString != NULL) && (*connid->nullValueString != '\0')) {
							valueObj = Tcl_NewStringObj(connid->nullValueString, -1);
						}
					}
				}

				if (valueObj == NULL) {
					initStorage(&storage);
					valueObj = Tcl_NewStringObj(utfString(&storage, string), -1);
					freeStorage(&storage);
				}

				if (Tcl_ObjSetVar2(interp, varNameObj, columnNameObjs[column],
							   valueObj, TCL_LEAVE_ERR_MSG) == NULL)
				{
					retval = TCL_ERROR;
					goto done;
				}
			}

			tuplesProcessed++;

			// Run the code body.
			r = Tcl_EvalObjEx(interp, procStringObj, 0);
			if ((r != TCL_OK) && (r != TCL_CONTINUE))
			{
				if (r == TCL_BREAK)
				{
					goto done;			/* exit loop, but leave TCL_OK in retval */
				}

				if (r == TCL_ERROR)
				{
					char		msg[60];

					sprintf(msg, "\n    (\"pg_select\" body line %d)",
							Tcl_GetErrorLine(interp));
					Tcl_AddErrorInfo(interp, msg);
				}

				retval = r;
				goto done;
			}
		}
		PQclear(result);
		if(rowByRow) {
			result = PQgetResult (conn);
		} else {
			result = NULL;
		}
	}

	done:
	/* drain output */
	while (result)
	{
		PQclear(result);
		if(rowByRow) {
			result = PQgetResult (conn);
		} else {
			result = NULL;
		}
	}

	if (columnListObj != NULL)
	{
		Tcl_DecrRefCount (columnListObj);
	}

	if (columnNameObjs != NULL)
	{
		ckfree((void *)columnNameObjs);
	}

	if(tuplesVarObj)
	    Tcl_UnsetVar(interp, Tcl_GetString(tuplesVarObj), 0);

	Tcl_UnregisterChannel(NULL, conn_chan);

	Tcl_UnsetVar(interp, varNameString, 0);

	return retval;
}

/*
 * Test whether any callbacks are registered on this connection for
 * the given relation name.  NB: supplied name must be case-folded already.
 */

static int
Pg_have_listener(Pg_ConnectionId * connid, const char *relname)
{
	Pg_TclNotifies *notifies;
	Tcl_HashEntry *entry;

	for (notifies = connid->notify_list;
		 notifies != NULL;
		 notifies = notifies->next)
	{
		Tcl_Interp *interp = notifies->interp;

		if (interp == NULL)
			continue;			/* ignore deleted interpreter */

		entry = Tcl_FindHashEntry(&notifies->notify_hash, (char *)relname);
		if (entry == NULL)
			continue;			/* no pg_listen in this interpreter */

		return 1;			/* OK, there is a listener */
	}

	return 0;				/* Found no listener */
}

/***********************************
Pg_listen
	create or remove a callback request for notifies on a given name

 syntax:
   pg_listen conn notifyname ?callbackcommand?

   With a fourth arg, creates or changes the callback command for
   notifies on the given name; without, cancels the callback request.

   Callbacks can occur whenever Tcl is executing its event loop.
   This is the normal idle loop in Tk; in plain tclsh applications,
   vwait or update can be used to enter the Tcl event loop.
***********************************/
int
Pg_listen(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	const char	   *origrelname;
	char	   *caserelname;
	char	   *callback = NULL;
	Pg_TclNotifies *notifies;
	Tcl_HashEntry *entry;
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	PGresult   *result;
	int			new;
	char	   *connString;
	int			callbackStrlen = 0;
	int         origrelnameStrlen;
        Tcl_Obj     *tresult;

	if (objc < 3 || objc > 4)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection relname ?callback?");
		return TCL_ERROR;
	}

	/*
	 * Get the command arguments. Note that the relation name will be
	 * copied by Tcl_CreateHashEntry while the callback string must be
	 * allocated by us.
	 */
	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

        if (connid->callbackPtr || connid->callbackInterp)
        {
               Tcl_SetResult(interp, "Attempt to query while waiting for callback", TCL_STATIC);
               return TCL_ERROR;
         }

	/*
	 * LISTEN/NOTIFY do not preserve case unless the relation name is
	 * quoted.	We have to do the same thing to ensure that we will find
	 * the desired pg_listen item.
	 */
	origrelname = Tcl_GetStringFromObj(objv[2], &origrelnameStrlen);
	caserelname = (char *)ckalloc((unsigned)(origrelnameStrlen + 1));
	if (*origrelname == '"')
	{
		/* Copy a quoted string without downcasing */
		strcpy(caserelname, origrelname + 1);
		caserelname[origrelnameStrlen - 2] = '\0';
	}
	else
	{
		/* Downcase it */
		const char   *rels = origrelname;
		char	   *reld = caserelname;

		while (*rels)
			*reld++ = tolower((unsigned char)*rels++);
		*reld = '\0';
	}

	if (objc > 3)
	{
		char	   *callbackStr;

		callbackStr = Tcl_GetStringFromObj(objv[3], &callbackStrlen);
		callback = ckalloc(callbackStrlen + 1);
		strcpy(callback, callbackStr);
	}

	/* Find or make a Pg_TclNotifies struct for this interp and connection */

	for (notifies = connid->notify_list; notifies; notifies = notifies->next)
	{
		if (notifies->interp == interp)
			break;
	}

	if (notifies == NULL)
	{
		notifies = (Pg_TclNotifies *) ckalloc(sizeof(Pg_TclNotifies));
		notifies->interp = interp;
		Tcl_InitHashTable(&notifies->notify_hash, TCL_STRING_KEYS);
		notifies->conn_loss_cmd = NULL;
		notifies->next = connid->notify_list;
		connid->notify_list = notifies;
		Tcl_CallWhenDeleted(interp, PgNotifyInterpDelete,
							(ClientData)notifies);
	}

	if (callback)
	{
		/*
		 * Create or update a callback for a relation
		 */
		int			alreadyHadListener = Pg_have_listener(connid, caserelname);

		entry = Tcl_CreateHashEntry(&notifies->notify_hash, caserelname, &new);
		/* If update, free the old callback string */
		if (!new)
			ckfree((void *)Tcl_GetHashValue(entry));

		/* Store the new callback string */
		Tcl_SetHashValue(entry, callback);

		/* Start the notify event source if it isn't already running */
		PgStartNotifyEventSource(connid);

		/*
		 * Send a LISTEN command if this is the first listener.
		 */
		if (!alreadyHadListener)
		{
			char	   *cmd = (char *)ckalloc((unsigned)(origrelnameStrlen + 8));

			sprintf(cmd, "LISTEN %s", origrelname);
			result = PQexec(conn, cmd);
			ckfree((void *)cmd);
			/* Transfer any notify events from libpq to Tcl event queue. */
			PgNotifyTransferEvents(connid);
			if (PQresultStatus(result) != PGRES_COMMAND_OK)
			{
				/* Error occurred during the execution of command */
				PQclear(result);
				Tcl_DeleteHashEntry(entry);
				ckfree((void *)callback);
				ckfree((void *)caserelname);
				report_connection_error(interp, conn);
				return TCL_ERROR;
			}
			PQclear(result);
		}
	}
	else
	{
		/*
		 * Remove a callback for a relation
		 */
		entry = Tcl_FindHashEntry(&notifies->notify_hash, caserelname);
		if (entry == NULL)
		{
                    tresult = Tcl_NewStringObj("not listening on ", -1);
                    Tcl_AppendStringsToObj(tresult, origrelname, NULL);
                    Tcl_SetObjResult(interp, tresult);

			ckfree((void *)caserelname);
			return TCL_ERROR;
		}
		ckfree((void *)Tcl_GetHashValue(entry));
		Tcl_DeleteHashEntry(entry);

		/*
		 * Send an UNLISTEN command if that was the last listener. Note:
		 * we don't attempt to turn off the notify mechanism if no LISTENs
		 * remain active; not worth the trouble.
		 */
		if (!Pg_have_listener(connid, caserelname))
		{
			char	   *cmd = (char *) ckalloc((unsigned)(origrelnameStrlen + 10));

			sprintf(cmd, "UNLISTEN %s", origrelname);
			result = PQexec(conn, cmd);
			ckfree((void *)cmd);
			/* Transfer any notify events from libpq to Tcl event queue. */
			PgNotifyTransferEvents(connid);
			if (PQresultStatus(result) != PGRES_COMMAND_OK)
			{
				/* Error occurred during the execution of command */
				PQclear(result);
				ckfree(caserelname);
				report_connection_error(interp, conn);
				return TCL_ERROR;
			}
			PQclear(result);
		}
	}

	ckfree(caserelname);
	return TCL_OK;
}

/**********************************
 * pg_sendquery
 send a query string to the backend connection

 syntax:
 pg_sendquery connection query

 the return result is either an error message or nothing, indicating the
 command was dispatched.
 **********************************/
int
Pg_sendquery(ClientData cData, Tcl_Interp *interp, int objc,
			 Tcl_Obj *CONST objv[])
{
	Pg_ConnectionId *connid;
	PGconn	        *conn;
        int              status;
	const char    *connString = NULL;
	const char      *execString = NULL;
	char            *newExecString = NULL;
	const char     **paramValues = NULL;
	char		*paramArrayName = NULL;
	const char      *paramsBuffer = NULL;
	int              nParams;
	int              index;
	int              useVariables = 0;

	enum             positionalArgs {SENDQUERY_ARG_CONN, SENDQUERY_ARG_SQL, SENDQUERY_ARGS};
	int              nextPositionalArg = SENDQUERY_ARG_CONN;

	struct stringStorage storage;

	for(index = 1; index < objc && nextPositionalArg != SENDQUERY_ARGS; index++) {
	    char *arg = Tcl_GetString(objv[index]);
	    if (arg[0] == '-') {
		if(strcmp(arg, "-paramarray") == 0) {
		    index++;
		    paramArrayName = Tcl_GetString(objv[index]);
		} else if(strcmp(arg, "-variables") == 0) {
		    useVariables = 1;
		} else {
		    goto wrong_args;
		}
	    } else {
		switch(nextPositionalArg) {
		    case SENDQUERY_ARG_CONN:
			connString = Tcl_GetString(objv[index]);
			nextPositionalArg = SENDQUERY_ARG_SQL;
			break;
		    case SENDQUERY_ARG_SQL:
			execString = Tcl_GetString(objv[index]);
			nextPositionalArg = SENDQUERY_ARGS;
			break;
		}
	    }
	}
	
	if (nextPositionalArg != SENDQUERY_ARGS || connString == NULL || execString == NULL)
	{
	    wrong_args:
		Tcl_WrongNumArgs(interp, 1, objv, "?-variables? ?-paramarray var? connection queryString ?parm...?");
		return TCL_ERROR;
	}

	/* figure out the connect string and get the connection ID */
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (connid->res_copyStatus != RES_COPY_NONE)
	{
	    Tcl_SetResult(interp, "Attempt to query while COPY in progress", TCL_STATIC);
	    return TCL_ERROR;
	}

        if (connid->callbackPtr || connid->callbackInterp)
        {
            Tcl_SetResult(interp, "Attempt to query while waiting for callback", TCL_STATIC);
	    return TCL_ERROR;
        }

	/* extra params will substitute for $1, $2, etc, in the statement */
	/* objc must be 3 or greater at this point */
	nParams = objc - index;

	if (useVariables) {
		if(paramArrayName || nParams) {
			Tcl_SetResult(interp, "-variables can not be used with positional or named parameters", TCL_STATIC);
			return TCL_ERROR;
		}
		if (handle_substitutions(interp, execString, &newExecString, &paramValues, &nParams, &paramsBuffer) != TCL_OK) {
			return TCL_ERROR;
		}
		if(nParams)
			execString = newExecString;
	} else if (paramArrayName) {
	    // Can't combine positional params and -paramarray
	    if (nParams) {
		Tcl_SetResult(interp, "Can't use both positional and named parameters", TCL_STATIC);
		return TCL_ERROR;
	    }
	    if (count_parameters(interp, execString, &nParams) == TCL_ERROR) {
		return TCL_ERROR;
	    }
	    if(nParams) {
		// After this point we must free newExecString and paramValues before exiting
		if (expand_parameters(interp, execString, nParams, paramArrayName, &newExecString, &paramValues, &paramsBuffer) == TCL_ERROR) {
		    return TCL_ERROR;
		}
		execString = newExecString;
	    }
	} else if (nParams) {
	    // After this point we must free paramValues and paramsBuffer before exiting
	    if (build_param_array(interp, nParams, &objv[index], &paramValues, &paramsBuffer) != TCL_OK) {
		return TCL_ERROR;
	    }
        }

	initStorage(&storage);
	if (nParams == 0) {
	    status = PQsendQuery(conn, externalString(&storage, execString));
	} else {
	    status = PQsendQueryParams(conn, externalString(&storage, execString), nParams, NULL, paramValues, NULL, NULL, 1);
	}
	freeStorage(&storage);
	if(newExecString) {
	    ckfree(newExecString);
	    newExecString = NULL;
	}
	if(paramValues) {
	    ckfree ((void *)paramValues);
	    paramValues = NULL;
	}
	if(paramsBuffer) {
	    ckfree((void *)paramsBuffer);
	    paramsBuffer = NULL;
	}
	connid->sql_count++;

	/* Transfer any notify events from libpq to Tcl event queue. */
	PgNotifyTransferEvents(connid);

	if (status)
	    return TCL_OK;
	else
	{
	    /* error occurred during the query */
	    report_connection_error(interp, conn);

	    // Reconnect if the connection is bad.
	    PgCheckConnectionState(connid);

	    return TCL_ERROR;
	}
}

/**********************************
 * pg_sendquery_prepared
 send a request to executed a prepared statement with given parameters  
 to the backend connection, asynchronously

 syntax:
 pg_sendquery_prepared connection statement_name [var1] [var2]...

 the return result is either an error message or a handle for a query
 result.  Handles start with the prefix "pgp"
 **********************************/

int
Pg_sendquery_prepared(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	char	   *connString;
	char	   *statementNameString;
	const char **paramValues = NULL;

	int         nParams;
	int         status;

	if (objc < 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection statementName [parm...]");
		return TCL_ERROR;
	}

	/* extra params will substitute for $1, $2, etc, in the statement */
	/* objc must be 3 or greater at this point */
	nParams = objc - 3;

	/* figure out the connect string and get the connection ID */

	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (connid->res_copyStatus != RES_COPY_NONE)
	{
		Tcl_SetResult(interp, "Attempt to query while COPY in progress", TCL_STATIC);
		return TCL_ERROR;
	}
//HERE//

	// TODO convert params
	/* If there are any extra params, allocate paramValues and fill it
	 * with the string representations of all of the extra parameters
	 * substituted on the command line.  Otherwise nParams will be 0,
	 * and we don't need to allocate space, paramValues will be NULL.
	 * However, prepared statements that don't take any parameters aren't
	 * generally real useful.
	 */
	if (nParams > 0) {
	    int param;

	    paramValues = (const char **)ckalloc (nParams * sizeof (char *));

	    for (param = 0; param < nParams; param++) {
		paramValues[param] = Tcl_GetString (objv[3+param]);
		if (strcmp(paramValues[param], "NULL") == 0)
                {
                    paramValues[param] = NULL;
                }
	    }
	}

	statementNameString = Tcl_GetString(objv[2]);

	status = PQsendQueryPrepared(conn, statementNameString, nParams, paramValues, NULL, NULL, 1);
	connid->sql_count++;

	if (paramValues != (const char **)NULL) {
	    ckfree ((void *)paramValues);
	}

	/* Transfer any notify events from libpq to Tcl event queue. */
	PgNotifyTransferEvents(connid);

	if (status)
		return TCL_OK;
	else
	{
		/* error occurred during the query */
		report_connection_error(interp, conn);

		// Reconnect if the connection is bad.
		PgCheckConnectionState(connid);

		return TCL_ERROR;
	}
}

/**********************************
 * pg_set_single_row_mode
 if called at the correct time and referencing new enough libpq (9.2+)
 this activates single-row return mode for the current query and returns 1,
 else returns 0.

 syntax:
 pg_set_single_row_mode connection

 the return result is either 1 or 0.
 **********************************/

int
Pg_set_single_row_mode(ClientData cData, Tcl_Interp *interp, int objc,
			Tcl_Obj *CONST objv[])
{
#ifndef HAVE_PQSETSINGLEROWMODE
                Tcl_SetObjResult(interp, 
                    Tcl_NewStringObj(
                        "function unavailable with this version of the postgres libpq library\n", -1));

	        return TCL_ERROR;
#else
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	char	   *connString;
	int         setRowModeResult;

	if (objc != 2)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	setRowModeResult = PQsetSingleRowMode (conn);
	Tcl_SetObjResult (interp, Tcl_NewIntObj (setRowModeResult));
	return TCL_OK;
#endif
}


/**********************************
 * pg_getresult
 wait for the next result from a prior pg_sendquery

 syntax:
 pg_getresult connection

 the return result is either an error message, nothing, or a handle for a query
 result.  Handles start with the prefix "pgp"
 **********************************/

int
Pg_getresult(ClientData cData, Tcl_Interp *interp, int objc,
			 Tcl_Obj *CONST objv[])
{
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	PGresult   *result;
	char	   *connString;

	if (objc != 2)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

        if (connid->callbackPtr || connid->callbackInterp)
        {
           /* Cancel any callback script: the user lost patience */

           Tcl_DecrRefCount(connid->callbackPtr);
           Tcl_Release((ClientData) connid->callbackInterp);

           connid->callbackPtr=NULL;
           connid->callbackInterp=NULL;
        }


	result = PQgetResult(conn);

	/* Transfer any notify events from libpq to Tcl event queue. */
	PgNotifyTransferEvents(connid);

	/* if there's a non-null result, give the caller the handle */
	if (result)
	{
		int	rId;
		if(PgSetResultId(interp, connString, result, &rId) != TCL_OK) {
			PQclear(result);
			return TCL_ERROR;
		}

		ExecStatusType rStat = PQresultStatus(result);

		if (rStat == PGRES_COPY_IN || rStat == PGRES_COPY_OUT)
		{
			connid->res_copyStatus = RES_COPY_INPROGRESS;
			connid->res_copy = rId;
		}
	}
	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Pg_getdata --
 *
 *    returns the data from the connection, from either a async
 *    connection, or a async query
 *
 * Syntax:
 *    pg_getdata $conn -result|-connection
 *
 * Results:
 *    the return result is a handle for the data that has
 *    arrived on that connection channel
 *
 *----------------------------------------------------------------------
 */

int
Pg_getdata(ClientData cData, Tcl_Interp *interp, int objc,
			 Tcl_Obj *CONST objv[])
{
    Pg_ConnectionId *connid;
    PGconn	    *conn;
    char	    *connString;
    int             optIndex;

    static const char *options[] = {
    	"-result", "-connection", NULL
    };

    enum options
    {
    	OPT_RESULT, OPT_CONNECTION
    };
    
    if (objc != 3)
    {
    	Tcl_WrongNumArgs(interp, 1, objv, "connection -result|-connection");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[2], options, "option", TCL_EXACT, &optIndex) != TCL_OK)
    {
		return TCL_ERROR;
    }

    connString = Tcl_GetString(objv[1]);

    conn = PgGetConnectionId(interp, connString, &connid);
    if (conn == NULL)
    	return TCL_ERROR;

    if (optIndex == OPT_RESULT)
    {
        PGresult        *result;
        result = PQgetResult(conn);

        /* if there's a non-null result, give the caller the handle */
        if (result)
        {
            int	rId;
            if(PgSetResultId(interp, connString, result, &rId) != TCL_OK) {
		PQclear(result);
	        return TCL_ERROR;
	    }
    
            ExecStatusType rStat = PQresultStatus(result);

	    // Reconnect if the connection is bad.
	    if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, conn);
		return TCL_ERROR;
	    }
    
            if (rStat == PGRES_COPY_IN || rStat == PGRES_COPY_OUT)
            {
                connid->res_copyStatus = RES_COPY_INPROGRESS;
                connid->res_copy = rId;
            }
        }
    }
    else if (optIndex == OPT_CONNECTION)
    {
        PostgresPollingStatusType pollstatus;
        Tcl_Obj         *res = NULL;

        pollstatus = PQconnectPoll(conn);

	// Reconnect if the connection is bad.
	if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, conn);
		return TCL_ERROR;
	}

	switch (pollstatus)
        {
            case PGRES_POLLING_FAILED:
            {
                res = Tcl_NewStringObj("PGRES_POLLING_FAILED", -1);
                break;
            }
            case PGRES_POLLING_READING:
            {
                res = Tcl_NewStringObj("PGRES_POLLING_READING", -1);
                break;
            }
            case PGRES_POLLING_WRITING:
            {
                res = Tcl_NewStringObj("PGRES_POLLING_WRITING", -1);
                break;
            }
            case PGRES_POLLING_OK:
            {
                res = Tcl_NewStringObj("PGRES_POLLING_OK", -1);
                break;
            }
            case PGRES_POLLING_ACTIVE:
            {
                res = Tcl_NewStringObj("PGRES_POLLING_ACTIVE", -1);
            }
        }

	Tcl_SetObjResult(interp, res);
    }
    else
    {
    	Tcl_WrongNumArgs(interp, 1, objv, "connection -result|-connection");
        return TCL_ERROR;
    }
        /* Transfer any notify events from libpq to Tcl event queue. */
        PgNotifyTransferEvents(connid);
    return TCL_OK;
}

/**********************************
 * pg_isbusy
 see if a query is busy, i.e. pg_getresult would block.

 syntax:
 pg_isbusy connection

 return is 1 if it's busy and pg_getresult would block, 0 otherwise
 **********************************/

int
Pg_isbusy(ClientData cData, Tcl_Interp *interp, int objc,
		  Tcl_Obj *CONST objv[])
{
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	char	   *connString;

	if (objc != 2)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

 	PQconsumeInput(conn);

        // Reconnect if the connection is bad.
        if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, conn);
		return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(PQisBusy(conn)));
	return TCL_OK;
}

/**********************************
 * pg_blocking
 see or set whether or not a connection is set to blocking or nonblocking

 syntax:
 pg_blocking connection
 pg_blocking connection 1
 pg_blocking connection 0

 return is 1 if it's blocking or 0 if not (if called with two arguments),
 sets blocking if called with 3.
 **********************************/

int
Pg_blocking(ClientData cData, Tcl_Interp *interp, int objc,
			Tcl_Obj *CONST objv[])
{
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	char	   *connString;
	int			boolean;

	if ((objc < 2) || (objc > 3))
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection ?bool?");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (objc == 2)
	{
		Tcl_SetObjResult(interp, Tcl_NewBooleanObj(!PQisnonblocking(conn)));
		return TCL_OK;
	}

	/* objc == 3, they're setting it */
	if (Tcl_GetBooleanFromObj(interp, objv[2], &boolean) == TCL_ERROR)
		return TCL_ERROR;

	PQsetnonblocking(conn, !boolean);
	return TCL_OK;
}

/**********************************
 * pg_null_value_string
 see or set the null value string

 syntax:
 pg_null_value_string connection
 pg_null_value_string connection nullString

 return is the current null value string if called with two arguments or
 the new null value string if called with 3.
 **********************************/

int
Pg_null_value_string(ClientData cData, Tcl_Interp *interp, int objc,
			         Tcl_Obj *CONST objv[])
{
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	char	   *connString;
	char       *nullValueString;
	int			length;

	if ((objc < 2) || (objc > 3))
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection ?string?");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (objc == 2)
	{
		if (connid->nullValueString == NULL || *connid->nullValueString == '\0') {
			Tcl_SetObjResult(interp, Tcl_NewStringObj("", 0));
		} else {
			Tcl_SetObjResult(interp, 
                          Tcl_NewStringObj(connid->nullValueString, -1));
		}
		return TCL_OK;
	}

	/* objc == 3, they're setting it */
	if (connid->nullValueString != NULL) {
		ckfree (connid->nullValueString);
	}

	nullValueString = Tcl_GetStringFromObj (objv[2], &length);
	connid->nullValueString = ckalloc (length + 1);
	strcpy (connid->nullValueString, nullValueString);

	Tcl_SetObjResult(interp, objv[2]);
	return TCL_OK;
}

/**********************************
 * pg_cancelrequest
 request that postgresql abandon processing of the current command

 syntax:
 pg_cancelrequest connection

 returns nothing if the command successfully dispatched or if nothing was
 going on, otherwise an error
 **********************************/

int
Pg_cancelrequest(ClientData cData, Tcl_Interp *interp, int objc,
				 Tcl_Obj *CONST objv[])
{
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	char	   *connString;

	if (objc != 2)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection");
		return TCL_ERROR;
	}

	connString = Tcl_GetString(objv[1]);

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

       /*
        * Clear any async result callback, if present.
        */

        if (connid->callbackPtr)    {
           Tcl_DecrRefCount(connid->callbackPtr);
           connid->callbackPtr = NULL;
        }

        if (connid->callbackInterp) {
           Tcl_Release((ClientData) connid->callbackInterp);
           connid->callbackInterp = NULL;
        }


	if (PQrequestCancel(conn) == 0)
	{
		// Reconnect if the connection is bad.
		if(PgCheckConnectionState(connid) != TCL_OK)
			report_connection_error(interp, conn);
		else
			Tcl_SetObjResult(interp,
				 Tcl_NewStringObj(PQerrorMessage(conn), -1));
		return TCL_ERROR;
	}
	return TCL_OK;
}

/***********************************
Pg_on_connection_loss
	create or remove a callback request for unexpected connection loss

 syntax:
   pg_on_connection_loss conn ?callbackcommand?

   With a third arg, creates or changes the callback command for
   connection loss; without, cancels the callback request.

   Callbacks can occur whenever Tcl is executing its event loop.
   This is the normal idle loop in Tk; in plain tclsh applications,
   vwait or update can be used to enter the Tcl event loop.
***********************************/
int
Pg_on_connection_loss(ClientData cData, Tcl_Interp *interp, int objc,
				 Tcl_Obj *CONST objv[])
{
	char	   *callback = NULL;
	Pg_TclNotifies *notifies;
	Pg_ConnectionId *connid;
	PGconn	   *conn;
	char	   *connString;

	if (objc < 2 || objc > 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection ?callback?");
		return TCL_ERROR;
	}

	/*
	 * Get the command arguments.
	 */
	connString = Tcl_GetString(objv[1]);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (objc > 2)
	{
		int         callbackStrLen;
		char	   *callbackStr;

		/* there is probably a better way to do this, like incrementing
		 * the reference count (?) */
		callbackStr = Tcl_GetStringFromObj(objv[2], &callbackStrLen);
		callback = (char *) ckalloc((unsigned) (callbackStrLen + 1));
		strcpy(callback, callbackStr);
	}

	/* Find or make a Pg_TclNotifies struct for this interp and connection */

	for (notifies = connid->notify_list; notifies; notifies = notifies->next)
	{
		if (notifies->interp == interp)
			break;
	}
	if (notifies == NULL)
	{
		notifies = (Pg_TclNotifies *) ckalloc(sizeof(Pg_TclNotifies));
		notifies->interp = interp;
		Tcl_InitHashTable(&notifies->notify_hash, TCL_STRING_KEYS);
		notifies->conn_loss_cmd = NULL;
		notifies->next = connid->notify_list;
		connid->notify_list = notifies;
		Tcl_CallWhenDeleted(interp, PgNotifyInterpDelete,
							(ClientData) notifies);
	}

	/* Store new callback setting */

	if (notifies->conn_loss_cmd)
		ckfree((void *) notifies->conn_loss_cmd);
	notifies->conn_loss_cmd = callback;

	if (callback)
	{
		/*
		 * Start the notify event source if it isn't already running. The
		 * notify source will cause Tcl to watch read-ready on the
		 * connection socket, so that we find out quickly if the
		 * connection drops.
		 */
		PgStartNotifyEventSource(connid);
	}

	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Pg_quote --
 *
 *    returns the quoted version of the passed in string
 *
 * Syntax:
 *    pg_quote ?-null? ?connection? string
 *
 * Results:
 *
 *    If the connection handle and the -null flag are both specified,
 *    we examine the string to see if it matches the null value string
 *    defined in the connection ID.  If it is, we return the string
 *    "NULL", unquoted.
 *
 *    If no connection handle but the -null flag is specified,
 *    we examine the string to see if it is the empty string.
 *    If it is, we return the string "NULL", unquoted.
 *
 *    If the passed in string doesn't match the null value string or if
 *    pg_quote was invoked without the -null flag, the string is escaped
 *    using PQescapeString or PQescapeStringConn, and that is returned.
 *
 *----------------------------------------------------------------------
 */
int
Pg_quote (ClientData cData, Tcl_Interp *interp, int objc,
		  Tcl_Obj *CONST objv[])
{
	char	   *fromString = NULL;
	char	   *toString;
	int         fromStringLen;
	int         stringSize;
	Pg_ConnectionId *connid = NULL;
	PGconn	   *conn = NULL;
	char	   *connString;
	int         do_null_handling = 0;
	int         error = 0;
	static Tcl_Obj *nullStringObj = NULL;

	/* allocate the null string object if we don't have it and increment
	 * its reference count so it'll never be freed.  We can use it over
	 * and over and it'll keep using the same string object
	 */
	if (nullStringObj == NULL)
	{
		nullStringObj = Tcl_NewStringObj ("NULL", -1);
		Tcl_IncrRefCount (nullStringObj);
	}

	if ((objc < 2) || (objc > 4))
	{
	wrongargs:
		Tcl_WrongNumArgs(interp, 1, objv, "?-null? ?connection? string");
		return TCL_ERROR;
	}

	if (objc == 2)
	{
		/*
		 * Get the "from" string.
		 */
		fromString = Tcl_GetStringFromObj(objv[1], &fromStringLen);
	}
	else if (objc == 3)
	{
		/*
		 * Get the connection object (or possibly the -null flag).
		 */
		connString = Tcl_GetString(objv[1]);
		if (connString[0] == '-' && strcmp(connString, "-null") == 0)
		{
			do_null_handling = 1;
		}
		else
		{
			// wasn't the -null flag, so it must be a connection.
			conn = PgGetConnectionId(interp, connString, &connid);
			if (conn == NULL)
				return TCL_ERROR;
		}

		/*
		 * Get the "from" string.
		 */
		fromString = Tcl_GetStringFromObj(objv[2], &fromStringLen);
	}
	else if (objc == 4)
	{
		/*
		 * Since there are 3 arguments, ensure the first one is the -null flag.
		 */
		connString = Tcl_GetString(objv[1]);
		if (connString[0] == '-' && strcmp(connString, "-null") == 0)
		{
			do_null_handling = 1;
		}
		else
		{
			// wasn't the -null flag, so there is a syntax issue.
			goto wrongargs;
		}

		/*
		 * Get the connection object.
		 */
		connString = Tcl_GetString(objv[2]);
		conn = PgGetConnectionId(interp, connString, &connid);
		if (conn == NULL)
			return TCL_ERROR;

		/*
		 * Get the "from" string.
		 */
		fromString = Tcl_GetStringFromObj(objv[3], &fromStringLen);

	}
	else
	{
	    goto wrongargs;
	}

	if (do_null_handling)
	{
		/*
		 * If the from string is empty, see if the null value string is also
		 * empty and if so, return the string NULL rather than something
		 * quoted
		 */
		if (fromStringLen == 0)
		{
			if (connid == NULL ||
			    connid->nullValueString == NULL ||
			    *connid->nullValueString == '\0')
			{
				Tcl_SetObjResult (interp, nullStringObj);
				return TCL_OK;
			}
		} else {
			/*
			 * The from string wasn't null, see if the connection's null value
			 * string also isn't null and if so, if they match and if so,
			 * return the string NULL
			 */
			if (connid != NULL && connid->nullValueString != NULL)
			{
				if (strcmp (fromString, connid->nullValueString) == 0)
				{
					Tcl_SetObjResult (interp, nullStringObj);
					return TCL_OK;
				}
			}
		}
	}

	/*
	 * Allocate the "to" string, the max size is documented in the
	 * postgres docs as 2 * fromStringLen + 1 and we add two more
	 * for the leading and trailing single quotes
	 */
	toString = (char *) ckalloc((2 * fromStringLen) + 3);

	/*
	 * call the library routine to escape the string, use
	 * Tcl_SetResult to set the command result to be that string,
	 * with TCL_DYNAMIC, we tell Tcl to free the memory when it's
	 * done with it
	 */
	*toString = '\'';

	if (conn != NULL)
	{
		stringSize = PQescapeStringConn (conn, toString+1, fromString,
										 fromStringLen, &error);
		if (error)
		{
			/* error returned from PQescapeStringConn, send it on up */
			ckfree (toString);
			Tcl_SetObjResult (interp, Tcl_NewStringObj ( PQerrorMessage (conn),
							  -1));
			return TCL_ERROR;
		}
	} else {
		stringSize = PQescapeString (toString+1, fromString, fromStringLen);
	}

	toString[stringSize+1] = '\'';
	toString[stringSize+2] = '\0';
	Tcl_SetResult(interp, toString, TCL_DYNAMIC);
	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Pg_escapeBytea --
 *
 *    returns the escaped version of the passed in binary
 *    string
 *
 * Syntax:
 *    pg_escapeBytea binaryString
 *
 * Results:
 *    the return result is either an error message or the passed
 *    in binary string after going through PQescapeBytea
 *
 * NOTE: PQunescapeBytea is *not* the direct inverse
 *     of PQescapeBytea. The result from PQescapeBytea needs
 *     to go through extra parsing, where as PQunescapeBytea
 *     is at the end of the parsing stage.
 *----------------------------------------------------------------------
 */
int
Pg_escapeBytea(ClientData cData, Tcl_Interp *interp, int objc,
                                 Tcl_Obj *CONST objv[])
{
        unsigned char    	*from;
        unsigned char           *to;
        int                      fromLen;
        size_t                   toLen;
	PGconn	                *conn = NULL;
	char                    *connString;

        if ((objc < 2) || (objc > 3))
        {
                Tcl_WrongNumArgs(interp, 1, objv, "?connection? binaryString");
                return TCL_ERROR;
        }

	if (objc == 2)
	{
	    /*
	     * Get the "from" string.
	     */
	    from = Tcl_GetByteArrayFromObj(objv[1], &fromLen);

	    to = PQescapeBytea(from, fromLen, &toLen);
	} else
	{
	    connString = Tcl_GetString(objv[1]);
	    conn = PgGetConnectionId(interp, connString, NULL);
	    if (conn == NULL)
		return TCL_ERROR;

	    /*
	     * Get the "from" string.
	     */
	    from = Tcl_GetByteArrayFromObj(objv[2], &fromLen);

	    to = PQescapeByteaConn(conn, from, fromLen, &toLen);
	}

        if (! to)
        {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("Failed to quote binary string", -1));
            return TCL_ERROR;
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj((char *)to, -1));

        #ifdef PQfreemem
            PQfreemem(to);
        #else
            PQfreeNotify(to);
        #endif

        return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Pg_unescapeBytea --
 *
 *    returns the unescaped version of the passed in escaped binary
 *    string
 *
 * Syntax:
 *    pg_unescapeBytea escapedBinaryString
 *
 * Results:
 *    the return result is either an error message or the passed
 *    in string, that has gone through PQunescapeBytea
 *
 * NOTE: PQunescapeBytea is *not* the direct inverse
 *     of PQescapeBytea. The result from PQescapeBytea needs
 *     to go through extra parsing, where as PQunescapeBytea
 *     is at the end of the parsing stage.
 *----------------------------------------------------------------------
 */
int
Pg_unescapeBytea(ClientData cData, Tcl_Interp *interp, int objc,
                                 Tcl_Obj *CONST objv[])
{
    const unsigned char  *from;
    unsigned char        *to;
    int         fromLen;
    size_t      toLen;

    if (objc != 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "binaryString");
        return TCL_ERROR;
    }

    from = (const unsigned char *)Tcl_GetStringFromObj(objv[1], &fromLen);
    to   = PQunescapeBytea(from, &toLen);
    if (! to)
    {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Failed to unquote binary string", -1));
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewByteArrayObj(to, toLen));

    #ifdef PQfreemem
        PQfreemem(to);
    #else
        PQfreeNotify(to);
    #endif

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Pg_dbinfo --
 *
 *    returns either the connection handles or the result handles
 *
 * Syntax:
 *    pg_dbinfo connections
 *    pg_dbinfo results connHandle 
 *    pg_dbinfo version connHandle 
 *    pg_dbinfo protocol connHandle 
 *    pg_dbinfo param connHandle paramName
 *    pg_dbinfo backendpid connHandle
 *    pg_dbinfo socket connHandle
 *    pg_dbinfo sql_count connHandle
 *
 *    pg_dbinfo dbname connHandle
 *    pg_dbinfo user connHandle
 *    pg_dbinfo pass connHandle
 *    pg_dbinfo host connHandle
 *    pg_dbinfo port connHandle
 *    pg_dbinfo options connHandle
 *    pg_dbinfo status connHandle
 *    pg_dbinfo transaction_status connHandle
 *    pg_dbinfo error_message connHandle
 *    pg_dbinfo needs_password connHandle
 *    pg_dbinfo used_password connHandle
 *    pg_dbinfo used_ssl connHandle
 *
 * Results:
 *    the return result is either an error message or a list of
 *    the connection/result handles.
 *
 *----------------------------------------------------------------------
 */
int
Pg_dbinfo(ClientData cData, Tcl_Interp *interp, int objc,
				 Tcl_Obj *CONST objv[])
{
    Pg_ConnectionId *connid = NULL;
    char	    *connString = NULL;
    char	    buf[32];
    Tcl_Obj         *listObj;
    Tcl_Obj         *tresult;
    Tcl_Obj         **elemPtrs;
    int             i, count, optIndex;
    Tcl_Channel     conn_chan;
    const char      *paramname;

    static const char *cmdargs = "connections|results|version|protocol|param|backendpid|socket|sql_count|dbname|user|password|host|port|options|status|transaction_status|error_message|needs_password|used_password|used_ssl";

    static const char *options[] = {
    	"connections", "results", "version", "protocol", 
        "param", "backendpid", "socket", "sql_count", 
	"dbname", "user", "password", "host", "port",
	"options", "status", "transaction_status",
	"error_message", "needs_password", "used_password",
	"used_ssl",
	NULL
    };

    enum options
    {
    	OPT_CONNECTIONS, OPT_RESULTS, OPT_VERSION, OPT_PROTOCOL,
        OPT_PARAM, OPT_BACKENDPID, OPT_SOCKET, OPT_SQL_COUNT,
	OPT_DBNAME, OPT_USER, OPT_PASSWORD, OPT_HOST, OPT_PORT,
	OPT_OPTIONS, OPT_STATUS, OPT_TRANSACTION_STATUS,
	OPT_ERROR_MESSAGE, OPT_NEEDS_PASSWORD, OPT_USED_PASSWORD,
	OPT_USED_SSL
    };
    
    if (objc <= 1)
    {
	Tcl_WrongNumArgs(interp,1,objv,cmdargs);
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], options, "option", TCL_EXACT, &optIndex) != TCL_OK) {
		return TCL_ERROR;
    }

    /* 
     * this is common for most cmdargs, so do it upfront
     */
    if (optIndex != OPT_CONNECTIONS)
    {
	if (optIndex == OPT_PARAM) { // OPT_PARAM is a maverick
        	if (objc != 4) {
			Tcl_WrongNumArgs(interp, 2, objv, "connHandle paramName");
			return TCL_ERROR;
		}
	} else {
        	if (objc != 3) {
			Tcl_WrongNumArgs(interp, 2, objv, "connHandle");
			return TCL_ERROR;
		}
	}

	connString = Tcl_GetString(objv[2]);
	conn_chan = Tcl_GetChannel(interp, connString, 0);
	if (conn_chan == NULL)
	{
	    tresult = Tcl_NewStringObj(connString, -1);
	    Tcl_AppendStringsToObj(tresult, " is not a valid connection", NULL);
	    Tcl_SetObjResult(interp, tresult);

	    return TCL_ERROR;
	}

	/* Check that it is a PG connection and not something else */
	connid = (Pg_ConnectionId *) Tcl_GetChannelInstanceData(conn_chan);

	if (connid->conn == NULL) {
	    return TCL_ERROR;
	}
    }

    switch ((enum options) optIndex)
    {
        case OPT_CONNECTIONS:
        {

            listObj = Tcl_NewListObj(0, (Tcl_Obj **) NULL);

            /*
             * This is not a very robust method to use.
             * Will have to re-think this
             */
            Tcl_GetChannelNames(interp);

            Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp), 
                &count, &elemPtrs);

            for (i = 0; i < count; i++) {

                char *name = Tcl_GetString(elemPtrs[i]);

                conn_chan = Tcl_GetChannel(interp, name, 0);
                if (conn_chan != NULL && 
                    Tcl_GetChannelType(conn_chan) == &Pg_ConnType)
                {

                    if (Tcl_ListObjAppendElement(interp, listObj, elemPtrs[i]) != TCL_OK)
                    {
                        Tcl_DecrRefCount(listObj);
                        return TCL_ERROR;
                    }
                }


            }
             break;
        }
        case OPT_RESULTS:
        {

        listObj = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
    
        for (i = 0; i <= connid->res_last; i++)
        {
     
            if (connid->results[i] == 0)
            {
                continue;
            }
    
            sprintf(buf, "%s.%d", connString, i);
            if (Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(buf, -1)) != TCL_OK)
            {
                Tcl_DecrRefCount(listObj);
                return TCL_ERROR;
            }
        }
            break;
        }
        case OPT_VERSION:
        {
#define SET_AND_CHECK_INT(fun) { \
		int result = fun; \
		if(PgCheckConnectionState(connid) != TCL_OK) { \
			report_connection_error(interp, connid->conn); \
			return TCL_ERROR; \
		} \
		Tcl_SetObjResult(interp, Tcl_NewIntObj(result)); \
	}
#define SET_AND_CHECK_BOOL(fun) { \
		int result = fun; \
		if(PgCheckConnectionState(connid) != TCL_OK) { \
			report_connection_error(interp, connid->conn); \
			return TCL_ERROR; \
		} \
		Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result)); \
	}
#define SET_AND_CHECK_STRING(fun) { \
		const char *result = fun; \
		if(PgCheckConnectionState(connid) != TCL_OK) { \
			report_connection_error(interp, connid->conn); \
			return TCL_ERROR; \
		} \
		Tcl_SetObjResult(interp, Tcl_NewStringObj(result, -1)); \
	}

	    SET_AND_CHECK_INT(PQserverVersion(connid->conn));
    
            return TCL_OK;

        }
        case OPT_PROTOCOL:
        {
	    SET_AND_CHECK_INT(PQprotocolVersion(connid->conn));
            return TCL_OK;
        }
        case OPT_PARAM:
        {
            paramname = Tcl_GetString(objv[3]);
	    SET_AND_CHECK_STRING(PQparameterStatus(connid->conn,paramname));
            return TCL_OK;
        }
        case OPT_BACKENDPID:
        {
	    SET_AND_CHECK_INT(PQbackendPID(connid->conn));
            return TCL_OK;
        }
        case OPT_SOCKET:
        {
	    SET_AND_CHECK_INT(PQsocket(connid->conn));
            return TCL_OK;
        }
        case OPT_SQL_COUNT:
        {
	    // Can't call libpq, so leave alone
            Tcl_SetObjResult(interp, Tcl_NewIntObj(
                             connid->sql_count));
            return TCL_OK;
        }

	case OPT_DBNAME:
	{
	    SET_AND_CHECK_STRING(PQdb(connid->conn));
            return TCL_OK;
	}

	case OPT_USER:
	{
	    SET_AND_CHECK_STRING(PQuser(connid->conn));
            return TCL_OK;
	}

	case OPT_PASSWORD:
	{
            SET_AND_CHECK_STRING(PQpass(connid->conn));
            return TCL_OK;
	}

	case OPT_HOST:
	{
	    SET_AND_CHECK_STRING(PQhost(connid->conn));
            return TCL_OK;
	}

	case OPT_PORT:
	{
	    SET_AND_CHECK_STRING(PQport(connid->conn));
            return TCL_OK;
	}

	case OPT_OPTIONS: {
	    SET_AND_CHECK_STRING(PQoptions(connid->conn));
            return TCL_OK;
	}

	case OPT_STATUS: {
	    switch (PQstatus(connid->conn)) {
	        case CONNECTION_OK:
		{
		    Tcl_SetObjResult(interp, 
		        Tcl_NewStringObj("connection_ok", -1));
		    return TCL_OK;
		}

		case CONNECTION_BAD: {
		    Tcl_SetObjResult(interp, 
		        Tcl_NewStringObj("connection_bad", -1));
		    return TCL_OK;
		}

		default: {
		    Tcl_SetObjResult(interp, 
		        Tcl_NewStringObj("unrecognized_status", -1));
		    return TCL_OK;
		}
	    }
	}

	case OPT_TRANSACTION_STATUS: {
	    int status = PQtransactionStatus(connid->conn);
            // Reconnect if the connection is bad.
            if(PgCheckConnectionState(connid) != TCL_OK) {
		report_connection_error(interp, connid->conn);
		return TCL_ERROR;
	    }

	    switch (status) {
	        case PQTRANS_IDLE: {
		    Tcl_SetObjResult(interp, 
		        Tcl_NewStringObj("idle", -1));
		    return TCL_OK;
		}

	        case PQTRANS_ACTIVE: {
		    Tcl_SetObjResult(interp, 
		        Tcl_NewStringObj("command_in_progress", -1));
		    return TCL_OK;
		}

		case PQTRANS_INTRANS: {
		    Tcl_SetObjResult(interp, 
		        Tcl_NewStringObj("idle_in_valid_block", -1));
		    return TCL_OK;
		}

		case PQTRANS_INERROR: {
		    Tcl_SetObjResult(interp, 
		        Tcl_NewStringObj("idle_in_failed_block", -1));
		    return TCL_OK;
		}

		case PQTRANS_UNKNOWN: {
		    Tcl_SetObjResult(interp, 
		        Tcl_NewStringObj("connection_bad", -1));
		    return TCL_OK;
		}

		default: {
		    Tcl_SetObjResult(interp, 
		        Tcl_NewStringObj("unrecognized_status", -1));
		    return TCL_OK;
		}
	    }
	}

	case OPT_ERROR_MESSAGE: {
	    SET_AND_CHECK_STRING(PQerrorMessage(connid->conn));
            return TCL_OK;
	}

	case OPT_NEEDS_PASSWORD: {
	    SET_AND_CHECK_BOOL(PQconnectionNeedsPassword(connid->conn));
            return TCL_OK;
	}

	case OPT_USED_PASSWORD: {
	    SET_AND_CHECK_BOOL(PQconnectionUsedPassword(connid->conn));
            return TCL_OK;
	}

	case OPT_USED_SSL: {
	    SET_AND_CHECK_BOOL( (PQgetssl(connid->conn) != NULL) );
            return TCL_OK;
	}

        default:
        {
	    Tcl_WrongNumArgs(interp,1,objv,cmdargs);
            return TCL_ERROR;
        }

    }
    Tcl_SetObjResult(interp, listObj);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Pg_sql --
 *
 *    returns result handle
 *
 * Syntax:
 *    pg_sql connhandle sqlStmt \
 *        ?-params {list}? \
 *        ?-binparams {list}? \
 *        ?-binresults? \
 *        ?-callback script? \
 *        ?-async yes|no? \
 *        ?-prepared yes|no?
 *
 * Results:
 *    the return result is either an error message or a list of
 *    the connection/result handles.
 *
 *----------------------------------------------------------------------
 */
int
Pg_sql(ClientData cData, Tcl_Interp *interp, int objc,
				 Tcl_Obj *CONST objv[])
{

    PGconn          *conn;
    PGresult        *result = NULL;
    int              iResult = 0;
    const char    *connString;
    const char      *execString;
    const char     **paramValues = NULL;
    int             *binValues = NULL;
    const int       *paramLengths = NULL;
    Pg_ConnectionId *connid;
    Tcl_Obj         **elemPtrs;
    Tcl_Obj         **elembinPtrs;
    int             i=3;
    int             count=0, countbin=0, optIndex;
    int             params=0,binparams=0,binresults=0,callback=0,async=0,prepared=0;
    unsigned char   flags = 0;
    struct stringStorage storage;

    static const char *cmdargs = "";

    static const char *options[] = {
    	"-params", "-binparams", "-binresults", "-callback", 
        "-async", "-prepared", NULL
    };

    enum options
    {
    	OPT_PARAMS, OPT_BINPARAMS, OPT_BINRESULTS, OPT_CALLBACK,
        OPT_ASYNC, OPT_PREPARED
    };
    
    if (objc < 3)
    {
	Tcl_WrongNumArgs(interp,1,objv,cmdargs);
        return TCL_ERROR;
    }

    /*
     *  We just loop through now to set some
     *  flags, since some of the options are
     *  dependent on others
     */
    while (i < objc)
    {

        if (Tcl_GetIndexFromObj(interp, objv[i], options,
		   "option", TCL_EXACT, &optIndex) != TCL_OK)
		    return TCL_ERROR;

        switch ((enum options) optIndex)
        {
            case OPT_PARAMS:
            {
                flags = flags | 0x01;
                params = i+1;
                i=i+2;
                Tcl_ListObjGetElements(interp, objv[params], &count, &elemPtrs);
                if (count == 0) {
                    params = 0;
                }

                break;
            }
            case OPT_BINPARAMS:
            {
                flags = flags | 0x02;
                binparams = i+1;
                i=i+2;
                break;
            }
            case OPT_BINRESULTS:
            {
                flags = flags | 0x04;
                Tcl_GetBooleanFromObj(interp, objv[i+1], &binresults);
                /*
                binresults = i+1;
                */
                i=i+2;
                break;
            }
            case OPT_CALLBACK:
            {
                /* assume async if -callback too */
                flags = flags | 0x10;
                flags = flags | 0x08;
                callback = i+1;
                async = 1;
                i=i+2;
                break;
            }
            case OPT_ASYNC:
            {
                flags = flags | 0x10;
                Tcl_GetBooleanFromObj(interp, objv[i+1], &async);
                /*
                async = i+1;
                */
                i=i+2;
                break;
            }
            case OPT_PREPARED:
            {
                flags = flags | 0x20;
                /*
                prepared = i+1;
                */
                Tcl_GetBooleanFromObj(interp, objv[i+1], &prepared);
                i=i+2;
            }
        } /* end switch */


        /*
         * Check error case where -binparams or
         * -binresults are given but -params is not
         if ((flags == 0x06) || (flags == 0x04) || (flags == 0x02)) {
            Tcl_SetResult(interp, "Need to specify -params", TCL_STATIC);
            return TCL_ERROR;
         }
*/

    } /* end while */

    /*
     * Check error case where -binparams or
     * -binresults are given but -params is not
     */
     if (!params && (binparams != 0 || binresults != 0)) {
        Tcl_SetResult(interp, "Need to specify -params option", TCL_STATIC);
        return TCL_ERROR;
     }

    /*
     *  Handle param options
     */
     if (params) {
         Tcl_ListObjGetElements(interp, objv[binparams], &countbin, &elembinPtrs);

         if (countbin != 0 && countbin != count) {
            Tcl_SetResult(interp, "-params and -binparams need the same number of elements", TCL_STATIC); 
            return TCL_ERROR;
         }

	 int param;

	 paramValues = (const char **)ckalloc (count * sizeof (char *));
	 binValues = (int *)ckalloc (countbin * sizeof (char *));

	 for (param = 0; param < count; param++) {
		// TODO convert to external
	     paramValues[param] = Tcl_GetString (elemPtrs[param]);
	     if (strcmp(paramValues[param], "NULL") == 0)
             {
                 paramValues[param] = NULL;
             }
	 }

	 for (param = 0; param < countbin; param++) {
	     Tcl_GetBooleanFromObj (interp, elembinPtrs[param], &binValues[param]);
	 }
     }

    connString = Tcl_GetString(objv[1]);
    conn = PgGetConnectionId(interp, connString, &connid);
    if (conn == NULL) 
            return TCL_ERROR;

    if (connid->res_copyStatus != RES_COPY_NONE)
    {
        Tcl_SetResult(interp, "Attempt to query while COPY in progress", TCL_STATIC); 
        return TCL_ERROR;
    }

    execString = Tcl_GetString(objv[2]);

    /*
     * Handle the callback first, before executing statments
     */
    initStorage(&storage);
    if (callback) {
        if (connid->callbackPtr || connid->callbackInterp)
        {
            Tcl_SetResult(interp, "Attempt to wait for result while already waiting", TCL_STATIC);
            return TCL_ERROR;
       }

       /* Start the notify event source if it isn't already running */
       PgStartNotifyEventSource(connid);

       connid->callbackPtr= objv[callback];
       connid->callbackInterp= interp;

       Tcl_IncrRefCount(objv[callback]);
       Tcl_Preserve((ClientData) interp);

       /* 
        *  invoke function based on type 
        *  of query 
        */
        if (prepared) {
	    iResult = PQsendQueryPrepared(conn, externalString(&storage, execString), count, paramValues, paramLengths, binValues, binresults);
        } else if (params) {
            iResult = PQsendQueryParams(conn, externalString(&storage, execString), count, NULL, paramValues, paramLengths, binValues, binresults);

        } else {
    
            iResult = PQsendQuery(conn, externalString(&storage, execString));
/*
            ckfree ((void *)paramValues);
*/
        }
    } else {

        if (prepared) {
	    result = PQexecPrepared(conn, externalString(&storage, execString), count, paramValues, paramLengths, binValues, binresults);
        } else if (params) {
            result = PQexecParams(conn, externalString(&storage, execString), count, NULL, paramValues, paramLengths, binValues, binresults);
        } else {
            result = PQexec(conn, externalString(&storage, execString));
            ckfree ((void *)paramValues);
        }
    } /* end if callback */
    freeStorage(&storage);

    PgNotifyTransferEvents(connid);

    // Reconnect if the connection is bad.
    if(PgCheckConnectionState(connid) != TCL_OK) {
	report_connection_error(interp, conn);
	return TCL_ERROR;
    }

    if (((result != NULL) || (iResult > 0)) && !callback)
    {
	int	rId;
	if(PgSetResultId(interp, connString, result, &rId) != TCL_OK) {
		PQclear(result);
		return TCL_ERROR;
	}

	ExecStatusType rStat = PQresultStatus(result);

	if (rStat == PGRES_COPY_IN || rStat == PGRES_COPY_OUT)
	{
		connid->res_copyStatus = RES_COPY_INPROGRESS;
		connid->res_copy = rId;
	}
	return TCL_OK;
    }
    else if ((result == NULL) && (iResult == 0))
    {
	/* error occurred during the query */
	report_connection_error(interp, conn);
	return TCL_ERROR;
    }

    
    return TCL_OK;
}

/*
error severity
error sqlstate
error message
error message primary
error message detail
error message hint
error position
error context
error source file
error source line
error source function
*/
