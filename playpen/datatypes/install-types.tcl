#
# playpen entry to mess with data types
#
# $Id$
#

package require Pgtcl

#
# fetch_types - fetch all the datatypes from the database and store
#  the data types in oidTypeCache, indexed by type number
#
proc fetch_types {conn} {
    global oidTypeCache

    pg_select $conn "select oid,typname from pg_type" data {
        set oidTypeCache($data(oid)) $data(typname)
    }

#puts "Here are all the types..."
#parray oidTypeCache
    #puts [array get ::oidTypeCache]
}

#
# table_types - given a connecvtion and a table name, return as a list the
#  names of the elements in the and each one's datatype
#
proc table_types {conn tableName} {
    set res [pg_exec $conn "select * from $tableName limit 1"]
    set result ""
    foreach triple [pg_result $res -lAttributes] {

        #lassign $triple name oid size
	set name [lindex $triple 0]
	set oid [lindex $triple 1]
	set size [lindex $triple 2]

        lappend result $name $::oidTypeCache($oid)
    }
    pg_result $res -clear
    return $result
}

