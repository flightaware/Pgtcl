#
# program to insert the sampledata.txt data set into pgtest_people
# using the new-fangled connection and result objects
#
# $Id$
#

package require Pgtcl

proc doit {} {
    set fp [open sampledata.txt]
    set conn [pg_connect -conninfo ""]

    while {[gets $fp line] >= 0} {
	set statement "insert into pgtest_people values ([pg_quote [lindex $line 0]], [pg_quote [lindex $line 1]], [pg_quote [lindex $line 2]], [pg_quote [lindex $line 3]], [pg_quote [lindex $line 4]], [pg_quote [lindex $line 5]]);"

	set result [$conn exec $statement]
        if {[$result -status] != "PGRES_COMMAND_OK"} {
	    puts "[pg_result $result -error] executing '$statement'"
	}
	$result -clear
    }
}

if !$tcl_interactive doit

