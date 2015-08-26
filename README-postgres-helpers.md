
Welcome to the Postgres helpers functions.

These are actually quite soild.  They've been in use for years.

They are installed as part of *make install* from the top-level directory of the Pgtcl repo.

To make them available to your Tcl program, execute

```tcl
package require sc_postgres
```

* **sc_pg::foreach_tuple** *pgResult* *arrayName* *body*

    Given a postgres result, an array name, and a code body, fill the array in turn with each result tuple and execute the code body against it.

	This is largely superseded by the new Pgtcl pg_result -foreach option.


* **sc_pg::quote** *string*

    Quote a string for Postgres.  Puts single quotes around it and quotes single quotes if they're contained within it. It's just a synonym for pg_quote.

* **sc_pg::gen_insert_from_array** *tableName* *array*

    Return a postgres SQL insert statement based on the specified table and the contents of the specified array.


* **sc_pg::gen_update_from_array** *tableName* *array* *keyFieldList*

    Return a postgres SQL update statement based on the contents of an array.  Key field list specifies the keys and their values must be in the array.  The where clause is generated as a big "and", so all the keys in the key field list must match for a row to be updated.

* **sc_pg::perform_insert** *session* *insertStatement*

    Execute a statement on the given database session.  Grab the status out of the result.  Clear the result.  Return the status.

* **sc_pg::perform_update_from_array** *session* *tableName* *array* *keyFieldList*

    Generate an update statement and execute it on the given session.  Grab the status out of the result.  Clear the result.  Return the status.

* **sc_pg::gen_insert_from_lists** *tableName* *nameList* *valueList*

    Generate a sql insert command based on the contents of an element list and a one-for-one corresponding value list, and return it.

* **sc_pg::perform_insert_from_array** *session* *tableName* *arrayName*

    Generate a sql insert command based on the contents of an array and execute it against the specified database session.

* **sc_pg::clock_to_sql_time** *clockValue*

    Convert a clock value (integer seconds since 1970) to a SQL standard abstime value, accurate to a day.

* **sc_pg::clock_to_precise_sql_time** *clockValue*

    Generate a SQL time from an integer clock time (seconds since 1970), accurate to the second, with timezone.

* **sc_pg::clock_to_precise_sql_time_without_timezone** *clockValue*

    Generate a SQL time from an integer clock time (seconds since 1970), accurate to the second, without timezone.

* **sc_pg::sql_time_to_clock** *sqlDate*

    Given a SQL standard abstime value, convert it to an integer clock value (seconds since 1970) and return it.

* **sc_pg::res_must_succeed** *$resultHandle*

    The given postgres result must be PGRES_COMMAND_OK and, if it isn't, throw an error.  If it is OK, clear the postgres result.

* **sc_pg::res_dont_care** *$resultHandle*

    Whether or not the give postgres result is PGRES_COMMAND_OK, the result is cleared.  By default we write something to stdout, but that's probably dumb.

