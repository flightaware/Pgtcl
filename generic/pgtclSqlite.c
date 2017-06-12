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

// From tclsqlite.c, part 1 of the hack, sqlite3 conveniently guarantees that the first element in
// the userdata for an sqlite proc is the sqlite3 database.
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
	{"integer",      PG_SQLITE_INT},
	{"real",         PG_SQLITE_DOUBLE},
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
Pg_sqlite_getNames(Tcl_Interp *interp, Tcl_Obj *list, int stride, char ***arrayPtr, int *lengthPtr)
{
	Tcl_Obj **objv;
	int       objc;
	char    **array;
	int       i;
	int       col;

	if(Tcl_ListObjGetElements(interp, list, &objc, &objv) != TCL_OK)
		return TCL_ERROR;

	if (stride > 1 && (objc % stride) != 0) {
		Tcl_AppendResult(interp, "List not an even length", (char *)NULL);
		return TCL_ERROR;
	}

	array = (char **)ckalloc((sizeof *array) * (objc / stride));

	for(col = 0, i = 0; i < objc; col++, i += stride) {
		array[col] = Tcl_GetString(objv[i]);
	}

	*arrayPtr = array;
	*lengthPtr = col;
	return TCL_OK;
}

int
Pg_sqlite_bindValue(sqlite3 *sqlite_db, sqlite3_stmt *statement, int column, char *value, enum mappedTypes type, const char **errorMessagePtr)
{
	switch(type) {
		case PG_SQLITE_INT: {
			if (sqlite3_bind_int(statement, column+1, atoi(value)) == SQLITE_OK)
				return TCL_OK;
			break;
		}
		case PG_SQLITE_DOUBLE: {
			if (sqlite3_bind_double(statement, column+1, atof(value)) == SQLITE_OK)
				return TCL_OK;
			break;
		}
		case PG_SQLITE_TEXT: {
			if (sqlite3_bind_text(statement, column+1, value, -1, SQLITE_TRANSIENT) == SQLITE_OK)
				return TCL_OK;
			break;
		}
		default: {
			*errorMessagePtr = "Internal error - invalid column type";
			return TCL_ERROR;
		}
	}
	*errorMessagePtr = sqlite3_errmsg(sqlite_db);
	return TCL_ERROR;
}

char *
Pg_sqlite_generate(Tcl_Interp *interp, sqlite3 *sqlite_db, char *sqliteTable, Tcl_Obj *nameList, Tcl_Obj *nameTypeList, Tcl_Obj *primaryKey, char *unknownKey, int newTable, int replacing)
{
	Tcl_Obj **objv;
	int       objc;
	Tcl_Obj  *create = Tcl_NewObj();
	Tcl_Obj  *sql = Tcl_NewObj();
	Tcl_Obj  *values = Tcl_NewObj();
	int       i;
	int       primaryKeyIndex = 0;
	int       stride;

	if(nameTypeList) {
		if(Tcl_ListObjGetElements(interp, nameTypeList, &objc, &objv) != TCL_OK)
			return NULL;

		if(objc & 1) {
			Tcl_AppendResult(interp, "List must have an even number of elements", (char *)NULL);
			return NULL;
		}

		stride = 2;
	} else {
		if(Tcl_ListObjGetElements(interp, nameList, &objc, &objv) != TCL_OK)
			return NULL;

		stride = 1;
	}

	if(newTable && primaryKey) {
		char *keyName = Tcl_GetString(primaryKey);
		for(i = 0; i < objc; i += stride)
			if(strcmp(keyName, Tcl_GetString(objv[i])) == 0)
				break;
		if(i >= objc) {
			Tcl_AppendResult(interp, "Primary key not found in list", (char *)NULL);
			return NULL;
		}
		primaryKeyIndex = i/stride;
	}

	if (newTable)
		Tcl_AppendStringsToObj(create, "CREATE TABLE ", sqliteTable, " (", (char *)NULL);

	if (replacing) {
		Tcl_AppendStringsToObj(sql, "INSERT OR REPLACE INTO ", sqliteTable, " (", (char *)NULL);
	} else {
		Tcl_AppendStringsToObj(sql, "INSERT INTO ", sqliteTable, " (", (char *)NULL);
	}

	for(i = 0; i < objc; i+= stride) {
		if (newTable) {
			Tcl_AppendToObj(create, "\n\t", -1);
			Tcl_AppendObjToObj(create, objv[i]);
			if (stride == 2) {
				Tcl_AppendToObj(create, " ", -1);
				Tcl_AppendObjToObj(create, objv[i+1]);
			} else {
				Tcl_AppendToObj(create, " TEXT", -1);
			}
			if(i == primaryKeyIndex)
				Tcl_AppendToObj(create, " PRIMARY KEY", -1);

			if(i < objc-stride)
				Tcl_AppendToObj(create, ",", -1);
		}

		if(unknownKey && strcmp(Tcl_GetString(objv[i]), unknownKey) == 0) {
			Tcl_AppendResult(interp, "Unknown key duplicates existing key", (char *)NULL);
			return NULL;
		}

		if(i > 0)
			Tcl_AppendToObj(sql, ", ", -1);
		Tcl_AppendObjToObj(sql, objv[i]);

		if(i > 0)
			Tcl_AppendToObj(values, ", ", -1);
		Tcl_AppendToObj(values, "?", -1);
	}

	if(unknownKey) {
		if (newTable) {
			Tcl_AppendStringsToObj(create, ",\n\t", unknownKey, " TEXT", (char *)NULL);
		}
		Tcl_AppendStringsToObj(sql, ", ", unknownKey, (char *)NULL);
		Tcl_AppendToObj(values, ",?", -1);
	}

	if(newTable) Tcl_AppendToObj(create, "\n);", -1);

	Tcl_AppendToObj(sql, ") VALUES (", -1);
	Tcl_AppendObjToObj(sql, values);
	Tcl_AppendToObj(sql, ");", -1);

	if(newTable) {
		if(Pg_sqlite_execObj(interp, sqlite_db, create) != TCL_OK)
			return NULL;
	}

	return Tcl_GetString(sql);
}

int
Pg_sqlite_dropTable(Tcl_Interp *interp, sqlite3 *sqlite_db, char *dropTable)
{
	Tcl_Obj *drop = Tcl_NewObj();

	Tcl_AppendStringsToObj(drop, "DROP TABLE ", dropTable, ";", (char *)NULL);

	return Pg_sqlite_execObj(interp, sqlite_db, drop);
}

// Part 2 of the hack, locate the ObjProc for a known sqlite3 command so I can validate that the
// userdata I've pulled out of the command provided really is a sqlite3 userdata.
int
sqlite_probe(Tcl_Interp *interp, Tcl_ObjCmdProc **procPtr)
{
	static Tcl_ObjCmdProc *sqlite3_ObjProc = NULL;

	if (sqlite3_ObjProc == NULL) {
		char               cmd_name[256];
		char               create_cmd[256];
		char               delete_cmd[256];
		struct Tcl_CmdInfo cmd_info;

		if (Tcl_Eval(interp, "package require sqlite3") != TCL_OK) {
			return TCL_ERROR;
		}

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
	}
	*procPtr = sqlite3_ObjProc;

	return TCL_OK;
}

int
Pg_sqlite_gets(Tcl_Interp *interp, Tcl_Channel chan, char **linePtr)
{
	Tcl_Obj *obj = Tcl_NewObj();

	if(Tcl_GetsObj(chan, obj) == -1) {
		*linePtr = NULL;
		if(Tcl_Eof(chan)) {
			return TCL_BREAK;
		} else {
			Tcl_AppendResult (interp, Tcl_ErrnoMsg(Tcl_GetErrno()), (char *)NULL);
			return TCL_ERROR;
		}
	}
	*linePtr = Tcl_GetString(obj);
	return TCL_OK;
}

int
Pg_sqlite_split_tabsep(char *row, char ***columnsPtr, int nColumns, char *sepStr, const char **errorMessagePtr)
{
	int i;
	char *col;
	char *nextCol;
	char **columns = ckalloc(nColumns * sizeof *columns);
	int returnCode = TCL_OK;
	int sepLen = strlen(sepStr);

	col = row;
	i = 0;
	while(col && i < nColumns) {
		nextCol = strstr(col, sepStr);
		columns[i++] = col;
		if(nextCol) {
			*nextCol = 0;
			nextCol += sepLen;
		}
		col = nextCol;
	}
	if (col) {
		*errorMessagePtr = "Too many columns in row";
		returnCode = TCL_ERROR;
	} else if (i < nColumns) {
		*errorMessagePtr = "Not enough columns in row";
		returnCode = TCL_ERROR;
	}
	if(returnCode == TCL_OK) {
		*columnsPtr = columns;
	} else {
		ckfree(columns);
	}

	return returnCode;
}

int
Pg_sqlite_split_keyval(Tcl_Interp *interp, char *row, char ***columnsPtr, int nColumns, char *sepStr, char **names, Tcl_Obj *unknownObj)
{
	char *val;
	char *key;
	char *nextVal;
	int col;
	char **columns = ckalloc(nColumns * sizeof *columns);
	int returnCode = TCL_OK;
	int sepLen = strlen(sepStr);

	Tcl_SetListObj(unknownObj, 0, NULL);
	for(col = 0; col < nColumns; col++)
		columns[col] = NULL;

	val = row;
	while(val) {
		nextVal = strstr(val, sepStr);
		if(!nextVal) {
			Tcl_AppendResult(interp, "Odd number of columns", (char *)NULL);
			returnCode = TCL_ERROR;
			break;
		}
		key = val;
		*nextVal = 0;
		nextVal += sepLen;

		val = nextVal;
		nextVal = strstr(val, sepStr);
		if(nextVal) {
			*nextVal = 0;
			nextVal += sepLen;
		}

		for(col = 0; col < nColumns; col++) {
			if(strcmp(key, names[col]) == 0) {
				break;
			}
		}
		if(col < nColumns)
			columns[col] = val;
		else {
			Tcl_ListObjAppendElement(interp, unknownObj, Tcl_NewStringObj(key, -1));
			Tcl_ListObjAppendElement(interp, unknownObj, Tcl_NewStringObj(val, -1));
		}

		val = nextVal;
	}

	if(returnCode == TCL_OK) {
		*columnsPtr = columns;
	} else {
		ckfree(columns);
		Tcl_SetListObj(unknownObj, 0, NULL);
	}

	return returnCode;
}

int
Pg_sqlite(ClientData clientdata, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
        char               *sqlite_commandName;
        struct Tcl_CmdInfo  sqlite_commandInfo;
        struct SqliteDb    *sqlite_clientData;
        sqlite3            *sqlite_db;
	int                 cmdIndex;
	Tcl_ObjCmdProc     *sqlite3_ObjProc = NULL;

	static CONST84 char *subCommands[] = {
		"filename", "import_postgres_result", "write_tabsep", "read_tabsep", "read_tabsep_keylist",
		(char *)NULL
	};

	enum subCommands
	{
		CMD_FILENAME, CMD_IMPORT_POSTGRES_RESULT, CMD_WRITE_TABSEP, CMD_READ_TABSEP, CMD_READ_KEYVAL,
		NUM_COMMANDS
	};

        if (objc <= 2) {
                Tcl_WrongNumArgs(interp, 1, objv, "sqlite_handle command ?args?");
                return TCL_ERROR;
        }

        sqlite_commandName = Tcl_GetString(objv[1]);

        if (!Tcl_GetCommandInfo(interp, sqlite_commandName, &sqlite_commandInfo)) {
                Tcl_AppendResult(interp, sqlite_commandName, " is not an sqlite3 handle", (char *)NULL);
                return TCL_ERROR;
        }

	if (sqlite_probe(interp, &sqlite3_ObjProc) != TCL_OK) {
		return TCL_ERROR;
	}

	if (sqlite3_ObjProc != sqlite_commandInfo.objProc) {
		Tcl_AppendResult(interp, "command ", sqlite_commandName, " is not an sqlite3 handle", (char *)NULL);
		return TCL_ERROR;
	}

        sqlite_clientData = (struct SqliteDb *)sqlite_commandInfo.objClientData;

        sqlite_db = sqlite_clientData->db;

	if (Tcl_GetIndexFromObj(interp, objv[2], subCommands, "command", TCL_EXACT, &cmdIndex) != TCL_OK)
		return TCL_ERROR;

	int minargs[NUM_COMMANDS] = { -1 };
	int incoming[NUM_COMMANDS] = { -1 };
	char *argerr[NUM_COMMANDS] = { "" };

	// one time initialization of common code
	if(minargs[0] == -1) {
		int i;
		for(i = 0; i < NUM_COMMANDS; i++) {
			minargs[i] = 0;
			incoming[i] = 0;
			argerr[i] = "";
		}
		minargs[CMD_IMPORT_POSTGRES_RESULT] = 4;
		minargs[CMD_READ_TABSEP] = 3;
		minargs[CMD_READ_KEYVAL] = 3;
		incoming[CMD_IMPORT_POSTGRES_RESULT] = 1;
		incoming[CMD_READ_TABSEP] = 1;
		incoming[CMD_READ_KEYVAL] = 1;
		argerr[CMD_READ_TABSEP] = "?-row tabsep_row? ?-file file_handle? ?-sql sqlite_sql? ?-create new_table? ?-into table? ?-as name-type-list? ?-types type-list? ?-names name-list? ?-pkey primary_key? ?-sep sepstring? ?-null nullstring? ?-replace?";
		argerr[CMD_IMPORT_POSTGRES_RESULT] = "handle ?-sql sqlite_sql? ?-create new_table? ?-into table? ?-as name-type-list? ?-types type-list? ?-names name-list? ?-rowbyrow? ?-pkey primary_key? ?-null nullstring? ?-replace?";
		argerr[CMD_READ_KEYVAL] = "?-row tabsep_row? ?-file file_handle? ?-create new_table? ?-into table? ?-as name-type-list? ?-names name-list? ?-pkey primary_key? ?-sep sepstring? ?-unknown colname? ?-replace?";
	}

	// common variables
	char              *sqliteCode = NULL;
	char              *sqliteTable = NULL;
	char              *dropTable = NULL;
	sqlite3_stmt      *statement = NULL;
	int                optIndex = 4;
	Tcl_Obj           *typeList = NULL;
	Tcl_Obj           *nameTypeList = NULL;
	Tcl_Obj           *nameList = NULL;
	int                rowbyrow = 0;
	int                returnCode = TCL_OK;
	const char        *errorMessage = NULL;
	enum mappedTypes  *columnTypes = NULL;
	char             **columnNames = NULL;
	int                nColumns = 0;
	int                column;
	int                prepStatus;
	PGconn            *conn = NULL;
	PGresult          *result = NULL;
	ExecStatusType     status;
	char              *tabsepFile = NULL;
	char              *tabsepRow = NULL;
	Tcl_Obj           *primaryKey = NULL;
	char             **columns = NULL;
	int                totalTuples = 0;
	char		  *sepString = "\t";
	char              *nullString = NULL;
	char              *unknownKey = NULL;
	int		   createTable = 0;
	int		   replaceTable = 0;

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
			if (optName[0] != '-') {
				goto common_wrong_num_args;
			}

			if (cmdIndex != CMD_READ_KEYVAL && strcmp(optName, "-types") == 0) {
				typeList = objv[optIndex];
				optIndex++;
			} else if (strcmp(optName, "-names") == 0) {
				nameList = objv[optIndex];
				optIndex++;
			} else if (strcmp(optName, "-as") == 0) {
				nameTypeList = objv[optIndex];
				optIndex++;
			} else if (strcmp(optName, "-pkey") == 0) {
				primaryKey = objv[optIndex];
				optIndex++;
			} else if (cmdIndex != CMD_IMPORT_POSTGRES_RESULT && strcmp(optName, "-sep") == 0) {
				sepString = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex != CMD_READ_KEYVAL && strcmp(optName, "-null") == 0) {
				nullString = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex == CMD_READ_KEYVAL && strcmp(optName, "-unknown") == 0) {
				unknownKey = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex != CMD_READ_KEYVAL && strcmp(optName, "-sql") == 0) {
				sqliteCode = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (strcmp(optName, "-create") == 0) {
				sqliteTable = Tcl_GetString(objv[optIndex]);
				createTable = 1;
				optIndex++;
			} else if (strcmp(optName, "-into") == 0) {
				sqliteTable = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex != CMD_IMPORT_POSTGRES_RESULT && strcmp(optName, "-row") == 0) {
				tabsepRow = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex != CMD_IMPORT_POSTGRES_RESULT && strcmp(optName, "-file") == 0) {
				tabsepFile = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex == CMD_IMPORT_POSTGRES_RESULT && strcmp(optName, "-rowbyrow") == 0) {
				rowbyrow = 1;
			} else if (strcmp(optName, "-replace") == 0) {
				replaceTable = 1;
			} else {
				goto common_wrong_num_args;
			}
		}

		if (cmdIndex == CMD_READ_TABSEP || cmdIndex == CMD_READ_KEYVAL) {
			if(!tabsepFile && !tabsepRow) {
				Tcl_AppendResult(interp, "command requires either -row or -file", (char *)NULL);
				return TCL_ERROR;
			}

			if(tabsepFile && tabsepRow) {
				Tcl_AppendResult(interp, "Can't use both -row and -file", (char *)NULL);
			}
		}

		if (sqliteCode && sqliteTable) {
			Tcl_AppendResult(interp, "Can't use both -sql and -into", (char *)NULL);
			return TCL_ERROR;
		}

		if (!sqliteCode && !sqliteTable) {
			Tcl_AppendResult(interp, "No sqlite destination provided", (char *)NULL);
			return TCL_ERROR;
		}

		if ((nameList || typeList) && nameTypeList) {
			Tcl_AppendResult(interp, "Can't use both -names/-types and -as", (char *)NULL);
			return TCL_ERROR;
		}

		if(typeList) {
			if (Pg_sqlite_mapTypes(interp, typeList, 0, 1, &columnTypes, &nColumns) != TCL_OK)
				return TCL_ERROR;
		}

		if(nameList) {
			if(cmdIndex == CMD_READ_KEYVAL) {
				if(Pg_sqlite_getNames(interp, nameList, 1, &columnNames, &nColumns) != TCL_OK) {
					if (columnTypes)
						ckfree(columnTypes);
					return TCL_ERROR;
				}
			} else if(!nColumns)  {
				if(Tcl_ListObjLength(interp, nameList, &nColumns) != TCL_OK) {
					if (columnTypes)
						ckfree(columnTypes);
					return TCL_ERROR;
				}
			}
		} else if(nameTypeList) {
			if(cmdIndex == CMD_READ_KEYVAL) {
				if(Pg_sqlite_getNames(interp, nameTypeList, 2, &columnNames, &nColumns) != TCL_OK) {
					if (columnTypes)
						ckfree(columnTypes);
					return TCL_ERROR;
				}
			}
		}

		if(sqliteTable) {
			if(nameTypeList) {
				if (Pg_sqlite_mapTypes(interp, nameTypeList, 1, 2, &columnTypes, &nColumns) != TCL_OK)
					return TCL_ERROR;
			} else if(!nameList) {
				Tcl_AppendResult(interp, "No template (-as) provided for -into", (char *)NULL);
				if (columnNames)
					ckfree(columnNames);
				if (columnTypes)
					ckfree(columnTypes);
				return TCL_ERROR;
			}

			sqliteCode = Pg_sqlite_generate(interp, sqlite_db, sqliteTable, nameList, nameTypeList, primaryKey, unknownKey, createTable, replaceTable);
			if (!sqliteCode) {
				if (columnTypes)
					ckfree(columnTypes);
				return TCL_ERROR;
			}
			if(createTable) dropTable = sqliteTable;
		}

		// Last try hack to guess columns
		if(!nColumns) {
			char *p = strchr(sqliteCode, '?');
			while(p) {
				p++;
				nColumns++;
				p = strchr(p, '?');
			}
		}

		if(!nColumns) {
			Tcl_AppendResult(interp, "Can't determine row length from provided arguments", (char *)NULL);
			if (columnNames)
				ckfree(columnNames);
			if (columnTypes)
				ckfree(columnTypes);
			return TCL_ERROR;
		}

		prepStatus = sqlite3_prepare_v2(sqlite_db, sqliteCode, -1, &statement, NULL);
		if(prepStatus != SQLITE_OK) {
			Tcl_AppendResult(interp, sqlite3_errmsg(sqlite_db), (char *)NULL);
			if (columnNames)
				ckfree(columnNames);
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

		case CMD_WRITE_TABSEP: {
			Tcl_Channel channel = NULL;
			const char *channelName = NULL;
			int channelMode;
			int sqliteStatus;

			optIndex = 5;
			nullString = "";

			if(objc < optIndex) {
			  write_wrong_num_args:
				Tcl_WrongNumArgs(interp, 3, objv, "handle sql ?-null nullstring? ?-sep sepstring?");
				return TCL_ERROR;
			}

			while (optIndex < objc) {
				char *optName = Tcl_GetString(objv[optIndex]);
				optIndex++;
				if (optName[0] != '-') {
					goto write_wrong_num_args;
				}

				if (strcmp(optName, "-null") == 0) {
					nullString = Tcl_GetString(objv[optIndex]);
					optIndex++;
				} else if (strcmp(optName, "-sep") == 0) {
					sepString = Tcl_GetString(objv[optIndex]);
					optIndex++;
				} else
					goto write_wrong_num_args;
			}

			channelName = Tcl_GetString(objv[3]);
			channel = Tcl_GetChannel(interp, channelName, &channelMode);
			if(!channel) {
				Tcl_AppendResult(interp, Tcl_ErrnoMsg(Tcl_GetErrno()), " converting ", channelName, (char *)NULL);
				return TCL_ERROR;
			}
			if (!channelMode && TCL_WRITABLE) {
				Tcl_AppendResult (interp, "Channel ", channelName, " is not writable", (char *)NULL);
				return TCL_ERROR;
			}

			prepStatus = sqlite3_prepare_v2(sqlite_db, Tcl_GetString(objv[4]), -1, &statement, NULL);
			if(prepStatus != SQLITE_OK) {
				Tcl_AppendResult(interp, sqlite3_errmsg(sqlite_db), (char *)NULL);
				return TCL_ERROR;
			}
			nColumns = sqlite3_column_count(statement);
			totalTuples = 0;

			returnCode = TCL_ERROR;
			while (SQLITE_ROW == (sqliteStatus = sqlite3_step(statement))) {
				int i;
				for(i = 0; i < nColumns; i++) {
					char *value = (char *)sqlite3_column_text(statement, i);
					if(value == NULL) value = nullString;
					if (i > 0 && Tcl_WriteChars(channel, sepString, -1) == -1) {
						Tcl_AppendResult(interp, Tcl_ErrnoMsg(Tcl_GetErrno()), (char *)NULL);
						goto write_tabsep_cleanup_and_exit;
					}
					if (Tcl_WriteChars(channel, value, -1) == -1) {
						Tcl_AppendResult(interp, Tcl_ErrnoMsg(Tcl_GetErrno()), (char *)NULL);
						goto write_tabsep_cleanup_and_exit;
					}
				}
				if(Tcl_WriteChars(channel, "\n", -1) == -1) {
					Tcl_AppendResult(interp, Tcl_ErrnoMsg(Tcl_GetErrno()), (char *)NULL);
					goto write_tabsep_cleanup_and_exit;
				}
				totalTuples++;
			}
			if(sqliteStatus != SQLITE_DONE) {
				Tcl_AppendResult(interp, sqlite3_errmsg(sqlite_db), (char *)NULL);
				goto write_tabsep_cleanup_and_exit;
			}
			returnCode = TCL_OK;
		  write_tabsep_cleanup_and_exit:

			if(statement)
				sqlite3_finalize(statement);

			if(returnCode == TCL_ERROR)
				return TCL_ERROR;

			Tcl_SetObjResult(interp, Tcl_NewIntObj(totalTuples));

			return TCL_OK;
		}

		case CMD_READ_KEYVAL:
		case CMD_READ_TABSEP: {
			Tcl_Channel tabsepChannel = NULL;
			int channelMode;
			char *row = NULL;
			Tcl_Obj *unknownObj = NULL;

			if(cmdIndex == CMD_READ_KEYVAL)
				unknownObj = Tcl_NewObj();

			if(tabsepFile) {
				tabsepChannel = Tcl_GetChannel(interp, tabsepFile, &channelMode);
				if(!tabsepChannel) {
					Tcl_AppendResult(interp, Tcl_ErrnoMsg(Tcl_GetErrno()), " converting ", tabsepFile, (char *)NULL);
					returnCode = TCL_ERROR;
					goto read_tabsep_cleanup_and_exit;
				}
				if (!channelMode && TCL_READABLE) {
					Tcl_AppendResult (interp, "File in -from argument must be readable", (char *)NULL);
					returnCode = TCL_ERROR;
					goto read_tabsep_cleanup_and_exit;
				}
				if((returnCode = Pg_sqlite_gets(interp, tabsepChannel, &row)) != TCL_OK) {
					if (returnCode == TCL_BREAK)
						returnCode = TCL_OK;
					goto read_tabsep_cleanup_and_exit;
				}
			} else {
				row = tabsepRow;
			}

			while(row) {
				if(cmdIndex == CMD_READ_KEYVAL) {
					int len;
					if (Pg_sqlite_split_keyval(interp, row, &columns, nColumns, sepString, columnNames, unknownObj) != TCL_OK) {
						returnCode = TCL_ERROR;
						break;
					}

					if(Tcl_ListObjLength(interp, unknownObj, &len) != TCL_OK) {
						returnCode = TCL_ERROR;
						break;
					}
					if(len && !unknownKey) {
						returnCode = TCL_ERROR;
						break;
					}
				} else {
					if (Pg_sqlite_split_tabsep(row, &columns, nColumns, sepString, &errorMessage) != TCL_OK) {
						returnCode = TCL_ERROR;
						break;
					}
				}

				for(column = 0; column < nColumns; column++) {
					char *value = columns[column];
					if(!value)
						continue;
					if(nullString && strcmp(value, nullString) == 0)
						continue;

					int type = columnTypes ? columnTypes[column] : PG_SQLITE_TEXT;
					if (Pg_sqlite_bindValue(sqlite_db, statement, column, value, type, &errorMessage)) {
						returnCode = TCL_ERROR;
						break;
					}
				}

				if(cmdIndex == CMD_READ_KEYVAL && unknownKey) {
					char *value = Tcl_GetString(unknownObj);
					if(value[0]) {
						if (Pg_sqlite_bindValue(sqlite_db, statement, nColumns, value, PG_SQLITE_TEXT, &errorMessage) != TCL_OK) {
							returnCode = TCL_ERROR;
							break;
						}
					}
				}

				if (sqlite3_step(statement) != SQLITE_DONE) {
					errorMessage = sqlite3_errmsg(sqlite_db);
					returnCode = TCL_ERROR;
					break;
				}
				sqlite3_reset(statement);
				sqlite3_clear_bindings(statement);

				totalTuples++;

				if(tabsepFile) {
					if((returnCode = Pg_sqlite_gets(interp, tabsepChannel, &row)) == TCL_BREAK)
						returnCode = TCL_OK;
				} else {
					row = NULL;
				}
			}

		  read_tabsep_cleanup_and_exit:

			if(statement)
				sqlite3_finalize(statement);

			if(columnTypes)
				ckfree(columnTypes);

			if(columns)
				ckfree(columns);

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

		case CMD_IMPORT_POSTGRES_RESULT: {
			char *pghandle_name = Tcl_GetString(objv[3]);
			int   nTuples;
			int   tupleIndex;

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
					for(column = 0; column < nColumns; column++) {
						char *value = PQgetvalue(result, tupleIndex, column);
						if(nullString && strcmp(value, nullString) == 0)
							continue;

						int type = columnTypes ? columnTypes[column] : PG_SQLITE_TEXT;
						if (Pg_sqlite_bindValue(sqlite_db, statement, column, value, type, &errorMessage) != TCL_OK) {
							returnCode = TCL_ERROR;
							goto import_loop_end;
						}
					}

					if (sqlite3_step(statement) != SQLITE_DONE) {
						errorMessage = sqlite3_errmsg(sqlite_db);
						returnCode = TCL_ERROR;
						goto import_loop_end;
					}
					sqlite3_reset(statement);
					sqlite3_clear_bindings(statement);

					totalTuples++;
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

