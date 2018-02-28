#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <libpq-fe.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>

#include <tcl.h>

#include "pgtclCmds.h"
#include "pgtclId.h"

#include <sqlite3.h>

#ifndef CONST84
#     define CONST84
#endif

#define LAPPEND_STRING(i, o, s) Tcl_ListObjAppendElement((i), (o), Tcl_NewStringObj((s), -1));

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

//
// Prepare a statement, and finalize if first if it's already prepared
//
int
Pg_sqlite_prepare(Tcl_Interp *interp, sqlite3 *sqlite_db, char *sql, sqlite3_stmt **statement_ptr)
{
	if(*statement_ptr) {
		sqlite3_finalize(*statement_ptr);
		*statement_ptr = NULL;
	}

	if(sqlite3_prepare_v2(sqlite_db, sql, -1, statement_ptr, NULL) != SQLITE_OK) {
		Tcl_AppendResult(interp, sqlite3_errmsg(sqlite_db), (char *)NULL);
		return TCL_ERROR;
	}
	return TCL_OK;
}

int
Pg_sqlite_begin(Tcl_Interp *interp, sqlite3 *sqlite_db)
{
	char *errMsg;
	if(sqlite3_exec(sqlite_db, "begin;", NULL, NULL, &errMsg) != SQLITE_OK) {
                Tcl_AppendResult(interp, errMsg, " when beginning a transaction", (char *)NULL);
                return TCL_ERROR;
        }
	return TCL_OK;
}

int
Pg_sqlite_commit(Tcl_Interp *interp, sqlite3 *sqlite_db)
{
	char *errMsg;
	if(sqlite3_exec(sqlite_db, "commit;", NULL, NULL, &errMsg) != SQLITE_OK) {
                Tcl_AppendResult(interp, errMsg, " when comitting a transaction", (char *)NULL);
                return TCL_ERROR;
        }
	return TCL_OK;
}

//
// Finalize a statement, commit, and prepare it again
//
int
Pg_sqlite_recommit(Tcl_Interp *interp, sqlite3 *sqlite_db, char *sql, sqlite3_stmt **statement_ptr)
{
//	if(*statement_ptr) {
//		sqlite3_finalize(*statement_ptr);
//		*statement_ptr = NULL;
//	}

	if(Pg_sqlite_commit(interp, sqlite_db) != TCL_OK)
		return TCL_ERROR;

	if(Pg_sqlite_begin(interp, sqlite_db) != TCL_OK)
		return TCL_ERROR;

//	return Pg_sqlite_prepare(interp, sqlite_db, sql, statement_ptr);
	return TCL_OK;
}

int Pg_sqlite_execObj(Tcl_Interp *interp, sqlite3 *sqlite_db, Tcl_Obj *obj)
{
	sqlite3_stmt *statement = NULL;
	int           result = TCL_OK;

	if(Pg_sqlite_prepare(interp, sqlite_db, Tcl_GetString(obj), &statement) != TCL_OK) {
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

// We don't use the internal Sqlite3 types because we may need to do some special handling for types mapped from
// PostgreSQL. For example, PostgreSQL booleans present as strings, but Sqlite3 booleans are integer 0 or 1.
enum mappedTypes {
	PG_SQLITE_INT,
	PG_SQLITE_DOUBLE,
	PG_SQLITE_TEXT,
	PG_SQLITE_BOOL,
	PG_SQLITE_NOTYPE
};

struct {
	char *name;
	enum mappedTypes type;
} mappedTypes[] = {
	{"integer",      PG_SQLITE_INT},
	{"real",         PG_SQLITE_DOUBLE},
	{"boolean",      PG_SQLITE_BOOL},
	{"int",          PG_SQLITE_INT},
	{"double",       PG_SQLITE_DOUBLE},
	{"bool",         PG_SQLITE_BOOL},
	{"text",         PG_SQLITE_TEXT},
	{NULL,           PG_SQLITE_NOTYPE}
};

// Reverse mapping primarily for debugging. Map to the first (canonical) name for a type.
char *Pg_sqlite_typename(enum mappedTypes type)
{
	static char *typenames[PG_SQLITE_NOTYPE] = { NULL };

	if (type >= PG_SQLITE_NOTYPE)
		return NULL;

	if (typenames[0] == NULL) {
		int t;

		for(t = 0; mappedTypes[t].name; t++) {
			if (typenames[mappedTypes[t].type] == NULL) {
				typenames[mappedTypes[t].type] = mappedTypes[t].name;
			}
		}
	}

	return typenames[type];
}

// Step through a list and produce an array of the types in the list. Since the list may be a list of types
// or a list of names and types, we take a start index and stride to describe the list.
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
				array[col] = mappedTypes[t].type;
				break;
			}
		}

		if(!mappedTypes[t].name) {
			ckfree((void *)array);
			Tcl_AppendResult(interp, "Unknown type ", typeName, (char *)NULL);
			return TCL_ERROR;
		}
	}

	*arrayPtr = array;
	*lengthPtr = col;
	return TCL_OK;
}

// Step through a list and produce an array of the names in the list. Since the list may be a list of types
// or a list of names and types, we take a stride to describe the list.
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

// PostgreSQL documentation, possible values for the boolean type:
// Valid literal values for the "true" state are: TRUE 't' 'true' 'y' 'yes' 'on' '1'
// For the "false" state, the following values can be used: FALSE 'f' 'false' 'n' 'no' 'off' '0'
// boolean values are output using the letters t and f.
// Handle all possible cases
int
Pg_sqlite_toBool(char *value)
{
	int i = 0;

	// skip 'quotes'
	if(value[i] == '\'') i++;

	switch (tolower(value[i])) {
		// off and on
		case 'o': {
			if (tolower(value[i+1]) == 'n') { return 1; }
			return 0;
		}

		// true and false
		case 't': case 'y': { return 1; }
		case 'f': case 'n': { return 0; }

		// otherwise assume it's an integer
		default: {
			return atoi(value);
		}
	}
}

// Bind a single value to an Sqlite3 prepared statement, using our internal types.
int
Pg_sqlite_bindValue(sqlite3 *sqlite_db, sqlite3_stmt *statement, int column, char *value, enum mappedTypes type, const char **errorMessagePtr)
{
	switch(type) {
		case PG_SQLITE_BOOL: {
			if (sqlite3_bind_int(statement, column+1, Pg_sqlite_toBool(value)) == SQLITE_OK)
				return TCL_OK;
			break;
		}
		case PG_SQLITE_INT: {
			int ival = atoi(value);
			if(ival == 0) {
				// It might be a boolean column mapped to an integer column
				ival = Pg_sqlite_toBool(value);
			}
			if (sqlite3_bind_int(statement, column+1, ival) == SQLITE_OK)
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

//
// Generate statement to query the target DB to see if the row is already there. Returns success and fills in the indexes
// of the primary keys in the name list.
//
int
Pg_sqlite_generateCheck(Tcl_Interp *interp, sqlite3 *sqlite_db, char *tableName, char **columnNames, int nColumns, Tcl_Obj *primaryKey, sqlite3_stmt **statementPtr, int **primaryKeyIndexPtr)
{
	Tcl_Obj     **keyv;
	int           keyc;
	int          *primaryKeyIndex = NULL;
	char        **primaryKeyNames = NULL;
	Tcl_Obj      *sql = Tcl_NewObj();
	Tcl_Obj      *where = Tcl_NewObj();
	int           i;
	int           k;
	int           result = TCL_ERROR;
	sqlite3_stmt *statement = NULL;

	if(Tcl_ListObjGetElements(interp, primaryKey, &keyc, &keyv) != TCL_OK)
		goto cleanup_and_exit;

	primaryKeyNames = (char **)ckalloc(keyc * (sizeof *primaryKeyNames));
	for(k = 0; k < keyc; k++) {
		char *column = Tcl_GetString(keyv[k]);
		char *space = strchr(column, ' ');
		if(space) {
			primaryKeyNames[k] = (char *)ckalloc(space - column + 1);
			*space = 0;
			strcpy(primaryKeyNames[k], column);
			*space = ' ';
		} else {
			primaryKeyNames[k] = (char *)ckalloc(strlen(column) + 1);
			strcpy(primaryKeyNames[k], column);
		}
		if(k != 0)
			Tcl_AppendStringsToObj(where, ", ", (char *)NULL);
		Tcl_AppendStringsToObj(where, primaryKeyNames[k], " = ?", (char *)NULL);
	}

	primaryKeyIndex = (int *)ckalloc((keyc + 1) * (sizeof *primaryKeyIndex));
	for(k = 0; k < keyc+1; k++) {
		primaryKeyIndex[k] = -1;
	}

	Tcl_AppendStringsToObj(sql, "SELECT ", (char *)NULL);
	for(i = 0; i < nColumns; i++) {
		char *column = columnNames[i];
		// add to the select clause
		if(i != 0)
			Tcl_AppendStringsToObj(sql, ", ", (char *)NULL);
		Tcl_AppendStringsToObj(sql, column, (char *)NULL);

		// look in primary key list
		for(k = 0; k < keyc; k++) {
			if(strcmp(column, primaryKeyNames[k]) == 0)
				break;
		}

		// if it's a primary key
		if(k < keyc) {
			// remember where it was in the column list
			primaryKeyIndex[k] = i;
		}
	}

	// if there's any unused primary keys that's an error
	for(k = 0; k < keyc; k++) {
		if(primaryKeyIndex[k] == -1)
			break;
	}
	if(k < keyc) {
		Tcl_AppendResult(interp, "Primary keys names must all be in the column list", (char *)NULL);
		goto cleanup_and_exit;
	}

	// combine select list and where clause into statement, and turn it into a string
	Tcl_AppendStringsToObj(sql, " FROM ", tableName, " WHERE (", Tcl_GetString(where), ");", (char *)NULL);

	// create statement
	if(Pg_sqlite_prepare(interp, sqlite_db, Tcl_GetString(sql), &statement) != TCL_OK) {
		goto cleanup_and_exit;
	}

	result = TCL_OK;

  cleanup_and_exit:
	// discard key names
	if(primaryKeyNames) {
		for(k = 0; k < keyc; k++) {
			ckfree((void *)primaryKeyNames[k]);
		}
		ckfree((void *)primaryKeyNames);
	}

	// save or discard primary key indexes.
	if(primaryKeyIndex) {
		if(result == TCL_OK)
			*primaryKeyIndexPtr = primaryKeyIndex;
		else
			ckfree((void *)primaryKeyIndex);
	}

	// save or discard statement
	if(statement) {
		if(result == TCL_OK)
			*statementPtr = statement;
		else
			sqlite3_finalize(statement);
	}

	return result;
}

//
// Execute prepared statement from Pg_sqlite_generate_check, pulling the primary keys from the already parsed row.
//
// Return TCL_CONTINUE if
//    (a) there is a row already existing, and
//    (b) all values in the parsed row match the values retreived from the db
// Return TCL_OK if the there is no row or it doesn't match.
// Return TCL_ERROR if there is an error.
//
int
Pg_sqlite_executeCheck(Tcl_Interp *interp, sqlite3 *sqlite_db, sqlite3_stmt *statement, int *primaryKeyIndex, enum mappedTypes *columnTypes, char **row, int count)
{
	int i;
	int status = TCL_ERROR;
	int col;

	for(i = 0; -1 != (col = primaryKeyIndex[i]); i++) {
		char             *value = row[col];
		enum mappedTypes  type = columnTypes[col];
		const char       *errorMessage;

		if (Pg_sqlite_bindValue(sqlite_db, statement, i, value, type, &errorMessage) != TCL_OK) {
			Tcl_AppendResult(interp, errorMessage, (char *)NULL);
			goto cleanup_and_exit;
		}
	}

	switch (sqlite3_step(statement)) {
		case SQLITE_ROW: {
			for(i = 0; i < count; i++) {
				// NULL CHECKING
				int colType = sqlite3_column_type(statement, i);
				// Both null, matches, continue to next row
				if(colType == SQLITE_NULL && !row[i]) { continue; }
				// sqlite null, check not null, no match, return
				if(colType == SQLITE_NULL && row[i]) {
					status = TCL_OK;
					goto cleanup_and_exit;
				}
				// sqlite has value, check is null, no match, return
				if(colType != SQLITE_NULL && !row[i]) {
					status = TCL_OK;
					goto cleanup_and_exit;
				}

				// Compare columns
				switch (columnTypes[i]) {
					case PG_SQLITE_BOOL: {
						int ival = sqlite3_column_int(statement, i);
						if (Pg_sqlite_toBool(row[i]) != ival) {
							status = TCL_OK;
							goto cleanup_and_exit;
						}
						break;
					}
					case PG_SQLITE_TEXT: {
						char *sval = (char *)sqlite3_column_text(statement, i);
						if(strcmp(row[i], sval) != 0) {
							status = TCL_OK;
							goto cleanup_and_exit;
						}
						break;
					}
					case PG_SQLITE_INT: {
						int ival = sqlite3_column_int(statement, i);
						int rval = atoi(row[i]);
						if(ival != rval) {
							status = TCL_OK;
							goto cleanup_and_exit;
						}
						break;
					}
					case PG_SQLITE_DOUBLE: {
						double dval = sqlite3_column_double(statement, i);
						double rval = atof(row[i]);
						if(dval != rval) {
							status = TCL_OK;
							goto cleanup_and_exit;
						}
						break;
					}
					default: { // otherwise unhandled type, maybe should be an error?
						char *value = (char *)sqlite3_column_text(statement, i);
						if(strcmp(row[i], value) != 0) {
							status = TCL_OK;
							goto cleanup_and_exit;
						}
						break;
					}
				}
			}
			status = TCL_CONTINUE;
			goto cleanup_and_exit;
		}
		case SQLITE_DONE: {
			status = TCL_OK;
			goto cleanup_and_exit;
		}
		default: {
			Tcl_AppendResult(interp, sqlite3_errmsg(sqlite_db), (char *)NULL);
			goto cleanup_and_exit;
		}
	}

  cleanup_and_exit:
	sqlite3_reset(statement);
	sqlite3_clear_bindings(statement);
	return status;
}

// Generate SQL to *insert* a row into Sqlite3. Also create the table if required.
//
// Pulls the names from either a name list or a name-type list. Only one need be provided. If it's creating
// the table and no types are provided, it punts and assumes text.
//
// TODO: Add type list argument, or get rid of the whole separate -names and -types options.
char *
Pg_sqlite_generate(Tcl_Interp *interp, sqlite3 *sqlite_db, char *sqliteTable, Tcl_Obj *nameList, Tcl_Obj *nameTypeList, Tcl_Obj *primaryKey, char *unknownKey, int newTable, int replacing)
{
	Tcl_Obj **objv;
	int       objc;
	Tcl_Obj **keyv = NULL;
	int       keyc = 0;
	Tcl_Obj  *create = Tcl_NewObj();
	Tcl_Obj  *sql = Tcl_NewObj();
	Tcl_Obj  *values = Tcl_NewObj();
	int       i;
	int       primaryKeyIndex = -1;
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
		if(Tcl_ListObjGetElements(interp, primaryKey, &keyc, &keyv) != TCL_OK)
			return NULL;

		if(keyc == 1) {
			char *keyName = Tcl_GetString(keyv[0]);
			for(i = 0; i < objc; i += stride)
				if(strcmp(keyName, Tcl_GetString(objv[i])) == 0)
					break;
			if(i >= objc) {
				Tcl_AppendResult(interp, "Primary key not found in list", (char *)NULL);
				return NULL;
			}
			primaryKeyIndex = i/stride;
		}
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

	if(newTable && keyc > 1) {
		int i;
		Tcl_AppendToObj(create, ",\n\tPRIMARY KEY(", -1);

		for(i = 0; i < keyc; i++) {
			if(i)
				Tcl_AppendToObj(create, ", ", -1);
			Tcl_AppendObjToObj(create, keyv[i]);
		}

		Tcl_AppendToObj(create, ")", -1);
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

// Drop table
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
Pg_sqlite_probe(Tcl_Interp *interp, Tcl_ObjCmdProc **procPtr)
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
			Tcl_AppendResult(interp, "pg_sqlite3 probe failed (", cmd_name, " not a native object proc)", (char *)NULL);
			Tcl_Eval(interp, delete_cmd);
			return TCL_ERROR;
		}

		sqlite3_ObjProc = cmd_info.objProc;
		Tcl_Eval(interp, delete_cmd);

		if (!sqlite3_ObjProc) {
			Tcl_AppendResult(interp, "pg_sqlite3 probe failed (", cmd_name, " not a native object proc)", (char *)NULL);
			return TCL_ERROR;
		}
	}
	*procPtr = sqlite3_ObjProc;

	return TCL_OK;
}

// Error/end-of-file handling wrapped around Tcl_GetsObj
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

// Split a string up into an array of named columns. Modifies the input string.
int
Pg_sqlite_split_tabsep(char *row, char ***columnsPtr, int nColumns, char *sepStr, char *nullStr, const char **errorMessagePtr)
{
	int i;
	char *col;
	char *nextCol;
	char **columns = (char **)ckalloc(nColumns * sizeof *columns);
	int returnCode = TCL_OK;
	int sepLen = strlen(sepStr);

	col = row;
	i = 0;
	while(col && i < nColumns) {
		nextCol = strstr(col, sepStr);
		columns[i] = col;
		if(nextCol) {
			*nextCol = 0;
			nextCol += sepLen;
		}
		if (nullStr && strcmp(columns[i], nullStr) == 0)
			columns[i] = NULL;
		col = nextCol;
		i++;
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
		ckfree((void *)columns);
	}

	return returnCode;
}

// Split a string up into a key-val list, and populate a column list with the values pulled in from the list. This
// modifies the original string.
//
// TODO, add nullStr option and code to remove value from list.
int
Pg_sqlite_split_keyval(Tcl_Interp *interp, char *row, char ***columnsPtr, int nColumns, char *sepStr, char **names, Tcl_Obj *unknownObj)
{
	char *val;
	char *key;
	char *nextVal;
	int col;
	char **columns = (char **)ckalloc(nColumns * sizeof *columns);
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
			LAPPEND_STRING(interp, unknownObj, key);
			LAPPEND_STRING(interp, unknownObj, val);
		}

		val = nextVal;
	}

	if(returnCode == TCL_OK) {
		*columnsPtr = columns;
	} else {
		ckfree((void *)columns);
		Tcl_SetListObj(unknownObj, 0, NULL);
	}

	return returnCode;
}

int Pg_sqlite_getDB(Tcl_Interp *interp, char *cmdName, sqlite3 **sqlite_dbPtr)
{
        struct Tcl_CmdInfo  sqlite_commandInfo;
	Tcl_ObjCmdProc     *sqlite3_ObjProc = NULL;
        struct SqliteDb    *sqlite_clientData;

        if (!Tcl_GetCommandInfo(interp, cmdName, &sqlite_commandInfo)) {
                Tcl_AppendResult(interp, cmdName, " is not an sqlite3 handle", (char *)NULL);
                return TCL_ERROR;
        }

	// Get a known sqlite3 Tcl_ObjCmdProc
	if (Pg_sqlite_probe(interp, &sqlite3_ObjProc) != TCL_OK) {
		return TCL_ERROR;
	}

	if (sqlite3_ObjProc != sqlite_commandInfo.objProc) {
		Tcl_AppendResult(interp, "command ", cmdName, " is not an sqlite3 handle", (char *)NULL);
		return TCL_ERROR;
	}

        sqlite_clientData = (struct SqliteDb *)sqlite_commandInfo.objClientData;

        *sqlite_dbPtr = sqlite_clientData->db;

	return TCL_OK;
}

int Pg_sqlite_error_callback(Tcl_Interp *interp, Tcl_Obj *commandObj, int row, int col, const char *errorMessage)
{
	struct Tcl_Obj *command = Tcl_NewObj();

	if (Tcl_ListObjAppendList(interp, command, commandObj) != TCL_OK ||
	    Tcl_ListObjAppendElement(interp, command, Tcl_NewIntObj(row)) ||
	    Tcl_ListObjAppendElement(interp, command, Tcl_NewIntObj(col)) ||
	    Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(errorMessage, -1)))
		return TCL_ERROR;

	return Tcl_EvalObjEx(interp, command, TCL_EVAL_DIRECT);
}

// Main routine, extract the sqlite handle, parse the ensemble command, and run the subcommand.
int
Pg_sqlite(ClientData clientdata, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
        sqlite3            *sqlite_db;
	int                 cmdIndex;

	static CONST84 char *subCommands[] = {
		"info", "import_postgres_result", "write_tabsep", "read_tabsep", "read_tabsep_keylist",
		(char *)NULL
	};

	enum subCommands
	{
		CMD_INFO, CMD_IMPORT_POSTGRES_RESULT, CMD_WRITE_TABSEP, CMD_READ_TABSEP, CMD_READ_KEYVAL,
		NUM_COMMANDS
	};

        if (objc <= 2) {
                Tcl_WrongNumArgs(interp, 1, objv, "sqlite_handle command ?args?");
                return TCL_ERROR;
        }

	if(Pg_sqlite_getDB(interp, Tcl_GetString(objv[1]), &sqlite_db) != TCL_OK) {
		return TCL_ERROR;
	}

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
		argerr[CMD_READ_TABSEP] = "?-row tabsep_row? ?-file file_handle? ?-sql sqlite_sql? ?-create new_table? ?-into table? ?-as name-type-list? ?-types type-list? ?-names name-list? ?-pkey primary_key? ?-sep sepstring? ?-null nullstring? ?-replace? ?-poll_interval count? ?-check?";
		argerr[CMD_IMPORT_POSTGRES_RESULT] = "handle ?-sql sqlite_sql? ?-create new_table? ?-into table? ?-as name-type-list? ?-types type-list? ?-names name-list? ?-rowbyrow? ?-pkey primary_key? ?-null nullstring? ?-replace? ?-poll_interval count? ?-check? ?-max col varname? ?-errors callback?";
		argerr[CMD_READ_KEYVAL] = "?-row tabsep_row? ?-file file_handle? ?-create new_table? ?-into table? ?-as name-type-list? ?-names name-list? ?-pkey primary_key? ?-sep sepstring? ?-unknown colname? ?-replace? ?-poll_interval count?";
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
	int		   replaceRows = 0;
	int                pollInterval = 0;
	int		   recommitInterval = 0;
	int		   checkRow = 0;
	sqlite3_stmt      *checkStatement = NULL;
	int		  *primaryKeyIndex = NULL;
	char              *maxColumn = NULL;
	Tcl_Obj           *maxVar = NULL;
	int                maxColumnNumber = -1;
	int                maxColumnType = PG_SQLITE_NOTYPE;
	int                nocomplain = 0;
	Tcl_Obj           *errorCallback = NULL;

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
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No list provided for -types", (char *)NULL);
					return TCL_ERROR;
				}
				typeList = objv[optIndex];
				optIndex++;
			} else if (strcmp(optName, "-names") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No list provided for -names", (char *)NULL);
					return TCL_ERROR;
				}
				nameList = objv[optIndex];
				optIndex++;
			} else if (strcmp(optName, "-as") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No list provided for -as", (char *)NULL);
					return TCL_ERROR;
				}
				nameTypeList = objv[optIndex];
				optIndex++;
			} else if (strcmp(optName, "-pkey") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No list provided for -pkey", (char *)NULL);
					return TCL_ERROR;
				}
				primaryKey = objv[optIndex];
				optIndex++;
			} else if (cmdIndex != CMD_IMPORT_POSTGRES_RESULT && strcmp(optName, "-sep") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No string provided for -sep", (char *)NULL);
					return TCL_ERROR;
				}
				sepString = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex != CMD_READ_KEYVAL && strcmp(optName, "-null") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No string provided for -null", (char *)NULL);
					return TCL_ERROR;
				}
				nullString = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex == CMD_READ_KEYVAL && strcmp(optName, "-unknown") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No column provided for -unknown", (char *)NULL);
					return TCL_ERROR;
				}
				unknownKey = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex != CMD_READ_KEYVAL && strcmp(optName, "-sql") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No code provided for -sql", (char *)NULL);
					return TCL_ERROR;
				}
				sqliteCode = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (strcmp(optName, "-create") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No table provided for -create", (char *)NULL);
					return TCL_ERROR;
				}
				sqliteTable = Tcl_GetString(objv[optIndex]);
				createTable = 1;
				optIndex++;
			} else if (strcmp(optName, "-into") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No table provided for -into", (char *)NULL);
					return TCL_ERROR;
				}
				sqliteTable = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex == CMD_IMPORT_POSTGRES_RESULT && strcmp(optName, "-max") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No column provided for -max", (char *)NULL);
					return TCL_ERROR;
				}
				maxColumn = Tcl_GetString(objv[optIndex]);
				optIndex++;
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No variable provided for -max", (char *)NULL);
					return TCL_ERROR;
				}
				maxVar = objv[optIndex];
				optIndex++;
			} else if (cmdIndex != CMD_IMPORT_POSTGRES_RESULT && strcmp(optName, "-row") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No string provided for -row", (char *)NULL);
					return TCL_ERROR;
				}
				tabsepRow = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex != CMD_IMPORT_POSTGRES_RESULT && strcmp(optName, "-file") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No name provided for -file", (char *)NULL);
					return TCL_ERROR;
				}
				tabsepFile = Tcl_GetString(objv[optIndex]);
				optIndex++;
			} else if (cmdIndex == CMD_IMPORT_POSTGRES_RESULT && strcmp(optName, "-errors") == 0) {
				nocomplain = 1;
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No variable provided for -errors", (char *)NULL);
					return TCL_ERROR;
				}
				errorCallback = objv[optIndex];
				optIndex++;
			} else if (cmdIndex == CMD_IMPORT_POSTGRES_RESULT && strcmp(optName, "-rowbyrow") == 0) {
				rowbyrow = 1;
			} else if (cmdIndex != CMD_READ_KEYVAL && strcmp(optName, "-check") == 0) {
				checkRow = 1;
			} else if (strcmp(optName, "-replace") == 0) {
				replaceRows = 1;
			} else if (strcmp(optName, "-recommit") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No value provided for -recommit", (char *)NULL);
					return TCL_ERROR;
				}
				if (Tcl_GetIntFromObj(interp, objv[optIndex], &recommitInterval) == TCL_ERROR) {
					Tcl_AppendResult(interp, " in argument to '-recommit'", (char *)NULL);
					return TCL_ERROR;
				}
				if(pollInterval <= 0) // Or should this be an error?
					pollInterval = 0;
				optIndex++;
			} else if (strcmp(optName, "-poll_interval") == 0) {
				if(optIndex >= objc) {
					Tcl_AppendResult(interp, "No value provided for -poll_interval", (char *)NULL);
					return TCL_ERROR;
				}
				if (Tcl_GetIntFromObj(interp, objv[optIndex], &pollInterval) == TCL_ERROR) {
					Tcl_AppendResult(interp, " in argument to '-poll_interval'", (char *)NULL);
					return TCL_ERROR;
				}
				if(pollInterval <= 0) // Or should this be an error?
					pollInterval = 0;
				optIndex++;
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
				  early_error_exit:
					if(dropTable)
						Pg_sqlite_dropTable(interp, sqlite_db, dropTable);
					if (columnNames)
						ckfree((void *)columnNames);
					if (columnTypes)
						ckfree((void *)columnTypes);
					return TCL_ERROR;
				}
			} else if(!nColumns)  {
				if(Tcl_ListObjLength(interp, nameList, &nColumns) != TCL_OK) {
					goto early_error_exit;
				}
			}
		}

		if(!columnNames && nameTypeList) {
			if(Pg_sqlite_getNames(interp, nameTypeList, 2, &columnNames, &nColumns) != TCL_OK) {
				goto early_error_exit;
			}
		}

		if(sqliteTable) {
			if(nameTypeList) {
				if (Pg_sqlite_mapTypes(interp, nameTypeList, 1, 2, &columnTypes, &nColumns) != TCL_OK)
					return TCL_ERROR;
			} else if(!nameList) {
				Tcl_AppendResult(interp, "No template (-as) provided for -into", (char *)NULL);
				goto early_error_exit;
			}

			sqliteCode = Pg_sqlite_generate(interp, sqlite_db, sqliteTable, nameList, nameTypeList, primaryKey, unknownKey, createTable, replaceRows);
			if (!sqliteCode) {
				goto early_error_exit;
			}
			if(createTable) dropTable = sqliteTable;
		}

		if(checkRow) {
			if(!columnNames || !columnTypes || !primaryKey || !sqliteTable) {
				Tcl_AppendResult(interp, "-check requires primary key, column names, and column types", (char *)NULL);
				goto early_error_exit;
			}

			if(Pg_sqlite_generateCheck(interp, sqlite_db, sqliteTable, columnNames, nColumns, primaryKey, &checkStatement, &primaryKeyIndex) != TCL_OK) {
				goto early_error_exit;
			}
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
			goto early_error_exit;
		}

		if(Pg_sqlite_prepare(interp, sqlite_db, sqliteCode, &statement) != TCL_OK) {
			goto early_error_exit;
		}

		if(maxColumn) {
			if (!columnNames || !columnTypes) {
				Tcl_AppendResult(interp, "Can't specify -max without column names and types",  (char *)NULL);
				goto early_error_exit;
			}
			for(maxColumnNumber = 0; maxColumnNumber < nColumns; maxColumnNumber++) {
				if(strcmp(maxColumn, columnNames[maxColumnNumber]) == 0)
					break;
			}
			if(maxColumnNumber >= nColumns) {
				Tcl_AppendResult(interp, "Argument to -max not found in columns",  (char *)NULL);
				goto early_error_exit;
			}
			switch (columnTypes[maxColumnNumber]) {
				case PG_SQLITE_INT: case PG_SQLITE_DOUBLE: case PG_SQLITE_TEXT: {
					maxColumnType = columnTypes[maxColumnNumber];
					break;
				}
				default: {
					Tcl_AppendResult(interp, "Can only track maximum of integer, float, or text columns", (char *)NULL);
					goto early_error_exit;
				}
			}
		}
	}

	switch (cmdIndex) {
		case CMD_INFO: {
			char       *dbName = NULL;
			Tcl_Obj    *infoList = Tcl_NewObj();
			int         doFilename = 0;
			int	    doBusy = 0;

			optIndex = 3;

			if(objc < optIndex) {
			  info_wrong_num_args:
				Tcl_WrongNumArgs(interp, 3, objv, "?-busy? ?-filename? ?-db dbname?");
				return TCL_ERROR;
			}

			while (optIndex < objc) {
				char *optName = Tcl_GetString(objv[optIndex]);
				optIndex++;
				if (optName[0] != '-') {
					goto info_wrong_num_args;
				}

				if (strcmp(optName, "-db") == 0) {
					dbName = Tcl_GetString(objv[optIndex]);
					optIndex++;
				} else if (strcmp(optName, "-busy") == 0) {
					doBusy = 1;
				} else if (strcmp(optName, "-filename") == 0) {
					doFilename = 1;
				} else
					goto info_wrong_num_args;
			}

			// no args, do everything
			if(!doFilename && !doBusy) {
				doFilename = 1;
				doBusy = 1;
			}

			if(doFilename) {
				if(!dbName)
					dbName = "main";
				LAPPEND_STRING(interp, infoList, "filename");
				LAPPEND_STRING(interp, infoList, sqlite3_db_filename(sqlite_db, dbName));
			}

			if(doBusy) {
				sqlite3_stmt *pStmt = NULL;
				Tcl_Obj      *busyList = NULL;

				for(pStmt=sqlite3_next_stmt(sqlite_db, pStmt); pStmt; pStmt=sqlite3_next_stmt(sqlite_db, pStmt)){
					if( sqlite3_stmt_busy(pStmt) ){
						if(!busyList)
							busyList = Tcl_NewObj();
						LAPPEND_STRING(interp, busyList, sqlite3_sql(pStmt));
					}
				}

				if(busyList) {
					LAPPEND_STRING(interp, infoList, "busy");
					Tcl_ListObjAppendElement(interp, infoList, busyList);
				}
			}

			Tcl_SetObjResult(interp, infoList);
			break;
		}

		case CMD_WRITE_TABSEP: {
			Tcl_Channel channel = NULL;
			const char *channelName = NULL;
			int channelMode;
			int sqliteStatus;

			channelName = Tcl_GetString(objv[3]);
			sqliteCode = Tcl_GetString(objv[4]);

			optIndex = 5;
			nullString = "";

			if(objc < optIndex) {
			  write_wrong_num_args:
				Tcl_WrongNumArgs(interp, 3, objv, "handle sql ?-null nullstring? ?-sep sepstring? ?-poll_interval row-count?");
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
				} else if (strcmp(optName, "-poll_interval") == 0) {
					if (Tcl_GetIntFromObj(interp, objv[optIndex], &pollInterval) == TCL_ERROR) {
						Tcl_AppendResult(interp, " in argument to '-poll_interval'", (char *)NULL);
						return TCL_ERROR;
					}
					if(pollInterval <= 0) // Or should this be an error?
						pollInterval = 0;
					optIndex++;
				} else
					goto write_wrong_num_args;
			}

			channel = Tcl_GetChannel(interp, channelName, &channelMode);
			if(!channel) {
				Tcl_AppendResult(interp, Tcl_ErrnoMsg(Tcl_GetErrno()), " converting ", channelName, (char *)NULL);
				return TCL_ERROR;
			}
			if (!(channelMode & TCL_WRITABLE)) {
				Tcl_AppendResult (interp, "Channel ", channelName, " is not writable", (char *)NULL);
				return TCL_ERROR;
			}

			if(Pg_sqlite_prepare(interp, sqlite_db, sqliteCode, &statement) != TCL_OK)
				return TCL_ERROR;

			nColumns = sqlite3_column_count(statement);
			totalTuples = 0;

			returnCode = TCL_ERROR;

			if(recommitInterval) {
				if(Pg_sqlite_begin(interp, sqlite_db) == TCL_ERROR)
					goto write_tabsep_cleanup_and_exit;
			}

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
				if(recommitInterval && (totalTuples % recommitInterval) == 0) {
					if(Pg_sqlite_recommit(interp, sqlite_db, sqliteCode, &statement) != TCL_OK)
						goto write_tabsep_cleanup_and_exit;
				}
				if(pollInterval && (totalTuples % pollInterval) == 0) {
					Tcl_DoOneEvent(0);
				}
			}
			if(sqliteStatus != SQLITE_DONE) {
				Tcl_AppendResult(interp, sqlite3_errmsg(sqlite_db), (char *)NULL);
				goto write_tabsep_cleanup_and_exit;
			}
			returnCode = TCL_OK;
		  write_tabsep_cleanup_and_exit:

			if(statement) {
				sqlite3_finalize(statement);
				statement = NULL;
			}

			if(checkStatement) {
				sqlite3_finalize(checkStatement);
				checkStatement = NULL;
			}

			if(recommitInterval) {
				if(Pg_sqlite_commit(interp, sqlite_db) != TCL_OK)
					returnCode = TCL_ERROR;
			}

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
				if (!(channelMode & TCL_READABLE)) {
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

			if(recommitInterval) {
				if(Pg_sqlite_begin(interp, sqlite_db) == TCL_ERROR) {
					returnCode = TCL_ERROR;
					goto read_tabsep_cleanup_and_exit;
				}
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
					if (Pg_sqlite_split_tabsep(row, &columns, nColumns, sepString, nullString, &errorMessage) != TCL_OK) {
						returnCode = TCL_ERROR;
						break;
					}
					if(checkRow) {
						int check = Pg_sqlite_executeCheck(interp, sqlite_db, checkStatement, primaryKeyIndex, columnTypes, columns, nColumns);
						if(check == TCL_ERROR) {
							returnCode = TCL_ERROR;
							break;
						}
						if (check == TCL_CONTINUE) {
							goto next_row;
						}
					}
				}

				for(column = 0; column < nColumns; column++) {
					char *value = columns[column];
					if(!value)
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

				// Once we've imported any data, we'll keep the table.
				dropTable = NULL;

				totalTuples++;
				if(recommitInterval && (totalTuples % recommitInterval) == 0) {
					if(Pg_sqlite_recommit(interp, sqlite_db, sqliteCode, &statement) != TCL_OK) {
						returnCode = TCL_ERROR;
						goto read_tabsep_cleanup_and_exit;
					}
				}
				if(pollInterval && (totalTuples % pollInterval) == 0) {
					Tcl_DoOneEvent(0);
				}

			  next_row:
				if(tabsepFile) {
					if((returnCode = Pg_sqlite_gets(interp, tabsepChannel, &row)) == TCL_BREAK)
						returnCode = TCL_OK;
				} else {
					row = NULL;
				}
			}

		  read_tabsep_cleanup_and_exit:

			if(statement) {
				sqlite3_finalize(statement);
				statement = NULL;
			}

			if(checkStatement) {
				sqlite3_finalize(checkStatement);
				checkStatement = NULL;
			}

			if(recommitInterval) {
				if(Pg_sqlite_commit(interp, sqlite_db) != TCL_OK)
					returnCode = TCL_ERROR;
			}

			if(columnTypes)
				ckfree((void *)columnTypes);

			if(columns)
				ckfree((void *)columns);

			if(returnCode == TCL_ERROR) {
				if (errorMessage) {
					Tcl_AppendResult(interp, (char *)errorMessage, (char *)NULL);
				}

				if(dropTable) {
					if(Pg_sqlite_dropTable(interp, sqlite_db, dropTable) != TCL_OK)
						Tcl_AppendResult(interp, " while dropping table", (char *)NULL);
				}

				return TCL_ERROR;
			}

			Tcl_SetObjResult(interp, Tcl_NewIntObj(totalTuples));
			return returnCode;
		}

		case CMD_IMPORT_POSTGRES_RESULT: {
			char    *pghandle_name = Tcl_GetString(objv[3]);
			int      nTuples;
			int      tupleIndex;
			int      maxInt = 0;
			double   maxFloat = 0.0;
			char     maxString[BUFSIZ];
			int      maxValid = 0;
			int      nRows = -1;

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

			if(recommitInterval) {
				if(Pg_sqlite_begin(interp, sqlite_db) == TCL_ERROR) {
					returnCode = TCL_ERROR;
					goto import_loop_end;
				}
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
					char **columns = (char **)ckalloc(nColumns * (sizeof *columns));
					int maxColumnIsNull = 0;
					++nRows;

					for(column = 0; column < nColumns; column++) {
						if(PQgetisnull(result, tupleIndex, column)) {
							columns[column] = NULL;
							if(maxColumn && column == maxColumnNumber) {
								maxColumnIsNull = 1;
							}
						} else {
							columns[column] = PQgetvalue(result, tupleIndex, column);
							if(nullString && strcmp(columns[column], nullString) == 0)
								columns[column] = NULL;
						}
					}
					if(checkRow) {
						int check = Pg_sqlite_executeCheck(interp, sqlite_db, checkStatement, primaryKeyIndex, columnTypes, columns, nColumns);
						if(check == TCL_ERROR) {
							if(nocomplain && Pg_sqlite_error_callback(interp, errorCallback, nRows, -1, Tcl_GetStringResult(interp)) == TCL_OK) {
								Tcl_ResetResult(interp);
								continue;
							}
							returnCode = TCL_ERROR;
							ckfree((void *)columns);
							goto import_loop_end;
						}
						if (check == TCL_CONTINUE) {
							ckfree((void *)columns);
							continue;
						}
					}
					for(column = 0; column < nColumns; column++) {
						if (!columns[column])
							continue;

						int type = columnTypes ? columnTypes[column] : PG_SQLITE_TEXT;
						if (Pg_sqlite_bindValue(sqlite_db, statement, column, columns[column], type, &errorMessage) != TCL_OK) {
							if(nocomplain && Pg_sqlite_error_callback(interp, errorCallback, nRows, column, errorMessage) == TCL_OK) {
								errorMessage = NULL;
								continue;
							}
							returnCode = TCL_ERROR;
							ckfree((void *)columns);
							goto import_loop_end;
						}
					}
					if(maxColumn && !maxColumnIsNull) {
						char *val = columns[maxColumnNumber];
						switch (maxColumnType) {
							case PG_SQLITE_TEXT: {
								if(!maxValid || strcmp(val, maxString) > 0) {
									strcpy(maxString, val);
									maxValid = 1;
								}
								break;
							}
							case PG_SQLITE_INT: {
								int valInt = atoi(val);
								if(!maxValid || valInt > maxInt) {
									maxInt = valInt;
									maxValid = 1;
								}
								break;
							}
							case PG_SQLITE_DOUBLE: {
								double valFloat = atof(val);
								if(!maxValid || valFloat > maxFloat) {
									maxFloat = valFloat;
									maxValid = 1;
								}
								break;
							}
						}
					}
					ckfree((void *)columns);
					columns = NULL;

					if (sqlite3_step(statement) != SQLITE_DONE) {
						errorMessage = sqlite3_errmsg(sqlite_db);
						if(nocomplain && Pg_sqlite_error_callback(interp, errorCallback, nRows, -1, errorMessage) == TCL_OK) {
							errorMessage = NULL;
						} else {
							returnCode = TCL_ERROR;
							goto import_loop_end;
						}
					}
					sqlite3_reset(statement);
					sqlite3_clear_bindings(statement);

					// Once we've imported any data, we'll keep the table.
					dropTable = NULL;

					totalTuples++;
					if(recommitInterval && (totalTuples % recommitInterval) == 0) {
						if(Pg_sqlite_recommit(interp, sqlite_db, sqliteCode, &statement) != TCL_OK) {
							returnCode = TCL_ERROR;
							goto import_loop_end;
						}
					}
					if(pollInterval && (totalTuples % pollInterval) == 0) {
						Tcl_DoOneEvent(0);
					}
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

			if(statement) {
				sqlite3_finalize(statement);
				statement = NULL;
			}

			if(checkStatement) {
				sqlite3_finalize(checkStatement);
				checkStatement = NULL;
			}

			if(recommitInterval) {
				if(Pg_sqlite_commit(interp, sqlite_db) != TCL_OK)
					returnCode = TCL_ERROR;
			}

			if(columnTypes)
				ckfree((void *)columnTypes);

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

			if(maxColumn) {
				if(maxValid) {
					Tcl_Obj *obj = NULL;

					switch (maxColumnType) {
						case PG_SQLITE_TEXT: {
							obj = Tcl_NewStringObj(maxString, -1);
							break;
						}
						case PG_SQLITE_INT: {
							obj = Tcl_NewIntObj(maxInt);
							break;
						}
						case PG_SQLITE_DOUBLE: {
							obj = Tcl_NewDoubleObj(maxFloat);
							break;
						}
					}

					if(obj)
						Tcl_ObjSetVar2(interp, maxVar, NULL, obj, 0);
				}
			}

			Tcl_SetObjResult(interp, Tcl_NewIntObj(totalTuples));
			return returnCode;
		}
	}

	return TCL_OK;
}

