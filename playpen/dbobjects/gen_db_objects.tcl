#
# this code generates an Itcl class based on fields found in a postgres table
#
# it then does a select and instantiates the table from an object
#
# $Id$
#
#

package require pp-provision
pp_gimme_DIO

source dbtable.tcl

proc gen_table_base_class {tableName} {
    set result "::itcl::class DB-$tableName {\n"
    append result "    inherit db_table\n\n"
    DIO forall "select * from $tableName limit 1" data {
	set varList ""
	foreach var [lsort [array names data]] {
	    append result "    public variable $var\n"
	    lappend varList $var
	}
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
    }
    append result "}\n"
}

