#
# pggrok - code to introspect the postgres database
#
# Copyright (C) 2004 Karl Lehenbauer
#
#


package require Pgtcl

source pggrok.tcl

set conn [pg_connect www]
::pggrok::dump $conn

#puts [::pggrok::table_to_oid $conn sc_hyperconnect_text]

puts "table attributes sc_hyperconnect_text"
::pggrok::table_attributes $conn sc_hyperconnect_text
puts ""

puts "indices sc_hyperconnect_text"
::pggrok::indices $conn sc_hyperconnect_text
