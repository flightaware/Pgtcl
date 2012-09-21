#
# program to compare two files created by dump.tcl and list tables that
# are in the first database but not the second, tables in the second but
# not the first, and, for tables in both databases, any fields that have
# been added, fields that have been removed, and information about any
# fields that have changed
#
# $Id$
#

package require Tclx

set suppressColumnChanges 1

proc compare_element {tableName what listVar0 listVar1 name} {
    upvar ${listVar0}::${name} array0
    upvar ${listVar1}::${name} array1

    set somethingChanged 0

    foreach varName [array names array0] {
	if {$::suppressColumnChanges && $varName == "attnum"} {
	    continue
	}
	if {$array0($varName) != $array1($varName)} {
	    puts "Changed $tableName $what \"$name\", $varName from \"$array0($varName)\" to \"$array1($varName)\""
	    set somethingChanged 1
	}
    }

    return $somethingChanged
}

proc compare_table_info {tableName what namespace0 namespace1} {
    set listVar0 ${namespace0}::${tableName}::${what}
    set listVar1 ${namespace1}::${tableName}::${what}

    set list0 [set $listVar0]
    set list1 [set $listVar1]

    #puts "comparing $tableName $what"
    #puts "    $listVar0"
    #puts "    $listVar1"
    #puts "    $list0"
    #puts "    $list1"
    #puts ""

    lassign [intersect3 $list0 $list1] deleted inBoth added

    set somethingChanged 0

    if {![lempty $deleted]} {
	puts "-- Deleted $tableName $what \"$deleted\""
	set somethingChange 1

	if {$what == "fields"} {
	    foreach field $deleted {
		puts "alter table $tableName drop column $field;"
	    }
	    puts ""
	}
    }

    if {![lempty $added]} {
	puts "-- Added $tableName $what \"$added\""
	set somethingChange 1

	if {$what == "fields"} {
	    foreach field $added {
		set formatType [set ${namespace1}::${tableName}::fields::${field}(format_type)]
		puts "alter table $tableName add column $field $formatType;"

		set null [set ${namespace1}::${tableName}::fields::${field}(attnotnull)]

		if {$null == "t"} {
		    puts "alter table $tableName set not null;"
		}

		# handle default values
		set default [set ${namespace1}::${tableName}::fields::${field}(default)]

		if {$default != ""} {
		    puts "alter table $tableName set default $default;"
		}

	    }
	}
	puts ""
    }

    foreach var $inBoth {
	if {[compare_element $tableName $what $listVar0 $listVar1 $var]} {
	    set somethingChanged 1
	}
    }

    if $somethingChanged {
	puts ""
    }
}

proc compare_dumpfiles {file0 file1} {
    namespace eval ::file0 "source $file0"
    namespace eval ::file1 "source $file1"

    set tableComp [intersect3 $::file0::tables $::file1::tables]
    set inFirstOnly [lindex $tableComp 0]
    set inSecondOnly [lindex $tableComp 2]
    set inBoth [lindex $tableComp 1]

    if {![lempty $inFirstOnly]} {
	puts "in first only: $inFirstOnly"
	puts ""
    }

    if {![lempty $inSecondOnly]} {
	puts "in second only: $inSecondOnly"
	puts ""
    }

    foreach table $inBoth {
	compare_table_info $table fields ::file0 ::file1
	compare_table_info $table indices ::file0 ::file1
    }
}

proc main {argv} {
    if {[llength $argv] != 2} {
	puts stderr "usage: $::argv0 dumpfile1 dumpfile2"
	exit 1
    }
    compare_dumpfiles [lindex $argv 0] [lindex $argv 1]
}

if !$tcl_interactive {main $argv}
