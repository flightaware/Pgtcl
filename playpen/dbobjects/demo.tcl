#
# this code generates an Itcl class based on fields found in a postgres table
#
# it then does a select and instantiates the table from an object
#
# $Id$
#
#

source gen_db_objects.tcl

if 0 {
set baseClass [gen_table_base_class pg_type]
} else {
set baseClass [gen_table_base_class pp_users]
}
puts $baseClass
eval $baseClass

package require sc-sqlobj

::sqlobj::SQLtable MySqlTable

if 0 {

MySqlTable instantiate -selectStatement "select * from pg_type" -dataKeyFields typname -objectType DB-pg_type

puts [::itcl::find objects -class DB-pg_type]
}

if 0 {
foreach obj [::itcl::find objects -class DB-pg_type] {
    puts $obj
    puts [$obj configure]
    puts ""
}

}

MySqlTable instantiate -selectStatement "select * from pp_users" -dataKeyFields "address neighborhood" -objectType DB-pp_users -namespace ::

puts [::itcl::find objects -class DB-pp_users]
