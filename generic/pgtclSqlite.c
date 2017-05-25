#include <ctype.h>
#include <string.h>
#include <libpq-fe.h>
#include <assert.h>

#include "pgtclCmds.h"
#include "pgtclId.h"
#include "libpq/libpq-fs.h"		/* large-object interface */

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
}

int
Pg_sqlite3(ClientData clientdata, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
        char               *sqlite_commandName;
        struct Tcl_CmdInfo  sqlite_commandInfo;
        struct SqliteDb     sqlite_clientData;
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
                Tcl_WrongNumArgs(interp, 1, objv, "sqlite_handle command ?args?")
                return TCL_ERROR;
        }

        sqlite_commandName = Tcl_GetString(objv[1]);

        if (!Tcl_GetCommandInfo(interp, sqlite_commandName, &sqlite_commandInfo)) {
                Tcl_AppendResult(interp, sqlite_commandName, " is not a sqlite3 handle.", (char *)NULL);
                return TCL_ERROR;
        }

        sqlite_clientData = (struct SqliteDb *)sqlite_commandInfo.objClientData;

        sqlite_db = sqlite_clientData->db;

	if (Tcl_GetIndexFromObj(interp, objv[2], subCommands, "command", TCL_EXACT, &cmdIndex) != TCL_OK)
		return TCL_ERROR;

	switch (cmdIndex) {
		case CMD_FILENAME: {
			char *sqlite_dbname;
			char *sqlite_filename;

			if(objc == 3) {
				sqlite_dbname = "main";
			} else if (objc == 4) {
				sqlite_dbname = Tcl_GetString(objv[3]);
			} else {
				Tcl_WrongNumArgs(interp, 3, objv, "?dbname?");
				return TCL_ERROR;
			}

			sqlite_filename = sqlite_db_filename(sqlite_db, sqlite_dbname);

			Tcl_AppendResult(interp, sqlite_filename, (char *)NULL);
			return TCL_OK;
		}
	}
}

