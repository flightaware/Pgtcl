#
# base class for dbobjects
#
# $Id$
#

::itcl::class db_table {

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

    method gen_insert {} {
	set result "insert into [$this table] ([join [$this fields] ","]) values ("
        
	foreach value [$this values] {
	    append result "[pg_quote $value],"
	}
	return "[string range $result 0 end-1]);"
    }
}
