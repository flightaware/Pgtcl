#
# program to insert the sampledata.txt data set into pgtest_people
#  using a prepared statement.
#
# $Id$
#

package require Pgtcl

proc doit {} {
    set fp [open sampledata.txt]
    set conn [pg_connect -conninfo ""]

    set result [pg_exec $conn {prepare pgtest_insert_people (varchar, varchar, varchar, varchar, varchar, varchar) as insert into pgtest_people values ($1, $2, $3, $4, $5, $6);}]
    if {[pg_result $result -status] != "PGRES_COMMAND_OK"} {
	puts "[pg_result $result -error] preparing statement"
	exit 1
    }

    while {[gets $fp line] >= 0} {
	set result [pg_exec_prepared $conn pgtest_insert_people [lindex $line 0] [lindex $line 1] [lindex $line 2] [lindex $line 3] [lindex $line 4] [lindex $line 5]]
        if {[pg_result $result -status] != "PGRES_COMMAND_OK"} {
	    puts "[pg_result $result -error] inserting '$line'"
	}
	pg_result $result -clear
    }
}

if !$tcl_interactive doit



