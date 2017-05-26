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

static Tcl_ObjCmdProc *sqlite3_ObjProc = NULL;

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

		case CMD_IMPORT_POSTGRES_RESULT: {
			if (objc < 4) {
			  import_wrong_num_args:
				Tcl_WrongNumArgs(interp, 3, objv, "handle ?-sql sqlite_sql? ?-into new_table? ?-names name-list? ?-as name-type-list? ?-types type-list? ?-rowbyrow?");
				return TCL_ERROR;
			}

			char         *pghandle_name = Tcl_GetString(objv[3]);
			char         *sqliteCode = NULL;
			char         *sqliteTable = NULL;
			char         *dropTable = NULL;
			sqlite3_stmt *statement = NULL;
			int           optIndex = 4;
			Tcl_Obj      *typeList = NULL;
			Tcl_Obj      *nameTypeList = NULL;
			int           rowbyrow = 0;
			int           returnCode = TCL_OK;
			char         *errorMessage = NULL;

			while(optIndex < objc) {
				char *optName = Tcl_GetString(objv[optIndex]);
				optIndex++;
				if (optName[0] != '-')
					goto import_wrong_num_args;
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
				} else if (strcmp(optName, "-rowbyrow") == 0) {
					rowbyrow = 1;
				} else
					goto import_wrong_num_args;
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
				importList = Pg_sqlite_createImportList(typeList, 0, 1);
			}

			if(sqliteTable) {
				if (!nameTypeList) {
					Tcl_AppendResult(interp, "No template (-as) provided for -into", (char *)NULL);
					return TCL_ERROR;
				}

				importList = Pg_sqlite_createImportList(nameTypeList, 1, 2);
				if (!importList)
					return TCL_ERROR;

				sqliteCode = Pg_sqlite_createTable(interp, sqlite_db, sqliteTable, nameTypeList);
				if (!sqliteCode)
					return TCL_ERROR;
				dropTable = sqliteTable;
			}

			prepStatus = sqlite3_prepare_v2(sqlite_db, sqliteCode, -1, &statement, NULL);
			if(prepStatus != SQLITE_OK) {
				errorMessage = "Failed to prepare sqlite3 statement";
				returnCode = TCL_ERROR;
				goto import_cleanup_and_exit;
			}

			if(rowbyrow) {
				conn = PqGetConnectionId(interp, pghandle, NULL);
				if(conn == NULL) {
					Tcl_AppendResult (interp, " while getting connection from ", pghandle, (char *)NULL);
					returnCode = TCL_ERROR;
					goto import_cleanup_and_exit;
				}
				PQSetSingleRowMode(conn);
				result = PQGetResult(conn);
			} else {
				result = PqGetResultId(interp, pghandle, NULL);
			}

			if(!result) {
				Tcl_AppendResult (interp, "Failed to get handle from ", pghandle, (char *)NULL);
				returnCode = TCL_ERROR;
				goto import_cleanup_and_exit;
			}

			int   nTuples;
			int   totalTupes = 0;

			while(result) {
				status = PQResultStatus(result);

				if(status != PGRES_TULES_OK && status != PGRES_SINGLE_TUPLE) {
					errorMessage = PQresultErrorMessage(result);
					if (!*errorMessage)
						errorMessage = PQresStatus(status);
					tclStatus = TCL_ERROR;
					break;
				}

				nTuples = PQntuples(result);

				for (tupleIndex = 0; tupleIndex < nTuples, tupleIndex++) {
					totalTuples++;
					for(column = 0; column < nColumns; column++) {
						value = PQgetValue(result, tupleIndex, column);
						switch(importList[column]) {
							case SQLITE_INT: {
								sqlite3_bind_int(statement, column, atoi(value));
								break;
							}
							case SQLITE_DOUBLE: {
								sqlite3_bind_double(statement, column, atof(value));
								break;
							}
							case SQLITE_TEXT: {
								sqlite3_bind_text(statement, column, value, -1, SQLITE_TRANSIENT);
								break;
							}
						}
					}
					stepStatus = sqlite3_step(statement);
					if (stepStatus != SQLITE_DONE) {
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

			if(importList)
				ckfree(importList);

			if(returnCode == TCL_ERROR) {
				if(dropTable) {
					Pg_sqlite_dropTable(sqlite_db, dropTable);
				}

				if (errorMessage) {
					Tcl_SetResult(interp, errorMessage, TCL_VOLATILE);
				}

				return TCL_ERROR;
			}

			Tcl_SetObjResult(interp, Tcl_NewIntObj(totalTuples));
			return returnCode;
		}
	}

	return TCL_OK;
}

