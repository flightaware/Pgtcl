#
# Copyright (C) 1996 NeoSoft.
#
# Permission to use, copy, modify, and distribute this software and its
# documentation for any purpose and without fee is hereby granted, provided
# that the above copyright notice appear in all copies.  NeoSoft makes no 
# representations about the suitability of this software for any purpose.
# It is provided "as is" without express or implied warranty.
#
#
# Copyright (C) 2003 Proc Place.
#  Berkeley copyright as above.
#
# Copyright (C) 2004 Superconnect, Ltd.
#  Berkeley copyright as above.
#

#
# postgres interface stuff
#
# $Id$
#

package provide sc_postgres 1.0

package require Tclx
package require Pgtcl

namespace eval sc_pg {

#
# foreach_tuple - given a postgres result, an array name, and a code
#  body, fill the array in turn with each result tuple and execute
#  the code body against it.
#
proc foreach_tuple {res arrayName body} {
    upvar $arrayName $arrayName

    set numTuples [pg_result $res -numTuples]
    for {set i 0} {$i < $numTuples} {incr i} {
	pg_result $res -tupleArray $i $arrayName
	uplevel 1 $body
    }
}

#
# quote - make string legally quoted for postgres  
# (obsoleted by pg_quote... it used to do it with a bunch of regexps)
#
proc quote_sql {string} {
    return [pg_quote $string]
}

#
# gen_sql_insert_from_array - return a sql insert statement based on the
#   contents of an array
#
proc gen_insert_from_array {tableName arrayName} {
    upvar $arrayName array

    set nameList [array names array]

    set result "insert into $tableName ([join $nameList ","]) values ("

    foreach name $nameList {
	append result "[pg_quote $array($name)],"
    }
    return "[string range $result 0 end-1]);"
}

#
# gen_insert_front_part - generate a sql insert front part
#
proc gen_insert_front_part {tableName nameList} {

    return "insert into $tableName ([join $nameList ","]) values ("
}

#
# gen_isnert_back_part - generate a sql insert back part
#
proc gen_insert_back_part {valueList} {
    set result ""
    foreach value $valueList {
	append result "[pg_quote $value],"
    }
    return "[string range $result 0 end-1]);"
}

#
# gen_insert_simplex_front_part - generate a sql insert command based on 
#  contents of a properly ordered list (fields same as the order in the table)
#
proc gen_insert_simplex_front_part {tableName} {
    set result "insert into $tableName values ("
}

#
# perform_insert - generate a sql insert command based on the contents
# of an array and execute it against the specified database session
#
proc perform_insert {session insertStatement} {
    set result [pg_exec $session $insertStatement]
    set status [pg_result $result -status]
    pg_result $result -clear
    return $status
}

#
# gen_insert_from_lists - generate a sql insert command based on the
# contents of an element list and a corresponding value list
#
proc gen_insert_from_lists {tableName nameList valueList} {

    set result "insert into $tableName ([join $nameList ","]) values ("

    foreach value $valueList {
	append result "[pg_quote $value],"
    }
    return "[string range $result 0 end-1]);"
}

#
# perform_insert_from_lists - generate a sql insert command based on the
# contents of an element list and a corresponding value list 
# and execute it against the specified database session
#
proc perform_insert_from_lists {session tableName nameList valueList} {
    set result [pg_exec $session [gen_insert_from_lists $tableName $nameList $valueList]]
    set status [pg_result $result -status]
    pg_result $result -clear
    return $status
}

#
# perform_insert_from_array - generate a sql insert command based on the
# contents of an array and execute it against the specified database session
#
proc perform_insert_from_array {session tableName arrayName} {
    upvar $arrayName array
    set result [pg_exec $session [gen_insert_from_array $tableName array]]
    set status [pg_result $result -status]
    pg_result $result -clear
    return $status
}

#
# clock_to_sql_time - convert a clock value (integer seconds since 1970) to a 
# sql standard abstime value, accurate to a day.
#
#        Month  Day [ Hour : Minute : Second ]  Year [ Timezone ]
#
proc clock_to_sql_time {clock} {
    return [clock format $clock -format "%b %d %Y"]
}

#
# clock_to_precise_sql_time - generate a SQL time from an integer clock
#  time (seconds since 1970), accurate to the second.
#
proc clock_to_precise_sql_time {clock} {
    return [clock format $clock -format "%b %d %H:%M:%S %Y GMT" -gmt 1]
}

#
# convert a sql standard abstime value to a clock value (integer
# seconds since 1970)
#
proc sql_time_to_clock {date} {
    if {$date == ""} {
	return 0
    }
    return [clock scan $date]
}

#
# res_must_succeed - a postgres result must be PGRES_COMMAND_OK and
# if not throw an error, and if so, clear the postgres result.
#
proc res_must_succeed {res} {
    if {[pg_result $res -status] != "PGRES_COMMAND_OK"} {
	error "[pg_result $res -error]"
    }
    pg_result $res -clear
}

#
# res_dont_care - any postgres result is OK, we don't care,
# clear the postgres result and return.
#
proc res_dont_care {res} {
    if {[pg_result $res -status] != "PGRES_COMMAND_OK"} {
	puts "[pg_result $res -error] (ignored)"
    }
    pg_result $res -clear
}

}

