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

	append result "    public variable fields [list $varList]\n\n"

	append result "    constructor {args} {\n        eval configure \$args\n    }\n"
    }
    append result "\n}"
}

