#
#   set this to your specific location
#
array set conninfo {
    host    localhost
    port    5432
    dbname  ####
    user    ####
}

# Or copy and fill out the above datastructure into ~/.conninfo

if {[file exists [file join $env(HOME) .conninfo.tcl]]} {
    source [file join $env(HOME) .conninfo.tcl]
}

