#
# program to dump info about tables and indices for a database, into a format
# that can be sourced back into tcl and will create a nested hierachy of
# namespaces and arrays representing tables and fields and indexes within
# tables
#
# $Id$
#

source pggrok.tcl

proc load_database_metadata {db} {
    set tables [::pggrok::tables $db]
    puts "set tables [list $tables]"
    puts ""

    foreach table $tables {
	puts "# TABLE $table"
	puts "namespace eval $table {"

	puts "    # fields"
	puts "    namespace eval fields {"
	set fields ""
	::pggrok::table_attributes $db $table field {
	    set name "$field(attname)"
	    unset field(attname)
	    puts "    array set $name [list [array get field]]"
	    lappend fields $name
	}
	puts "    }"
	puts ""
	puts "    set fields [list $fields]"
	puts ""

	puts "    # indices"
	puts "    namespace eval indices {"
	set indices ""
	::pggrok::indices $db $table index {
	    set name "$index(relname)"
	    unset index(relname)
	    puts "    array set $name [list [array get index]]"
	    lappend indices $name
	}
	puts "    }"
	puts ""
	puts "    set indices [list $indices]"
	puts ""

	puts "}"
	puts ""
    }
}

proc main {argv} {
    if {[llength $argv] != 1} {
	puts stderr "usage: $::argv0 dbstring"
	exit 1
    }
    set connectString [lindex $argv 0]
    set db [pg_connect $connectString]
    load_database_metadata $db
    pg_disconnect $db
}

if !$tcl_interactive {main $argv}
