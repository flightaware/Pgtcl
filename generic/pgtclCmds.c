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

#include "pgtclCmds.h"
#include "pgtclId.h"
#include "libpq/libpq-fs.h"		/* large-object interface */

#ifndef CONST84
#     define CONST84
#endif

/*
 * Local function forward declarations
 */
static int execute_put_values(Tcl_Interp *interp, CONST84 char *array_varname,
				   PGresult *result, int tupno);


#ifdef TCL_ARRAYS

#define ISOCTAL(c)		(((c) >= '0') && ((c) <= '7'))
#define DIGIT(c)		((c) - '0')


/*
 * translate_escape()
 *
 * This function performs in-place translation of a single C-style
 * escape sequence pointed by p. Curly braces { } and double-quote
 * are left escaped if they appear inside an array.
 * The value returned is the pointer to the last character (the one
 * just before the rest of the buffer).
 */

static inline char *
translate_escape(char *p, int isArray)
{
	char		c,
			   *q,
			   *s;

#ifdef TCL_ARRAYS_DEBUG_ESCAPE
	printf("   escape = '%s'\n", p);
#endif
	/* Address of the first character after the escape sequence */
	s = p + 2;
	switch (c = *(p + 1))
	{
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
			c = DIGIT(c);
			if (ISOCTAL(*s))
				c = (c << 3) + DIGIT(*s++);
			if (ISOCTAL(*s))
				c = (c << 3) + DIGIT(*s++);
			*p = c;
			break;
		case 'b':
			*p = '\b';
			break;
		case 'f':
			*p = '\f';
			break;
		case 'n':
			*p = '\n';
			break;
		case 'r':
			*p = '\r';
			break;
		case 't':
			*p = '\t';
			break;
		case 'v':
			*p = '\v';
			break;
		case '\\':
		case '{':
		case '}':
		case '"':

			/*
			 * Backslahes, curly braces and double-quotes are left escaped
			 * if they appear inside an array. They will be unescaped by
			 * Tcl in Tcl_AppendElement. The buffer position is advanced
			 * by 1 so that the this character is not processed again by
			 * the caller.
			 */
			if (isArray)
				return p + 1;
			else
				*p = c;
			break;
		case '\0':

			/*
			 * This means a backslash at the end of the string. It should
			 * never happen but in that case replace the \ with a \0 but
			 * don't shift the rest of the buffer so that the caller can
			 * see the end of the string and terminate.
			 */
			*p = c;
			return p;
			break;
		default:

			/*
			 * Default case, store the escaped character over the
			 * backslash and shift the buffer over itself.
			 */
			*p = c;
	}
	/* Shift the rest of the buffer over itself after the current char */
	q = p + 1;
	for (; *s;)
		*q++ = *s++;
	*q = '\0';
#ifdef TCL_ARRAYS_DEBUG_ESCAPE
	printf("   after  = '%s'\n", p);
#endif
	return p;
}

/*
 * tcl_value()
 *
 * This function does in-line conversion of a value returned by libpq
 * into a tcl string or into a tcl list if the value looks like the
 * representation of a postgres array.
 */

static char *
tcl_value(char *value)
{
	int			literal,
				last;
	char	   *p;

	if (!value)
		return NULL;


#ifdef TCL_ARRAYS_DEBUG
	printf("pq_value  = '%s'\n", value);
#endif
	last = strlen(value) - 1;
	if ((last >= 1) && (value[0] == '{') && (value[last] == '}'))
	{
		/* Looks like an array, replace ',' with spaces */
		/* Remove the outer pair of { }, the last first! */
		value[last] = '\0';
		value++;
		literal = 0;
		for (p = value; *p; p++)
		{
			if (!literal)
			{
				/* We are at the list level, look for ',' and '"' */
				switch (*p)
				{
					case '"':	/* beginning of literal */
						literal = 1;
						break;
					case ',':	/* replace the ',' with space */
						*p = ' ';
						break;
				}
			}
			else
			{
				/* We are inside a C string */
				switch (*p)
				{
					case '"':	/* end of literal */
						literal = 0;
						break;
					case '\\':

						/*
						 * escape sequence, translate it
						 */
						p = translate_escape(p, 1);
						break;
				}
			}
			if (!*p)
				break;
		}
	}
	else
	{
		/* Looks like a normal scalar value */
		for (p = value; *p; p++)
		{
			if (*p == '\\')
			{
				/*
				 * escape sequence, translate it
				 */
				p = translate_escape(p, 0);
			}
			if (!*p)
				break;
		}
	}
#ifdef TCL_ARRAYS_DEBUG
	printf("tcl_value = '%s'\n\n", value);
#endif
	return value;
}
#else    /* TCL_ARRAYS */
#define tcl_value(x) x
#endif   /* TCL_ARRAYS */


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
		Tcl_Obj    *resultList = Tcl_GetObjResult(interp);

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
    PGconn	   *conn;
    char	   *firstArg;
    char	   *conninfoString;
    char	   *connhandle = NULL;
    int            optIndex, i, skip = 0;
    Tcl_DString    ds;
        

    static CONST char *options[] = {
    	"-host", "-port", "-tty", "-options", "-user", 
        "-password", "-conninfo", "-connlist", "-connhandle", (char *)NULL
    };

    enum options
    {
    	OPT_HOST, OPT_PORT, OPT_TTY, OPT_OPTIONS, OPT_USER, 
        OPT_PASSWORD, OPT_CONNINFO, OPT_CONNLIST, OPT_CONNHANDLE
    };

    Tcl_DStringInit(&ds);

    if (objc == 1)
    {
        Tcl_DStringAppend(&ds, "pg_connect: database name missing\n", -1);
        Tcl_DStringAppend(&ds, "pg_connect databaseName [-host hostName] [-port portNumber] [-tty pgtty]\n", -1);
        Tcl_DStringAppend(&ds, "pg_connect -conninfo conninfoString", -1);
        Tcl_DStringAppend(&ds, "pg_connect -connlist [connlist]", -1);
        Tcl_DStringResult(interp, &ds);

        return TCL_ERROR;
    }



    i = objc%2 ? 1 : 2;

    while (i + 1 < objc)
    {
        char	   *nextArg = Tcl_GetStringFromObj(objv[i + 1], NULL);


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
                        Tcl_GetStringFromObj(elemPtrs[lelem], NULL), -1);
                    Tcl_DStringAppend(&ds, "=", -1);
                    Tcl_DStringAppend(&ds, 
                        Tcl_GetStringFromObj(elemPtrs[lelem+1], NULL), -1);
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
        Tcl_DStringAppend(&ds, Tcl_GetStringFromObj(objv[1], NULL), -1);
    }

    conn = PQconnectdb(Tcl_DStringValue(&ds));

 
    Tcl_DStringFree(&ds);

    if (PQstatus(conn) == CONNECTION_OK)
    {
        PgSetConnectionId(interp, conn, connhandle);
        return TCL_OK;
    }
    else
    {
        Tcl_AppendResult(interp, "Connection to database failed\n",
						 PQerrorMessage(conn), 0);
        PQfinish(conn);
        return TCL_ERROR;
    }
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
    char	   *connString;

    if (objc != 2)
    {
	Tcl_WrongNumArgs(interp, 1, objv, "connection");
	return TCL_ERROR;
    }

    connString = Tcl_GetStringFromObj(objv[1], NULL);
    conn_chan = Tcl_GetChannel(interp, connString, 0);
    if (conn_chan == NULL)
    {
	Tcl_ResetResult(interp);
	Tcl_AppendResult(interp, connString, " is not a valid connection", 0);
	return TCL_ERROR;
    }


    /* Check that it is a PG connection and not something else */
    connid = (Pg_ConnectionId *) Tcl_GetChannelInstanceData(conn_chan);

    if (connid->conn == NULL)
	return TCL_ERROR;

    if (connid->cmd_token != NULL)
        Tcl_DeleteCommandFromToken(interp, connid->cmd_token);

    return Tcl_UnregisterChannel(interp, conn_chan);
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
	PGconn	   *conn;
	PGresult   *result;
	const char	   *connString;
	const char *execString;
	const char **paramValues = NULL;

	/* THIS CODE IS REPLICATED IN Pg_sendquery AND SHOULD BE FACTORED */
#ifdef HAVE_PQEXECPARAMS
	int         nParams;

	if (objc < 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection queryString [parm...]");
		return TCL_ERROR;
	}

	/* extra params will substitute for $1, $2, etc, in the statement */
	/* objc must be 3 or greater at this point */
	nParams = objc - 3;

	/* If there are any extra params, allocate paramValues and fill it
	 * with the string representations of all of the extra parameters
	 * substituted on the command line.  Otherwise nParams will be 0,
	 * and PQexecParams will work just like PQexec (no $-substitutions).
	 */
	if (nParams > 0) {
	    int param;

	    paramValues = (const char **)ckalloc (nParams * sizeof (char *));

	    for (param = 0; param < nParams; param++) {
		paramValues[param] = Tcl_GetStringFromObj (objv[3+param], NULL);
	    }
	}
#else /* HAVE_PQEXECPARAMS */
	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection queryString");
		return TCL_ERROR;
	}
#endif /* HAVE_PQEXECPARAMS */

	/* figure out the connect string and get the connection ID */

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (connid->res_copyStatus != RES_COPY_NONE)
	{
		Tcl_SetResult(interp, "Attempt to query while COPY in progress", TCL_STATIC);
		return TCL_ERROR;
	}

	execString = Tcl_GetStringFromObj(objv[2], NULL);

	/* we could call PQexecParams when nParams is 0, but PQexecParams
	 * will not accept more than one SQL statement per call, while
	 * PQexec will.  by checking and using PQexec when no parameters
	 * are included, we maintain compatibility for code that doesn't
	 * use params and might have had multiple statements in a single 
	 * request */
#ifdef HAVE_PQEXECPARAMS
	if (nParams == 0) {
#endif
	    result = PQexec(conn, execString);
#ifdef HAVE_PQEXECPARAMS
	} else {
	    result = PQexecParams(conn, execString, nParams, NULL, paramValues, NULL, NULL, 1);
	}
#endif
//result = PQexecPrepared(conn, statementNameString, nParams, paramValues, NULL, NULL, 1);

	/* REPLICATED IN pg_exec_prepared -- NEEDS TO BE FACTORED */
	/* Transfer any notify events from libpq to Tcl event queue. */
	PgNotifyTransferEvents(connid);

	if (result)
	{
		int	rId = PgSetResultId(interp, connString, result);

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
		Tcl_SetObjResult(interp, Tcl_NewStringObj(PQerrorMessage(conn), -1));
		return TCL_ERROR;
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

	int         nParams;

    /* THIS CODE IS REPLICATED IN Pg_sendquery_prepared AND NEEDS TO BE FACTORED */
#ifndef HAVE_PQEXECPREPARED
	Tcl_AppendResult(interp, "function unavailable with this version of the postgres libpq library", 0);
	return TCL_ERROR;
#else
	if (objc < 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection statementName [parm...]");
		return TCL_ERROR;
	}

	/* extra params will substitute for $1, $2, etc, in the statement */
	/* objc must be 3 or greater at this point */
	nParams = objc - 3;

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
		paramValues[param] = Tcl_GetStringFromObj (objv[3+param], NULL);
	    }
	}

	/* figure out the connect string and get the connection ID */

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (connid->res_copyStatus != RES_COPY_NONE)
	{
		Tcl_SetResult(interp, "Attempt to query while COPY in progress", TCL_STATIC);
		return TCL_ERROR;
	}

	statementNameString = Tcl_GetStringFromObj(objv[2], NULL);

	result = PQexecPrepared(conn, statementNameString, nParams, paramValues, NULL, NULL, 1);

	/* REPLICATED IN pg_exec -- NEEDS TO BE FACTORED */
	/* Transfer any notify events from libpq to Tcl event queue. */
	PgNotifyTransferEvents(connid);

	if (result)
	{
		int	rId = PgSetResultId(interp, connString, result);

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
		Tcl_SetObjResult(interp, Tcl_NewStringObj(PQerrorMessage(conn), -1));
		return TCL_ERROR;
	}
#endif /* HAVE_PQEXECPREPARED */
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

	-assignbyidx arrayName ?appendstr?
		assign the results to an array using the first field's value
		as a key.
		All but the first field of each tuple are stored, using
		subscripts of the form (field0value,attributeNameappendstr)

	-getTuple tupleNumber
		returns the values of the tuple in a list

	-tupleArray tupleNumber arrayName
		stores the values of the tuple in array arrayName, indexed
		by the attributes returned

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

 **********************************/
int
Pg_result(ClientData cData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	PGresult   *result;
	int			i;
	int			tupno;
	CONST84 char	   *arrVar;
	Tcl_Obj    *arrVarObj;
	Tcl_Obj    *appendstrObj;
	char	   *queryResultString;
	int			optIndex;
	int			errorOptIndex;

	Tcl_Obj* listObj;
	Tcl_Obj* subListObj;
	Tcl_Obj* fieldObj;

	static CONST char *options[] = {
		"-status", "-error", "-conn", "-oid",
		"-numTuples", "-cmdTuples", "-numAttrs", "-assign", "-assignbyidx",
		"-getTuple", "-tupleArray", "-attributes", "-lAttributes",
		"-clear", "-list", "-llist", (char *)NULL
	};

	enum options
	{
		OPT_STATUS, OPT_ERROR, OPT_CONN, OPT_OID,
		OPT_NUMTUPLES, OPT_CMDTUPLES, OPT_NUMATTRS, OPT_ASSIGN, OPT_ASSIGNBYIDX,
		OPT_GETTUPLE, OPT_TUPLEARRAY, OPT_ATTRIBUTES, OPT_LATTRIBUTES,
		OPT_CLEAR, OPT_LIST, OPT_LLIST
	};

	static CONST char *errorOptions[] = {
		"severity", "sqlstate", "primary", "detail",
		"hint", "position", "context", "file", "line",
		"function", (char *)NULL
	};

	static CONST char pgDiagCodes[] = {
		PG_DIAG_SEVERITY,
		PG_DIAG_SQLSTATE, 
		PG_DIAG_MESSAGE_PRIMARY,
		PG_DIAG_MESSAGE_DETAIL, 
		PG_DIAG_MESSAGE_HINT, 
		PG_DIAG_STATEMENT_POSITION, 
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
	queryResultString = Tcl_GetStringFromObj(objv[1], NULL);
	result = PgGetResultId(interp, queryResultString);
	if (result == (PGresult *)NULL)
	{
		Tcl_AppendResult(interp, "\n", queryResultString,
						 " is not a valid query result", (char *)NULL);
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

				Tcl_SetStringObj(Tcl_GetObjResult(interp), 
				    PQresultErrorField (result, pgDiagCodes[errorOptIndex]), -1);

				return TCL_OK;
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

				Tcl_SetLongObj(Tcl_GetObjResult(interp), PQoidValue(result));
				return TCL_OK;
			}

		case OPT_CLEAR:
			{
				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				PgDelResultId(interp, queryResultString);
				PQclear(result);
				return TCL_OK;
			}

		case OPT_NUMTUPLES:
			{
				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				Tcl_SetIntObj(Tcl_GetObjResult(interp), PQntuples(result));
				return TCL_OK;
			}
		case OPT_CMDTUPLES:
			{
				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				Tcl_SetStringObj(Tcl_GetObjResult(interp), PQcmdTuples(result), -1);
				return TCL_OK;
			}

		case OPT_NUMATTRS:
			{
				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				Tcl_SetIntObj(Tcl_GetObjResult(interp), PQnfields(result));
				return TCL_OK;
			}

		case OPT_ASSIGN:
			{
				Tcl_Obj    *fieldNameObj;

				if (objc != 4)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "arrayName");
					return TCL_ERROR;
				}

				arrVarObj = objv[3];
				arrVar = Tcl_GetStringFromObj(arrVarObj, NULL);
				fieldNameObj = Tcl_NewObj ();

				/*
				 * this assignment assigns the table of result tuples into
				 * a giant array with the name given in the argument. The
				 * indices of the array are of the form (tupno,attrName).
				 */
				for (tupno = 0; tupno < PQntuples(result); tupno++)
				{
					for (i = 0; i < PQnfields(result); i++)
					{

						/*
						 * construct the array element name consisting
						 * of the tuple number, a comma, and the field
						 * name.
						 * this is a little kludgey -- we set the obj
						 * to an int but the append following will force a
						 * string conversion.
						 */
						Tcl_SetIntObj(fieldNameObj, tupno);
						Tcl_AppendToObj(fieldNameObj, ",", 1);
						Tcl_AppendToObj(fieldNameObj, PQfname(result, i), -1);


						if (Tcl_ObjSetVar2(interp, arrVarObj, fieldNameObj,
										   Tcl_NewStringObj(
								 tcl_value(PQgetvalue(result, tupno, i)),
										 -1), TCL_LEAVE_ERR_MSG) == NULL) {
							Tcl_DecrRefCount (fieldNameObj);
							return TCL_ERROR;
						}
					}
				}
				Tcl_DecrRefCount (fieldNameObj);
				return TCL_OK;
			}

		case OPT_ASSIGNBYIDX:
			{
				Tcl_Obj *fieldNameObj = Tcl_NewObj();

				if ((objc != 4) && (objc != 5))
				{
					Tcl_WrongNumArgs(interp, 3, objv, "arrayName ?append_string?");
					Tcl_DecrRefCount(fieldNameObj);
					return TCL_ERROR;
				}

				arrVarObj = objv[3];
				arrVar = Tcl_GetStringFromObj(arrVarObj, NULL);

				if (objc == 5)
					appendstrObj = objv[4];
				else
					appendstrObj = NULL;

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
					const char *field0 = PQgetvalue(result, tupno, 0);

					for (i = 1; i < PQnfields(result); i++)
					{
						Tcl_SetStringObj(fieldNameObj, field0, -1);
						Tcl_AppendToObj(fieldNameObj, ",", 1);
						Tcl_AppendToObj(fieldNameObj, PQfname(result, i), -1);

						if (appendstrObj != NULL)
							Tcl_AppendObjToObj(fieldNameObj, appendstrObj);

						if (Tcl_ObjSetVar2(interp, arrVarObj, fieldNameObj,
										   Tcl_NewStringObj( tcl_value(PQgetvalue(result, tupno, i)), -1), TCL_LEAVE_ERR_MSG) == NULL)
						{
                            
							Tcl_DecrRefCount(fieldNameObj);
							return TCL_ERROR;
						}
					}
				}
				Tcl_DecrRefCount(fieldNameObj);
				return TCL_OK;
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
					Tcl_AppendResult(interp, "argument to getTuple cannot exceed number of tuples - 1", 0);
					return TCL_ERROR;
				}

				/* set the result object to be an empty list */
				resultObj = Tcl_GetObjResult(interp);
				Tcl_SetListObj(resultObj, 0, NULL);

				/* build up a return list, Tcl-object-style */
				for (i = 0; i < PQnfields(result); i++)
				{
					char	   *value;

					value = tcl_value(PQgetvalue(result, tupno, i));
					if (Tcl_ListObjAppendElement(interp, resultObj,
							   Tcl_NewStringObj(value, -1)) == TCL_ERROR)
						return TCL_ERROR;
				}
				return TCL_OK;
			}

		case OPT_TUPLEARRAY:
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
					Tcl_AppendResult(interp, "argument to tupleArray cannot exceed number of tuples - 1", 0);
					return TCL_ERROR;
				}

				arrayName = Tcl_GetStringFromObj(objv[4], NULL);

				for (i = 0; i < PQnfields(result); i++)
				{
					if (Tcl_SetVar2(interp, arrayName, PQfname(result, i),
								 tcl_value(PQgetvalue(result, tupno, i)),
									TCL_LEAVE_ERR_MSG) == NULL)
						return TCL_ERROR;
				}
				return TCL_OK;
			}

		case OPT_ATTRIBUTES:
			{
				Tcl_Obj    *resultObj = Tcl_GetObjResult(interp);

				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				Tcl_SetListObj(resultObj, 0, NULL);

				for (i = 0; i < PQnfields(result); i++)
				{
					Tcl_ListObjAppendElement(interp, resultObj,
							   Tcl_NewStringObj(PQfname(result, i), -1));
				}
				return TCL_OK;
			}

		case OPT_LATTRIBUTES:
			{
				Tcl_Obj    *resultObj = Tcl_GetObjResult(interp);

				if (objc != 3)
				{
					Tcl_WrongNumArgs(interp, 3, objv, "");
					return TCL_ERROR;
				}

				Tcl_SetListObj(resultObj, 0, NULL);

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
				return TCL_OK;
			}

		case OPT_LIST: 
		{
 	
			listObj = Tcl_NewListObj(0, (Tcl_Obj **) NULL);

			/*
			**	Loop through the tuple, and append each 
			**	attribute to the list
			**
			**	This option appends all of the attributes
			**	for each tuple to the same list
			*/
			for (tupno = 0; tupno < PQntuples(result); tupno++)
			{
				subListObj = Tcl_NewListObj(0, (Tcl_Obj **) NULL);

				/*
				**	Loop over the attributes for the tuple, 
				**	and append them to the list
				*/
				for (i = 0; i < PQnfields(result); i++)
				{
				    fieldObj = Tcl_NewObj();

				    Tcl_SetStringObj(fieldObj, PQgetvalue(result, tupno, i), -1);
				    if (Tcl_ListObjAppendElement(interp, subListObj, fieldObj) != TCL_OK)
					{
						Tcl_DecrRefCount(listObj);
						Tcl_DecrRefCount(fieldObj);
						return TCL_ERROR;
					}
	
				}
				if (Tcl_ListObjAppendList(interp, listObj, subListObj) != TCL_OK)
				{
					Tcl_DecrRefCount(listObj);
					Tcl_DecrRefCount(fieldObj);
					return TCL_ERROR;
				}
			}
	
			Tcl_SetObjResult(interp, listObj);
			return TCL_OK;

		}
		case OPT_LLIST: 
		{
			listObj = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
	
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

					Tcl_SetStringObj(fieldObj, PQgetvalue(result, tupno, i), -1);
	
					if (Tcl_ListObjAppendElement(interp, subListObj, fieldObj) != TCL_OK)
					{
						Tcl_DecrRefCount(listObj);
						Tcl_DecrRefCount(fieldObj);
						return TCL_ERROR;
					}
	
				}
				if (Tcl_ListObjAppendElement(interp, listObj, subListObj) != TCL_OK)
				{
					Tcl_DecrRefCount(listObj);
					Tcl_DecrRefCount(fieldObj);
					return TCL_ERROR;
				}
			}
	
			Tcl_SetObjResult(interp, listObj);
			return TCL_OK;
		}

		default:
			{
				Tcl_AppendResult(interp, "Invalid option\n", 0);
				goto Pg_result_errReturn;		/* append help info */
			}
	}

Pg_result_errReturn:
	Tcl_AppendResult(interp,
					 "pg_result result ?option? where option is\n",
					 "\t-status\n",
					 "\t-error\n",
					 "\t-conn\n",
					 "\t-oid\n",
					 "\t-numTuples\n",
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
					 (char *)0);
	return TCL_ERROR;
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
	CONST84 char	   *array_varname = NULL;
	char	   *arg;
	char	   *connString;
	char	   *queryString;

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
		arg = Tcl_GetStringFromObj(objv[i], NULL);
		if (arg[0] != '-')
			break;

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

			array_varname = Tcl_GetStringFromObj(objv[i++], NULL);
			continue;
		}

		arg = Tcl_GetStringFromObj(objv[i], NULL);

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
	connString = Tcl_GetStringFromObj(objv[i++], NULL);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (connid->res_copyStatus != RES_COPY_NONE)
	{
		Tcl_SetResult(interp, "Attempt to query while COPY in progress", TCL_STATIC);
		return TCL_ERROR;
	}

	/*
	 * Execute the query
	 */
	queryString = Tcl_GetStringFromObj(objv[i++], NULL);
	result = PQexec(conn, queryString);

	/*
	 * Transfer any notify events from libpq to Tcl event queue.
	 */
	PgNotifyTransferEvents(connid);

	/*
	 * Check for errors
	 */
	if (result == NULL)
	{
		Tcl_SetResult(interp, PQerrorMessage(conn), TCL_VOLATILE);
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
			resultObj = Tcl_GetObjResult(interp);
			Tcl_SetListObj(resultObj, 0, NULL);
			if (Tcl_ListObjAppendElement(interp, resultObj,
			   Tcl_NewStringObj(PQresStatus(PQresultStatus(result)), -1))
				== TCL_ERROR)
				return TCL_ERROR;

			if (Tcl_ListObjAppendElement(interp, resultObj,
					  Tcl_NewStringObj(PQresultErrorMessage(result), -1))
				== TCL_ERROR)
				return TCL_ERROR;

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
			if (execute_put_values(interp, array_varname, result, 0) != TCL_OK)
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
		if (execute_put_values(interp, array_varname, result, tupno) != TCL_OK)
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
execute_put_values(Tcl_Interp *interp, CONST84 char *array_varname,
				   PGresult *result, int tupno)
{
	int			i;
	int			n;
	char	   *fname;
	char	   *value;

	/*
	 * For each column get the column name and value and put it into a Tcl
	 * variable (either scalar or array item)
	 */
	n = PQnfields(result);
	for (i = 0; i < n; i++)
	{
		fname = PQfname(result, i);
		value = PQgetvalue(result, tupno, i);

		if (array_varname != NULL)
		{
			if (Tcl_SetVar2(interp, array_varname, fname, value,
							TCL_LEAVE_ERR_MSG) == NULL)
				return TCL_ERROR;
		}
		else
		{
			if (Tcl_SetVar(interp, fname, value, TCL_LEAVE_ERR_MSG) == NULL)
				return TCL_ERROR;
		}
	}
	return TCL_OK;
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

	if (objc != 4)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection lobjOid mode");
		return TCL_ERROR;
	}

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString,  NULL);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &lobjId) == TCL_ERROR)
		return TCL_ERROR;

	modeString = Tcl_GetStringFromObj(objv[3], &modeStringLen);
	if ((modeStringLen < 1) || (modeStringLen > 2))
	{
		Tcl_AppendResult(interp, "mode argument must be 'r', 'w', or 'rw'", 0);
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
			Tcl_AppendResult(interp, "mode argument must be 'r', 'w', or 'rw'", 0);
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
			Tcl_AppendResult(interp, "mode argument must be 'r', 'w', or 'rw'", 0);
			return TCL_ERROR;
	}

	fd = lo_open(conn, lobjId, mode);
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

	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection fd");
		return TCL_ERROR;
	}

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString, NULL);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &fd) != TCL_OK)
		return TCL_ERROR;

	Tcl_SetObjResult(interp, Tcl_NewIntObj(lo_close(conn, fd)));
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

	if (objc != 5)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn fd bufVar len");
		return TCL_ERROR;
	}

	conn = PgGetConnectionId(interp, Tcl_GetStringFromObj(objv[1], NULL),
							 NULL);
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
        if (nbytes >= 0)
        {
            #if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION >= 1 || TCL_MAJOR_VERSION > 8
	        bufObj = Tcl_NewByteArrayObj(buf, nbytes);
            #else
	        bufObj = Tcl_NewStringObj(buf, nbytes);
            #endif

	    if (Tcl_ObjSetVar2(interp, bufVar, NULL, bufObj,
					   TCL_LEAVE_ERR_MSG | TCL_PARSE_PART1) == NULL)
		rc = TCL_ERROR;
        }
   
        if (rc == TCL_OK)
		Tcl_SetObjResult(interp, Tcl_NewIntObj(nbytes));

	ckfree(buf);
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

	if (objc != 5)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn fd buf len");
		return TCL_ERROR;
	}

	conn = PgGetConnectionId(interp, Tcl_GetStringFromObj(objv[1], NULL),
							 NULL);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &fd) != TCL_OK)
		return TCL_ERROR;

	buf = Tcl_GetByteArrayFromObj(objv[3], &nbytes);

	if (Tcl_GetIntFromObj(interp, objv[4], &len) != TCL_OK)
		return TCL_ERROR;

	if (len > nbytes)
		len = nbytes;

	if (len <= 0)
	{
		Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
		return TCL_OK;
	}

	nbytes = lo_write(conn, fd, buf, len);
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

	if (objc != 5)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn fd offset whence");
		return TCL_ERROR;
	}

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString, NULL);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &fd) != TCL_OK)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[3], &offset) == TCL_ERROR)
		return TCL_ERROR;

	whenceStr = Tcl_GetStringFromObj(objv[4], NULL);

	if (strcmp(whenceStr, "SEEK_SET") == 0)
		whence = SEEK_SET;
	else if (strcmp(whenceStr, "SEEK_CUR") == 0)
		whence = SEEK_CUR;
	else if (strcmp(whenceStr, "SEEK_END") == 0)
		whence = SEEK_END;
	else
	{
		Tcl_AppendResult(interp, "'whence' must be SEEK_SET, SEEK_CUR or SEEK_END", 0);
		return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(lo_lseek(conn, fd, offset, whence)));
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

	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn mode");
		return TCL_ERROR;
	}

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString, NULL);
	if (conn == NULL)
		return TCL_ERROR;

	modeStr = Tcl_GetStringFromObj(objv[2], NULL);

	modeWord = strtok(modeStr, "|");
	if (strcmp(modeWord, "INV_READ") == 0)
		mode = INV_READ;
	else if (strcmp(modeWord, "INV_WRITE") == 0)
		mode = INV_WRITE;
	else
	{
		Tcl_AppendResult(interp,
						 "mode must be some OR'd combination of INV_READ, and INV_WRITE", 0);
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
			Tcl_AppendResult(interp,
							 "mode must be some OR'd combination of INV_READ, INV_WRITE", 0);
			return TCL_ERROR;
		}
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(lo_creat(conn, mode)));
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

	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn fd");
		return TCL_ERROR;
	}

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString, NULL);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &fd) != TCL_OK)
		return TCL_ERROR;

	Tcl_SetObjResult(interp, Tcl_NewIntObj(lo_tell(conn, fd)));
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

	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn fd");
		return TCL_ERROR;
	}

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString, NULL);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &lobjId) == TCL_ERROR)
		return TCL_ERROR;

	retval = lo_unlink(conn, lobjId);
	if (retval == -1)
	{
            Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
                "unlink of '", lobjId, "' failed", (char *) NULL);
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

	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn filename");
		return TCL_ERROR;
	}

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString, NULL);
	if (conn == NULL)
		return TCL_ERROR;

	filename = Tcl_GetStringFromObj(objv[2], NULL);

	lobjId = lo_import(conn, filename);
	if (lobjId == InvalidOid)
	{
		Tcl_AppendResult(interp,
						 "import of '", filename, "' failed", NULL);
		return TCL_ERROR;
	}

	Tcl_SetLongObj(Tcl_GetObjResult(interp), (long)lobjId);
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

	if (objc != 4)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "conn lobjId filename");
		return TCL_ERROR;
	}

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString, NULL);
	if (conn == NULL)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &lobjId) == TCL_ERROR)
		return TCL_ERROR;

	filename = Tcl_GetStringFromObj(objv[3], NULL);

	retval = lo_export(conn, lobjId, filename);
	if (retval == -1)
	{
		Tcl_AppendResult(interp, "export failed", 0);
		return TCL_ERROR;
	}
	return TCL_OK;
}

/**********************************
 * pg_select
 send a select query string to the backend connection

 syntax:
 pg_select connection query var proc

 The query must be a select statement
 The var is used in the proc as an array
 The proc is run once for each row found

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
	PGconn	   *conn;
	PGresult   *result;
	int			r,
				retval;
	int			tupno,
				column,
				ncols;
	char	   *connString;
	char	   *queryString;
	char	   *varNameString;
	Tcl_Obj    *varNameObj;
	Tcl_Obj    *procStringObj;
	Tcl_Obj    *columnListObj;
	Tcl_Obj   **columnNameObjs;

	if (objc != 5)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection queryString var proc");
		return TCL_ERROR;
	}

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	queryString = Tcl_GetStringFromObj(objv[2], NULL);

	varNameObj = objv[3];
	varNameString = Tcl_GetStringFromObj(varNameObj, NULL);

	procStringObj = objv[4];

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if ((result = PQexec(conn, queryString)) == 0)
	{
		/* error occurred sending the query */
		Tcl_SetResult(interp, PQerrorMessage(conn), TCL_VOLATILE);
		return TCL_ERROR;
	}

	/* Transfer any notify events from libpq to Tcl event queue. */
	PgNotifyTransferEvents(connid);

	if (PQresultStatus(result) != PGRES_TUPLES_OK)
	{
		/* query failed, or it wasn't SELECT */
		Tcl_SetResult(interp, (char *)PQresultErrorMessage(result),
					  TCL_VOLATILE);
		PQclear(result);
		return TCL_ERROR;
	}

	ncols = PQnfields(result);
	columnNameObjs = (Tcl_Obj **)ckalloc(sizeof(Tcl_Obj) * ncols);

	for (column = 0; column < ncols; column++)
		columnNameObjs[column] = Tcl_NewStringObj(PQfname(result, column), -1);

	columnListObj = Tcl_NewListObj(ncols, columnNameObjs);

	Tcl_SetVar2Ex(interp, varNameString, ".headers", columnListObj, 0);
	Tcl_SetVar2Ex(interp, varNameString, ".numcols", Tcl_NewIntObj(ncols), 0);

	retval = TCL_OK;

	for (tupno = 0; tupno < PQntuples(result); tupno++)
	{
		Tcl_SetVar2Ex(interp, varNameString, ".tupno", Tcl_NewIntObj(tupno), 0);

		for (column = 0; column < ncols; column++)
		{
			Tcl_Obj    *valueObj;

			valueObj = Tcl_NewStringObj(tcl_value(PQgetvalue(result, tupno, column)), -1);
			Tcl_ObjSetVar2(interp, varNameObj, columnNameObjs[column],
						   valueObj,
						   0);
		}

		Tcl_SetVar2(interp, varNameString, ".command", "update", 0);

		r = Tcl_EvalObjEx(interp, procStringObj, 0);
		if ((r != TCL_OK) && (r != TCL_CONTINUE))
		{
			if (r == TCL_BREAK)
				break;			/* exit loop, but return TCL_OK */

			if (r == TCL_ERROR)
			{
				char		msg[60];

				sprintf(msg, "\n    (\"pg_select\" body line %d)",
						interp->errorLine);
				Tcl_AddErrorInfo(interp, msg);
			}

			retval = r;
			break;
		}
	}

	ckfree((void *)columnNameObjs);
	Tcl_UnsetVar(interp, varNameString, 0);
	PQclear(result);
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
	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

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
			ckfree((char *)Tcl_GetHashValue(entry));

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
			ckfree(cmd);
			/* Transfer any notify events from libpq to Tcl event queue. */
			PgNotifyTransferEvents(connid);
			if (PQresultStatus(result) != PGRES_COMMAND_OK)
			{
				/* Error occurred during the execution of command */
				PQclear(result);
				Tcl_DeleteHashEntry(entry);
				ckfree(callback);
				ckfree(caserelname);
				Tcl_SetResult(interp, PQerrorMessage(conn), TCL_VOLATILE);
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
			Tcl_AppendResult(interp, "not listening on ", origrelname, 0);
			ckfree(caserelname);
			return TCL_ERROR;
		}
		ckfree((char *)Tcl_GetHashValue(entry));
		Tcl_DeleteHashEntry(entry);

		/*
		 * Send an UNLISTEN command if that was the last listener. Note:
		 * we don't attempt to turn off the notify mechanism if no LISTENs
		 * remain active; not worth the trouble.
		 */
		if (!Pg_have_listener(connid, caserelname))
		{
			char	   *cmd = (char *)
			ckalloc((unsigned)(origrelnameStrlen + 10));

			sprintf(cmd, "UNLISTEN %s", origrelname);
			result = PQexec(conn, cmd);
			ckfree(cmd);
			/* Transfer any notify events from libpq to Tcl event queue. */
			PgNotifyTransferEvents(connid);
			if (PQresultStatus(result) != PGRES_COMMAND_OK)
			{
				/* Error occurred during the execution of command */
				PQclear(result);
				ckfree(caserelname);
				Tcl_SetResult(interp, PQerrorMessage(conn), TCL_VOLATILE);
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
	PGconn	   *conn;
	char	   *connString;
	char	   *execString;
	int			status;

	/* THIS CODE IS REPLICATED IN Pg_exec AND SHOULD BE FACTORED */
#ifdef HAVE_PQSENDQUERYPARAMS
	int         nParams;
	const char **paramValues = NULL;

	if (objc < 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection queryString [parm...]");
		return TCL_ERROR;
	}

	/* extra params will substitute for $1, $2, etc, in the statement */
	/* objc must be 3 or greater at this point */
	nParams = objc - 3;

	/* If there are any extra params, allocate paramValues and fill it
	 * with the string representations of all of the extra parameters
	 * substituted on the command line.  Otherwise nParams will be 0,
	 * and PQexecParams will work just like PQexec (no $-substitutions).
	 */
	if (nParams > 0) {
	    int param;

	    paramValues = (const char **)ckalloc (nParams * sizeof (char *));

	    for (param = 0; param < nParams; param++) {
		paramValues[param] = Tcl_GetStringFromObj (objv[3+param], NULL);
	    }
	}
#else /* HAVE_PQSENDQUERYPARAMS */
	if (objc != 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection queryString");
		return TCL_ERROR;
	}
#endif /* HAVE_PQSENDQUERYPARAMS */

	connString = Tcl_GetStringFromObj(objv[1], NULL);

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (connid->res_copyStatus != RES_COPY_NONE)
	{
		Tcl_SetResult(interp, "Attempt to query while COPY in progress", TCL_STATIC);
		return TCL_ERROR;
	}

	execString = Tcl_GetStringFromObj(objv[2], NULL);

#ifdef HAVE_PQSENDQUERYPARAMS
	if (nParams == 0) {
#endif
		status = PQsendQuery(conn, execString);
#ifdef HAVE_PQSENDQUERYPARAMS
	} else {
	    status = PQsendQueryParams(conn, execString, nParams, NULL, paramValues, NULL, NULL, 1);
	}
#endif

	/* Transfer any notify events from libpq to Tcl event queue. */
	PgNotifyTransferEvents(connid);

	if (status)
		return TCL_OK;
	else
	{
		/* error occurred during the query */
		Tcl_SetObjResult(interp, Tcl_NewStringObj(PQerrorMessage(conn), -1));
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

    /* THIS CODE IS REPLICATED IN Pg_exec_prepared AND NEEDS TO BE FACTORED */
#ifndef HAVE_PQSENDQUERYPREPARED
	Tcl_AppendResult(interp, "function unavailable with this version of the postgres libpq library", 0);
	return TCL_ERROR;
#else /* HAVE_PQSENDQUERYPREPARED */
	if (objc < 3)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection statementName [parm...]");
		return TCL_ERROR;
	}

	/* extra params will substitute for $1, $2, etc, in the statement */
	/* objc must be 3 or greater at this point */
	nParams = objc - 3;

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
		paramValues[param] = Tcl_GetStringFromObj (objv[3+param], NULL);
	    }
	}

	/* figure out the connect string and get the connection ID */

	connString = Tcl_GetStringFromObj(objv[1], NULL);
	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (connid->res_copyStatus != RES_COPY_NONE)
	{
		Tcl_SetResult(interp, "Attempt to query while COPY in progress", TCL_STATIC);
		return TCL_ERROR;
	}

	statementNameString = Tcl_GetStringFromObj(objv[2], NULL);

	status = PQsendQueryPrepared(conn, statementNameString, nParams, paramValues, NULL, NULL, 1);

	/* Transfer any notify events from libpq to Tcl event queue. */
	PgNotifyTransferEvents(connid);

	if (status)
		return TCL_OK;
	else
	{
		/* error occurred during the query */
		Tcl_SetObjResult(interp, Tcl_NewStringObj(PQerrorMessage(conn), -1));
		return TCL_ERROR;
	}
#endif /* HAVE_PQSENDQUERYPREPARED */
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

	connString = Tcl_GetStringFromObj(objv[1], NULL);

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	result = PQgetResult(conn);

	/* Transfer any notify events from libpq to Tcl event queue. */
	PgNotifyTransferEvents(connid);

	/* if there's a non-null result, give the caller the handle */
	if (result)
	{
		int			rId = PgSetResultId(interp, connString, result);

		ExecStatusType rStat = PQresultStatus(result);

		if (rStat == PGRES_COPY_IN || rStat == PGRES_COPY_OUT)
		{
			connid->res_copyStatus = RES_COPY_INPROGRESS;
			connid->res_copy = rId;
		}
	}
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

	connString = Tcl_GetStringFromObj(objv[1], NULL);

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	PQconsumeInput(conn);

	Tcl_SetIntObj(Tcl_GetObjResult(interp), PQisBusy(conn));
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

	connString = Tcl_GetStringFromObj(objv[1], NULL);

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (objc == 2)
	{
		Tcl_SetBooleanObj(Tcl_GetObjResult(interp), !PQisnonblocking(conn));
		return TCL_OK;
	}

	/* objc == 3, they're setting it */
	if (Tcl_GetBooleanFromObj(interp, objv[2], &boolean) == TCL_ERROR)
		return TCL_ERROR;

	PQsetnonblocking(conn, !boolean);
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
	PGresult   *result;
	char	   *connString;

	if (objc != 2)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "connection");
		return TCL_ERROR;
	}

	connString = Tcl_GetStringFromObj(objv[1], NULL);

	conn = PgGetConnectionId(interp, connString, &connid);
	if (conn == NULL)
		return TCL_ERROR;

	if (PQrequestCancel(conn) == 0)
	{
		Tcl_SetObjResult(interp,
					 Tcl_NewStringObj(PQresultErrorMessage(result), -1));
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
	connString = Tcl_GetStringFromObj(objv[1], NULL);
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

/***********************************
Pg_quote
	escape string for inclusion in SQL queries

 syntax:
   pg_quote string

***********************************/
int
Pg_quote(ClientData cData, Tcl_Interp *interp, int objc,
				 Tcl_Obj *CONST objv[])
{
	char	   *fromString;
	char	   *toString;
	int         fromStringLen;
	int         stringSize;

	if (objc != 2)
	{
		Tcl_WrongNumArgs(interp, 1, objv, "string");
		return TCL_ERROR;
	}

	/*
	 * Get the "from" string.
	 */
	fromString = Tcl_GetStringFromObj(objv[1], &fromStringLen);

	/* 
	 * allocate the "to" string, max size is documented in the
	 * postgres docs as 2 * fromStringLen + 1, we add two more
	 * for the leading and trailing single quotes
	 */
	toString = (char *) ckalloc((2 * fromStringLen) + 3);

	/*
	 * call the library routine to escape the string, use
	 * Tcl_SetResult to set the command result to be that string,
	 * with TCL_DYNAMIC, we tell Tcl to free the memory when it's
	 * done with it */
	 *toString = '\'';
	stringSize = PQescapeString (toString+1, fromString, fromStringLen);
	toString[stringSize+1] = '\'';
	toString[stringSize+2] = '\0';
	Tcl_SetResult(interp, toString, TCL_DYNAMIC);
	return TCL_OK;
}
/***********************************
Pg_conninfo

 syntax:
***********************************/
int
Pg_conninfo(ClientData cData, Tcl_Interp *interp, int objc,
				 Tcl_Obj *CONST objv[])
{

    Pg_ConnectionId *connid;
    Tcl_Obj   *names;
    Tcl_Obj   **elemPtrs;
    Tcl_Obj   *listObj;
    int       i, count, length;
    char     *tmp;
    Tcl_Channel conn_chan;
   
    listObj = Tcl_NewListObj(0, (Tcl_Obj **) NULL);

    Tcl_GetChannelNamesEx(interp, (char *) NULL);


    Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp), &count, &elemPtrs);


    for (i = 0; i < count; i++) {

        char *name = Tcl_GetStringFromObj(elemPtrs[i], NULL);

        conn_chan = Tcl_GetChannel(interp, name, 0);
        if (conn_chan != NULL && Tcl_GetChannelType(conn_chan) == &Pg_ConnType)
        {

        if (Tcl_ListObjAppendElement(interp, listObj, elemPtrs[i]) != TCL_OK)
        {
            Tcl_DecrRefCount(listObj);
            return TCL_ERROR;
        }
        }


    }

    Tcl_ResetResult(interp);
    Tcl_SetObjResult(interp, listObj);
    
    return TCL_OK;

}

/*
 *----------------------------------------------------------------------
 *
 * Pg_results --
 *
 *    returns the current result handles for a connection
 *
 * Syntax:
 *    pg_result conn 
 *
 * Results:
 *    the return result is either an error message or a list of
 *    the result handles for connection 'conn'.
 *
 *----------------------------------------------------------------------
 */
int
Pg_results(ClientData cData, Tcl_Interp *interp, int objc,
				 Tcl_Obj *CONST objv[])
{

    Pg_ConnectionId *connid;
    PGconn	    *conn;
    char	    *connString;
    char	    buf[32];
    Tcl_Channel     conn_chan;
    int             i;
    Tcl_Obj         *listObj;

    listObj = Tcl_NewListObj(0, (Tcl_Obj **) NULL);

    if (objc != 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "connection");
        return TCL_ERROR;
    }

    connString = Tcl_GetStringFromObj(objv[1], NULL);
    conn_chan = Tcl_GetChannel(interp, connString, 0);
    if (conn_chan == NULL)
    {
        Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "... not a valid connection", NULL);
        return TCL_ERROR;
    }


    /* Check that it is a PG connection and not something else */
    connid = (Pg_ConnectionId *) Tcl_GetChannelInstanceData(conn_chan);

    if (connid->conn == NULL)
        return TCL_ERROR;

    for (i = 0; i <= connid->res_last; i++)
    {
        sprintf(buf, "%s.%d", connString, i);
        if (Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(buf, -1)) != TCL_OK)
        {
            Tcl_DecrRefCount(listObj);
            return TCL_ERROR;
        }
    }

    Tcl_SetObjResult(interp, listObj);
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
