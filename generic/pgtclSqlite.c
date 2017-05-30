#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <libpq-fe.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>

#include "pgtclCmds.h"
#include "pgtclId.h"

#include <sqlite3.h>

#ifndef CONST84
#     define CONST84
#endif

// From tclsqlite.c:
/*
** There is one instance of this structure for each SQLite database
** that has been opened by the SQLite TCL interface.
**
** If this module is built with SQLITE_TEST defined (to create the SQLite
** testfixture executable), then it may be configured to use either
** sqlite3_prepare_v2() or sqlite3_prepare() to prepare SQL statements.
** If SqliteDb.bLegacyPrepare is true, sqlite3_prepare() is used.
*/
struct SqliteDb {
  sqlite3 *db;               /* The "real" database structure. MUST BE FIRST */
  // other stuff we don't look at, but probably should maybe use to validate...
};

int Pg_sqlite_execObj(Tcl_Interp *interp, sqlite3 *sqlite_db, Tcl_Obj *obj)
{
	sqlite3_stmt *statement = NULL;
	int           result = TCL_OK;
//fprintf(stderr, "DEBUG Pg_sqlite_execObj(Tcl_Interp *interp, sqlite3 *sqlite_db, Tcl_Obj *obj);\n");
//fprintf(stderr, "DEBUG obj = {%s};\n", Tcl_GetString(obj));

	if(sqlite3_prepare_v2(sqlite_db, Tcl_GetString(obj), -1, &statement, NULL) != SQLITE_OK) {
		Tcl_AppendResult(interp, sqlite3_errmsg(sqlite_db), (char *)NULL);
		statement = NULL; // probably redundant
		result = TCL_ERROR;
	} else if(sqlite3_step(statement) != SQLITE_DONE) {
		Tcl_AppendResult(interp, sqlite3_errmsg(sqlite_db), (char *)NULL);
		result = TCL_ERROR;
	}

	if(statement)
		sqlite3_finalize(statement);

	return result;
}

static Tcl_ObjCmdProc *sqlite3_ObjProc = NULL;

enum mappedTypes {
	PG_SQLITE_INT,
	PG_SQLITE_DOUBLE,
	PG_SQLITE_TEXT,
	PG_SQLITE_NOTYPE
};

struct {
	char *name;
	enum mappedTypes type;
} mappedTypes[] = {
	{"int",          PG_SQLITE_INT},
	{"double",       PG_SQLITE_DOUBLE},
	{"text",         PG_SQLITE_TEXT},
	{NULL,           PG_SQLITE_NOTYPE}
};

char *typenames[sizeof mappedTypes / sizeof *mappedTypes];

int
Pg_sqlite_mapTypes(Tcl_Interp *interp, Tcl_Obj *list, int start, int stride, enum mappedTypes **arrayPtr, int *lengthPtr)
{
	Tcl_Obj          **objv;
	int                objc;
	enum mappedTypes  *array;
	int                i;
	int                col;

	if(Tcl_ListObjGetElements(interp, list, &objc, &objv) != TCL_OK)
		return TCL_ERROR;

	if (stride > 1 && (objc % stride) != 0) {
		Tcl_AppendResult(interp, "List not an even length", (char *)NULL);
		return TCL_ERROR;
	}

	array = (enum mappedTypes *)ckalloc((sizeof *array) * (objc / stride));

	for(col = 0, i = start; i < objc; col++, i += stride) {
		char *typeName = Tcl_GetString(objv[i]);
		int   t;

		for(t = 0; mappedTypes[t].name; t++) {
			if(strcmp(typeName, mappedTypes[t].name) == 0) {
				typenames[mappedTypes[t].type] = mappedTypes[t].name;
				array[col] = mappedTypes[t].type;
				break;
			}
		}

		if(!mappedTypes[t].name) {
			ckfree(array);
			Tcl_AppendResult(interp, "Unknown type ", typeName, (char *)NULL);
			return TCL_ERROR;
		}
	}

	*arrayPtr = array;
	*lengthPtr = col;
	return TCL_OK;
}

int
Pg_sqlite_bindValue(sqlite3_stmt *statement, int column, char *value, enum mappedTypes type)
{
	switch(type) {
		case PG_SQLITE_INT: {
			if (sqlite3_bind_int(statement, column+1, atoi(value)) != SQLITE_OK)
				return TCL_ERROR;
			break;
		}
		case PG_SQLITE_DOUBLE: {
			if (sqlite3_bind_double(statement, column+1, atof(value)) != SQLITE_OK)
				return TCL_ERROR;
			break;
		}
		case PG_SQLITE_TEXT: {
			if (sqlite3_bind_text(statement, column+1, value, -1, SQLITE_TRANSIENT) != SQLITE_OK)
				return TCL_ERROR;
			break;
		}
		default: {
			return TCL_BREAK;
		}
	}
	return TCL_OK;
}

char *
Pg_sqlite_createTable(Tcl_Interp *interp, sqlite3 *sqlite_db, char *sqliteTable, Tcl_Obj *nameTypeList)
{
	Tcl_Obj          **objv;
	int                objc;
	Tcl_Obj *create = Tcl_NewObj();
	Tcl_Obj *sql = Tcl_NewObj();
	Tcl_Obj *values = Tcl_NewObj();
	int i;

	if(Tcl_ListObjGetElements(interp, nameTypeList, &objc, &objv) != TCL_OK)
		return NULL;

	if(objc & 1) {
		Tcl_AppendResult(interp, "List must have an even number of elements", (char *)NULL);
		return NULL;
	}

	Tcl_AppendStringsToObj(create, "CREATE TABLE ", sqliteTable, " (", (char *)NULL);

	Tcl_AppendStringsToObj(sql, "INSERT INTO ", sqliteTable, " (", (char *)NULL);

	for(i = 0; i < objc; i+= 2) {
		Tcl_AppendToObj(create, "\n\t", -1);
		Tcl_AppendObjToObj(create, objv[i]);
		Tcl_AppendToObj(create, " ", -1);
		Tcl_AppendObjToObj(create, objv[i+1]);
		if(i == 0)
			Tcl_AppendToObj(create, " PRIMARY KEY", -1);

		if(i < objc-2)
			Tcl_AppendToObj(create, ",", -1);

		if(i > 0)
			Tcl_AppendToObj(sql, ", ", -1);
		Tcl_AppendObjToObj(sql, objv[i]);

		if(i > 0)
			Tcl_AppendToObj(values, ", ", -1);
		Tcl_AppendToObj(values, "?", -1);
	}

	Tcl_AppendToObj(create, "\n);", -1);

	Tcl_AppendToObj(sql, ") VALUES (", -1);
	Tcl_AppendObjToObj(sql, values);
	Tcl_AppendToObj(sql, ");", -1);

	if(Pg_sqlite_execObj(interp, sqlite_db, create) != TCL_OK)
		return NULL;

	return Tcl_GetString(sql);
}

int
Pg_sqlite_dropTable(Tcl_Interp *interp, sqlite3 *sqlite_db, char *dropTable)
{
	Tcl_Obj *drop = Tcl_NewObj();

	Tcl_AppendStringsToObj(drop, "DROP TABLE ", dropTable, ";", (char *)NULL);

	return Pg_sqlite_execObj(interp, sqlite_db, drop);
}

int
sqlite_probe(Tcl_Interp *interp)
{
	if (sqlite3_ObjProc != NULL) return TCL_OK;

	char cmd_name[256];
	char create_cmd[256];
	char delete_cmd[256];
        struct Tcl_CmdInfo  cmd_info;
	
	snprintf(cmd_name, 256, "::dummy%d", getpid());
	snprintf(create_cmd, 256, "sqlite3 %s :memory:", cmd_name);
	snprintf(delete_cmd, 256, "%s close", cmd_name);

	if (Tcl_Eval(interp, create_cmd) != TCL_OK) {
		return TCL_ERROR;
	}

        if (!Tcl_GetCommandInfo(interp, cmd_name, &cmd_info)) {
                Tcl_AppendResult(interp, "pg_sqlite3 probe failed (", cmd_name, " not found)", (char *)NULL);
		Tcl_Eval(interp, delete_cmd);
                return TCL_ERROR;
        }

	if (!cmd_info.isNativeObjectProc) {
		Tcl_AppendResult(interp, "pg_sqlite2 probe failed (", cmd_name, " not a native object proc)", (char *)NULL);
		Tcl_Eval(interp, delete_cmd);
		return TCL_ERROR;
	}

	sqlite3_ObjProc = cmd_info.objProc;
	Tcl_Eval(interp, delete_cmd);

	if (!sqlite3_ObjProc) {
                Tcl_AppendResult(interp, "pg_sqlite2 probe failed (", cmd_name, " not a native object proc)", (char *)NULL);
		return TCL_ERROR;
	}

	return TCL_OK;
}

int
Pg_sqlite(ClientData clientdata, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
        char               *sqlite_commandName;
        struct Tcl_CmdInfo  sqlite_commandInfo;
        struct SqliteDb    *sqlite_clientData;
        sqlite3            *sqlite_db;
	int                 cmdIndex;

	static CONST84 char *subCommands[] = {
		"filename", "import_postgres_result", "write_tabsep", "read_tabsep", 
		(char *)NULL
	};

	enum subCommands
	{
		CMD_FILENAME, CMD_IMPORT_POSTGRES_RESULT, CMD_WRITE_TABSEP, CMD_READ_TABSEP,
		NUM_COMMANDS
	};

        if (objc <= 2) {
                Tcl_WrongNumArgs(interp, 1, objv, "sqlite_handle command ?args?");
                return TCL_ERROR;
        }

        sqlite_commandName = Tcl_GetString(objv[1]);

        if (!Tcl_GetCommandInfo(interp, sqlite_commandName, &sqlite_commandInfo)) {
                Tcl_AppendResult(interp, sqlite_commandName, " is not a command.", (char *)NULL);
                return TCL_ERROR;
        }

	if (sqlite_probe(interp) != TCL_OK) {
		return TCL_ERROR;
	}

	if (sqlite3_ObjProc != sqlite_commandInfo.objProc) {
		Tcl_AppendResult(interp, sqlite_commandName, " is not an sqlite3 handle.", (char *)NULL);
		return TCL_ERROR;
	}

        sqlite_clientData = (struct SqliteDb *)sqlite_commandInfo.objClientData;

        sqlite_db = sqlite_clientData->db;

	if (Tcl_GetIndexFromObj(interp, objv[2], subCommands, "command", TCL_EXACT, &cmdIndex) != TCL_OK)
		return TCL_ERROR;

	int minargs[NUM_COMMANDS] = { -1 };
	int incoming[NUM_COMMANDS] = { -1 };
	char *argerr[NUM_COMMANDS] = { "" };
	if(minargs[0] == -1) {
		int i;
		for(i = 0; i < NUM_COMMANDS; i++) {
			minargs[i] = 0;
			incoming[i] = 0;
			argerr[i] = "";
		}
		minargs[CMD_IMPORT_POSTGRES_RESULT] = 4;
		minargs[CMD_READ_TABSEP] = 4;
		minargs[CMD_WRITE_TABSEP] = 3;
		incoming[CMD_IMPORT_POSTGRES_RESULT] = 1;
		incoming[CMD_READ_TABSEP] = 1;
		argerr[CMD_READ_TABSEP] = "?-row tabsep_row? ?-from file_handle? ?-into new_table? ?-names name-list? ?-as name-type-list? ?-types type-list?";
		argerr[CMD_IMPORT_POSTGRES_RESULT] = "handle ?-sql sqlite_sql? ?-into new_table? ?-names name-list? ?-as name-type-list? ?-types type-list? ?-rowbyrow?";
	}

	// common variables
	char              *sqliteCode = NULL;
	char              *sqliteTable = NULL;
	char              *dropTable = NULL;
	sqlite3_stmt      *statement = NULL;
	int                optIndex = 4;
	Tcl_Obj           *typeList = NULL;
	Tcl_Obj           *nameTypeList = NULL;
	int                rowbyrow = 0;
	int                returnCode = TCL_OK;
	const char        *errorMessage = NULL;
	enum mappedTypes  *columnTypes;
	int                nColumns = -1;
	int                column;
	int                prepStatus;
	PGconn            *conn = NULL;
	PGresult          *result = NULL;
	ExecStatusType     status;

	// common code
	if(incoming[cmdIndex]) {
		optIndex = minargs[cmdIndex];

		if (objc < optIndex) {
		  common_wrong_num_args:
			Tcl_WrongNumArgs(interp, 3, objv, argerr[cmdIndex]);
			return TCL_ERROR;
		}

		while(optIndex < objc) {
			char *optName = Tcl_GetString(objv[optIndex]);
			optIndex++;
			if (optName[0] != '-')
				goto common_wrong_num_args;
			if (strcmp(optName, "-types") == 0) {
				typeList = objv[optIndex];
				optIndex++;
			} else if (strcmp(optName, "-as") == 0) {
				nameTypeList = objv[optIndex];
				optIndex++;
			} else if (strcmp(optName, "-sql") == 0) {
				sqliteCode = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (strcmp(optName, "-into") == 0) {
				sqliteTable = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex == CMD_IMPORT_POSTGRES_RESULT && strcmp(optName, "-rowbyrow") == 0) {
				rowbyrow = 1;
			} else
				goto common_wrong_num_args;
		}

		if (sqliteCode && sqliteTable) {
			Tcl_AppendResult(interp, "Can't use both -sql and -into", (char *)NULL);
			return TCL_ERROR;
		}

		if (typeList && nameTypeList) {
			Tcl_AppendResult(interp, "Can't use both -types and -as", (char *)NULL);
			return TCL_ERROR;
		}

		if(typeList) {
			if (Pg_sqlite_mapTypes(interp, typeList, 0, 1, &columnTypes, &nColumns) != TCL_OK)
				return TCL_ERROR;
		}

		if(sqliteTable) {
			if (!nameTypeList) {
				Tcl_AppendResult(interp, "No template (-as) provided for -into", (char *)NULL);
				return TCL_ERROR;
			}

			if (Pg_sqlite_mapTypes(interp, nameTypeList, 1, 2, &columnTypes, &nColumns) != TCL_OK)
				return TCL_ERROR;

			sqliteCode = Pg_sqlite_createTable(interp, sqlite_db, sqliteTable, nameTypeList);
			if (!sqliteCode) {
				if (columnTypes)
					ckfree(columnTypes);
				return TCL_ERROR;
			}
			dropTable = sqliteTable;
		}

		prepStatus = sqlite3_prepare_v2(sqlite_db, sqliteCode, -1, &statement, NULL);
		if(prepStatus != SQLITE_OK) {
			Tcl_AppendResult(interp, sqlite3_errmsg(sqlite_db), (char *)NULL);
			if(columnTypes)
				ckfree(columnTypes);
			if(dropTable)
				Pg_sqlite_dropTable(interp, sqlite_db, dropTable);
			return TCL_ERROR;
		}

	}

	switch (cmdIndex) {
		case CMD_FILENAME: {
			char       *sqlite_dbname;
			const char *sqlite_filename;

			if(objc == 3) {
				sqlite_dbname = "main";
			} else if (objc == 4) {
				sqlite_dbname = Tcl_GetString(objv[3]);
			} else {
				Tcl_WrongNumArgs(interp, 3, objv, "?dbname?");
				return TCL_ERROR;
			}

			sqlite_filename = sqlite3_db_filename(sqlite_db, sqlite_dbname);

			Tcl_AppendResult(interp, sqlite_filename, (char *)NULL);
			break;
		}

		//case CMD_READ_TABSEP: {
		//	FILE              *fp = NULL;
		//	char              *row = NULL;
		//}

		case CMD_IMPORT_POSTGRES_RESULT: {
			char *pghandle_name = Tcl_GetString(objv[3]);
			int   nTuples;
			int   totalTuples = 0;
			int   tupleIndex;
			int   stepStatus;

			if(rowbyrow) {
				conn = PgGetConnectionId(interp, pghandle_name, NULL);
				if(conn == NULL) {
					Tcl_AppendResult (interp, " while getting connection from ", pghandle_name, (char *)NULL);
					returnCode = TCL_ERROR;
					goto import_cleanup_and_exit;
				}
				PQsetSingleRowMode(conn);
				result = PQgetResult(conn);
			} else {
				result = PgGetResultId(interp, pghandle_name, NULL);
			}

			if(!result) {
				Tcl_AppendResult (interp, "Failed to get handle from ", pghandle_name, (char *)NULL);
				returnCode = TCL_ERROR;
				goto import_cleanup_and_exit;
			}

			while(result) {
				status = PQresultStatus(result);

				if(status != PGRES_TUPLES_OK && status != PGRES_SINGLE_TUPLE) {
					errorMessage = PQresultErrorMessage(result);
					if (!*errorMessage)
						errorMessage = PQresStatus(status);
					returnCode = TCL_ERROR;
					break;
				}

				nTuples = PQntuples(result);

				for (tupleIndex = 0; tupleIndex < nTuples; tupleIndex++) {
					totalTuples++;
					for(column = 0; column < nColumns; column++) {
						char *value = PQgetvalue(result, tupleIndex, column);
						switch (Pg_sqlite_bindValue(statement, column, value, columnTypes[column])) {
							case TCL_ERROR: {
								goto import_bailout;
							}
							case TCL_OK: {
								break;
							}
							default: {
								errorMessage = "INTERNAL ERROR invalid type code";
								returnCode = TCL_ERROR;
								goto import_loop_end;
							}
						}
					}

					stepStatus = sqlite3_step(statement);
					if (stepStatus != SQLITE_DONE) {
					  import_bailout:
						errorMessage = sqlite3_errmsg(sqlite_db);
						returnCode = TCL_ERROR;
						goto import_loop_end;
					}
					sqlite3_reset(statement);
					sqlite3_clear_bindings(statement);
				}

				if(rowbyrow) {
					PQclear(result);
					result = PQgetResult(conn);
				} else {
					result = NULL;
				}
			}

		  import_loop_end:

			if(rowbyrow) {
				while(result) {
					PQclear(result);
					result = PQgetResult(conn);
				}
			}

		  import_cleanup_and_exit:

			if(statement)
				sqlite3_finalize(statement);

			if(columnTypes)
				ckfree(columnTypes);

			if(returnCode == TCL_ERROR) {
				if (errorMessage) {
					Tcl_AppendResult(interp, (char *)errorMessage, (char *)NULL);
				}

				if(dropTable) {
					if(Pg_sqlite_dropTable(interp, sqlite_db, dropTable) != TCL_OK)
						Tcl_AppendResult(interp, " while handling error", (char *)NULL);
				}

				return TCL_ERROR;
			}

			Tcl_SetObjResult(interp, Tcl_NewIntObj(totalTuples));
			return returnCode;
		}
	}

	return TCL_OK;
}

