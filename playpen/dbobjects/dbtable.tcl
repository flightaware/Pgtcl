#
# base class for dbobjects
#
# $Id$
#

::itcl::class db_table {
    public variable fields

    constructor {args} {
	eval configure $args
    }

    method publics {} {
	puts [configure]
    }

    method dump {} {
	foreach varSet [configure] {
	    puts "[lindex $varSet 0] -> [lindex $varSet 2]"
	}
    }

    method fields {} {
	return $fields
    }

    method gen_insert {} {
	set result "insert into $tableName ([join $fields ","]) values ("
        
	foreach field $fields {
	    append result "[pg_quote [set $field]],"
	}
	return "[string range $result 0 end-1]);"
    }
}
