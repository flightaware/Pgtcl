#
# this code generates an Itcl class based on fields found in a postgres table
#
# it then does a select and instantiates the table from an object
#
# $Id$
#
#

source dbtable.tcl

#
# gen_table_base_class - return source code to generate an Itcl class
#  for the specified table, from the specified database connection
#
proc gen_table_base_class {conn tableName} {
    set result "::itcl::class DB-$tableName {\n"
    append result "    inherit db_table\n\n"

    set res [pg_exec $conn "select * from $tableName limit 1"]
    set varList ""
    foreach var [lsort [pg_result $res -attributes]] {
	append result "    public variable $var\n"
	lappend varList $var
    }
    pg_result $res -clear

    append result "\n"

    append result "    common fields [list $varList]\n\n"

    append result "    constructor {args} {\n        eval configure \$args\n    }\n\n"

    append result "    method fields {} {\n        return \$fields\n    }\n\n"

    append result "    method table {} {\n        return [list $tableName]\n    }\n\n"

    append result "    method values {} {\n    return \[list"
    foreach var $varList {
	append result " \$$var"
    }
    append result "]\n    }\n\n"
    append result "}\n"
}

