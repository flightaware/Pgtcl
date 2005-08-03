#
# pggrok - code to introspect the postgres database
#
# Copyright (C) 2004 Karl Lehenbauer
#
#

package require Pgtcl

package provide pggrok 1.0

namespace eval pggrok {

#
# tables -- return a list of the names of all the tables in the database
#
proc tables {conn} {
    set result ""
    pg_execute -array data $conn {
	select c.relname as name from pg_catalog.pg_class c 
	left join pg_catalog.pg_user u on u.usesysid = c.relowner
	left join pg_catalog.pg_namespace n on n.oid = c.relnamespace
	where c.relkind in ('r','')
	and n.nspname not in('pg_catalog', 'pg_toast')
	and pg_catalog.pg_table_is_visible(c.oid)
	order by 1
    } {
	lappend result $data(name)
    }

    return $result
}

#
# schema -- return a list of the names of all the schema in the database
#
proc schema {conn} {
    set result ""
    pg_execute -array data $conn {
	SELECT c.relname as name
	FROM pg_catalog.pg_class c
	     LEFT JOIN pg_catalog.pg_user u ON u.usesysid = c.relowner
	     LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
	WHERE c.relkind IN ('S','')
	      AND n.nspname NOT IN ('pg_catalog', 'pg_toast')
	      AND pg_catalog.pg_table_is_visible(c.oid)
	ORDER BY 1;
    } {
	lappend result $data(name)
    }

    return $result
}

#
# users -- return a list of the names of all the users in the database
#
proc users {conn} {
    set result ""
    pg_execute -array data $conn {
	SELECT u.usename AS name
	FROM pg_catalog.pg_user u
	ORDER BY 1;
    } {
	lappend result $data(name)
    }

    return $result
}

#
# views -- return a list of the names of all the views in the datbase
#
proc views {conn} {
    set result ""
    pg_execute -array data $conn {
	SELECT c.relname as "name"
	FROM pg_catalog.pg_class c
	     LEFT JOIN pg_catalog.pg_user u ON u.usesysid = c.relowner
	     LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
	WHERE c.relkind IN ('v','')
	      AND n.nspname NOT IN ('pg_catalog', 'pg_toast')
	      AND pg_catalog.pg_table_is_visible(c.oid)
	ORDER BY 1;
    } {
	lappend result $data(name)
    }

    return $result
}

#
# table_to_oid -- given a connection and a table name, return thew OID of that
# table
#
proc table_to_oid {conn table} {
    set cmd {
	SELECT c.oid FROM pg_catalog.pg_class c
	    LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
	    WHERE pg_catalog.pg_table_is_visible(c.oid) 
	    AND c.relname ~ '^%s$'
    }
    set result ""
    pg_execute -array data $conn [format $cmd $table] {
	lappend result $data(oid)
    }

    return $result
}

#
# table_attributes -- given a connection and a table name, fill the
# specified array name with elements containing data about each
# field in turn, executing the code body on the result
#
proc table_attributes {conn table arrayName codeBody} {
    upvar $arrayName data
    set oid [table_to_oid $conn $table]

    set cmd {
	SELECT a.attname,
	  pg_catalog.format_type(a.atttypid, a.atttypmod),
	  (SELECT substring(d.adsrc for 128) FROM pg_catalog.pg_attrdef d
	   WHERE d.adrelid = a.attrelid AND d.adnum = a.attnum AND a.atthasdef),
	  a.attnotnull, a.attnum
	FROM pg_catalog.pg_attribute a
	WHERE a.attrelid = '%s' AND a.attnum > 0 AND NOT a.attisdropped
	ORDER BY a.attnum
    }

    pg_execute -array data $conn [format $cmd $oid] {
	set data(default) $data(?column?)
	unset data(?column?)
	uplevel $codeBody
    }
}

#
# indices -- given a connection handle and a table name, fill the specified
# array name with elements containing data about each index defined for the
# table in turn, executing the code body on each result
#
# if there are no indexes, the code body will not be executed
#
proc indices {conn table arrayName codeBody} {
    upvar $arrayName data
    set oid [table_to_oid $conn $table]

    set cmd {
	SELECT c2.relname, i.indisprimary, i.indisunique, 
            pg_catalog.pg_get_indexdef(i.indexrelid)
	    FROM pg_catalog.pg_class c, pg_catalog.pg_class c2, 
            pg_catalog.pg_index i
	WHERE c.oid = '%s' AND c.oid = i.indrelid AND i.indexrelid = c2.oid
	ORDER BY i.indisprimary DESC, i.indisunique DESC, c2.relname
    }

    pg_execute -array data $conn [format $cmd $oid] {
	uplevel $codeBody
    }
}

proc dump {conn} {

    puts "TABLES"
    puts [::pggrok::tables $conn]
    puts ""

    puts "SCHEMAS"
    puts [::pggrok::schema $conn]
    puts ""


    puts "USERS"
    puts [::pggrok::users $conn]
    puts ""

    puts "VIEWS"
    puts [::pggrok::views $conn]
    puts ""
}

}

