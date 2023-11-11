package require tspec
package require try
package require sdbtcl

array set opts [tspec::init -host localhost -database MAXDB -user MONA -password RED]

proc count_sessions_from_this_process {} {
    set session_count_result [exec $::env(SQLCLI) \
        -n $::opts(-host) -d $::opts(-database) -u $::opts(-user),$::opts(-password) -xaAj \
        "SELECT Count(*) FROM sessions WHERE applicationprocess=[pid]" \
    ]
    return [lindex [split $session_count_result ,] 0]
}

proc read_file { fileName args } {
    set f [open $fileName]
    try {
        if {[llength $args] > 0} {
            fconfigure $f {*}$args
        }
        set text [read $f]
    } finally {
        close $f
    }
    return $text
}

proc save_as_file { text fileName args } {
    set f [open $fileName w]
    try {
        if {[llength $args] > 0} {
            fconfigure $f {*}$args
        }
        puts -nonewline $f $text
    } finally {
        close $f
    }
}

describe "TCL extension" {
    it "creates sdb command" {
        expect "sdb command is created" {
            set commands [info commands sdb]
            expr { [llength $commands] == 1 && [lindex $commands 0] == "sdb" }
        }
    }
}

describe "sdb command" {

    it "returns SQLDBC library version" {
        set version [sdb version]
        expect "library version" {
            regexp -nocase {^libSQLDBC\s\d+\.\d+\.\d+\s+Build\s\d{3,3}-\d{3,3}-\d{3,3}-\d{3,3}} $version
        }
    }

    it "creates database connections" {
        sdb connect db {*}[array get ::opts]
        assert "database session is open" [count_sessions_from_this_process] == 1
    }

    it "requires a unique database command name" {
        set err [catch {sdb connect "" {*}[array get ::opts]} res]
        expect "connection failed" {
            expr { $err == 1 && $res eq "database command name is required" }
        }
        set err [catch {sdb connect db {*}[array get ::opts]} res]
        expect "connection failed" {
            expr { $err == 1 && $res eq "command db already exists" }
        }
    }

    it "can create a preconfigured database connection" {
        if {[llength [info commands db]] == 1} {
            db disconnect
        }
        assert "database session is closed" [count_sessions_from_this_process] == 0

        sdb connect db {*}[array get ::opts] -autocommit on -isolationlevel "REPEATABLE READ" -compname "R TCL" -chopblanks 1
        assert "database session is open" [count_sessions_from_this_process] == 1
        expect "autocommit is ON" {
            expr { !![db configure -autocommit] }
        }
        assert "isolation level is REPEATABLE READ" [db configure -isolationlevel] eq "REPEATABLE READ"
        assert "custom componnet name is R TCL" [db configure -compname] eq "R TCL"
        expect "'chop blanks' is ON" {
            expr { !![db configure -chopblanks] }
        }
        assert "initial SQL Mode is INTERNAL" [db configure -sqlmode] eq "INTERNAL"
        assert "initial APPLICATION is ODB" [db configure -application] eq "ODB"

        db disconnect
    }

    it "accepts XUSER key to connect to a database" {
        assert "database session is closed" [count_sessions_from_this_process] == 0
        sdb connect db -key $::opts(-user) ;# for simplicity XUSER key is created to match the test user ID
        assert "database session is open" [count_sessions_from_this_process] == 1
        db disconnect
    }

    epilogue {
        if {[llength [info commands db]] == 1} {
            db disconnect
        }
    }
}

describe "Database Connection" {
    prologue {
        if {[llength [info commands db]] == 0} {
            sdb connect db {*}[array get ::opts]
        }
    }

    it "is connected and usable" {
        # puts "unicode: db=[db is unicodedatabase`], conn=[db configure -unicode]"
        expect "is connected" {
            expr { [db is connected] }
        }
        expect "is usable" {
            expr { [db is usable] }
        }
    }

    it "returns information about the server" {
        set ver [db get kernelversion]
        set majorVer [expr int($ver/10000)]
        assert "major kernel verion is 7" $majorVer == 7
        set fmt [db get datetimeformat]
        assert "date-time has a known format" $fmt in {INTERNAL ISO USA Europe Japan Oracle ANSI}
    }

    it "can be (re)configured" {
        db configure -autocommit on -isolationlevel "REPEATABLE READ"
        expect "autocommit is ON" {
            expr { !![db configure -autocommit] }
        }
        assert "isolation level is REPEATABLE READ" [db configure -isolationlevel] eq "REPEATABLE READ"
        db configure -autocommit off -isolationlevel "READ COMMITTED"
    }

    it "directly executes SQLs that do not have parameters" {
        set numRows [db execute "SELECT * FROM dual"]
        assert "one row in the result set" $numRows == 1
        set numCols [db columns -count]
        set colLabels [db column -labels]
        expect "one column name which is DUMMY" {
            expr { [llength $colLabels] == 1 && [lindex $colLabels 0] eq "DUMMY" }
        }
        assert "one column in the result set" $numCols == 1
        assert "row is fetched" [db fetch -asarray row] == 1
        assert "rows has one column" [array size row] == 1
        assert "column value is 'a'" $row(DUMMY) eq "a"
    }

    it "disconnects from the database" {
        assert "database session is open" [count_sessions_from_this_process] == 1
        db disconnect
        assert "database session is closed" [count_sessions_from_this_process] == 0
        assert "database command is deleted" [llength [info commands db]] == 0
        # restore connection for other tests
        sdb connect db {*}[array get ::opts]
    }

    it "disconnects from the database when command is deleted" {
        assert "database session is open" [count_sessions_from_this_process] == 1
        rename db {}
        assert "database session is closed" [count_sessions_from_this_process] == 0
    }

    epilogue {
        if {[llength [info commands db]] == 1} {
            db disconnect
        }
    }
}

describe "Unprepared Statement" {
    prologue {
        if {[llength [info commands db]] == 0} {
            # The HOTEL tables use CHAR, hence the need for "-chopblanks"
            sdb connect db {*}[array get ::opts] -chopblanks 1
        }
    }

    it "is created by the connection command" {
        set stmt [db newstatement]
        set numRows [db execute $stmt "SELECT * FROM dual"]
        assert "one row in the result set" $numRows == 1
        set numCols [db columns $stmt -count]
        set colLabels [db column $stmt -labels]
        expect "one column name which is DUMMY" {
            expr { [llength $colLabels] == 1 && [lindex $colLabels 0] eq "DUMMY" }
        }
        assert "one column in the result set" $numCols == 1
        assert "row is fetched" [db fetch -asarray $stmt row] == 1
        assert "rows has one column" [array size row] == 1
        assert "column value is 'a'" $row(DUMMY) eq "a"
    }

    it "executes single SQL statement" {
        set stmt [db newstatement]
        set numRows [db execute $stmt "SET CURRENT_SCHEMA=hotel"]
        assert "no rows were affected" $numRows == 0
    }

    it "executes batches of SQL" {
        set stmt [db newstatement]
        set batch {
            {INSERT INTO city VALUES ('22580', 'Woodford', 'VA')}
            {INSERT INTO hotel VALUES (8200, 'Tru', '22580', '6524 Dominion Raceway Ave', NULL)}
            {INSERT INTO room VALUES (8200, 'double', 90, 95)}
            {INSERT INTO reservation VALUES (9995, 4000, 8200, 'double', '2023-12-29', '2024-01-02')}
            {UPDATE room SET free = free - 1 WHERE hno = 8200 AND type = 'double'}
        }
        set res [db batch $stmt {*}$batch]
        expect "status for each executed SQL statement" {
            expr {
                [llength $res] == [llength $batch] &&
                [lindex $res 0] == 1 &&
                [lindex $res 1] == 1 &&
                [lindex $res 2] == 1 &&
                [lindex $res 3] == 1 &&
                [lindex $res 4] == 1
            }
        }
        db rollback
    }

    it "returns result set columns meta data" {
        set stmt [db newstatement]
        set numRows [db execute $stmt "SET CURRENT_SCHEMA=hotel"]
        assert "no rows were affected" $numRows == 0
        set numRows [db execute $stmt "
            SELECT h.name  AS hotel_name
                 , r.type  AS room_type
                 , r.free  AS num_available_rooms
                 , r.price AS room_price
             FROM room r
             JOIN hotel h
               ON h.hno = r.hno
            WHERE h.zip = '60601'
              AND r.price < 150
            ORDER BY price
        "]
        assert "3 rows in the result set" $numRows == 3
        assert "4 columns in the result set" [db columns $stmt -count] == 4
        set col1 [db column $stmt 1]
        set info [dict create {*}$col1]
        assert "col 1 schema"       [dict get $info schema]     eq "HOTEL"
        assert "col 1 table"        [dict get $info table]      eq "HOTEL"
        assert "col 1 column"       [dict get $info column]     eq "NAME"
        assert "col 1 label"        [dict get $info label]      eq "HOTEL_NAME"
        assert "col 1 type"         [dict get $info type]       eq "CHAR ASCII"
        assert "col 1 length"       [dict get $info length]     == 50
        assert "col 1 precision"    [dict get $info precision]  == 50
        assert "col 1 scale"        [dict get $info scale]      == 0
        assert "col 1 bytelength"   [dict get $info bytelength] == 50
        assert "col 1 is nullable"  [dict get $info nullable]   == 1
        assert "col 1 is writable"  [dict get $info writable]   == 0

        set col2 [db column $stmt 2]
        set info [dict create {*}$col2]
        assert "col 2 schema"       [dict get $info schema]     eq "HOTEL"
        assert "col 2 table"        [dict get $info table]      eq "ROOM"
        assert "col 2 column"       [dict get $info column]     eq "TYPE"
        assert "col 2 label"        [dict get $info label]      eq "ROOM_TYPE"
        assert "col 2 type"         [dict get $info type]       eq "CHAR ASCII"
        assert "col 2 length"       [dict get $info length]     == 6
        assert "col 2 precision"    [dict get $info precision]  == 6
        assert "col 2 scale"        [dict get $info scale]      == 0
        assert "col 2 bytelength"   [dict get $info bytelength] == 6
        assert "col 2 is nullable"  [dict get $info nullable]   == 1
        assert "col 2 is writable"  [dict get $info writable]   == 0

        set col3 [db column $stmt 3]
        set info [dict create {*}$col3]
        assert "col 3 schema"       [dict get $info schema]     eq "HOTEL"
        assert "col 3 table"        [dict get $info table]      eq "ROOM"
        assert "col 3 column"       [dict get $info column]     eq "FREE"
        assert "col 3 label"        [dict get $info label]      eq "NUM_AVAILABLE_ROOMS"
        assert "col 3 type"         [dict get $info type]       eq "FIXED"
        assert "col 3 length"       [dict get $info length]     == 3
        assert "col 3 precision"    [dict get $info precision]  == 3
        assert "col 3 scale"        [dict get $info scale]      == 0
        assert "col 3 bytelength"   [dict get $info bytelength] == 3
        assert "col 3 is nullable"  [dict get $info nullable]   == 1
        assert "col 3 is writable"  [dict get $info writable]   == 0

        set col4 [db column $stmt 4]
        set info [dict create {*}$col4]
        assert "col 4 schema"       [dict get $info schema]     eq "HOTEL"
        assert "col 4 table"        [dict get $info table]      eq "ROOM"
        assert "col 4 column"       [dict get $info column]     eq "PRICE"
        assert "col 4 label"        [dict get $info label]      eq "ROOM_PRICE"
        assert "col 4 type"         [dict get $info type]       eq "FIXED"
        assert "col 4 length"       [dict get $info length]     == 6
        assert "col 4 precision"    [dict get $info precision]  == 6
        assert "col 4 scale"        [dict get $info scale]      == 2
        assert "col 4 bytelength"   [dict get $info bytelength] == 4
        assert "col 4 is nullable"  [dict get $info nullable]   == 1
        assert "col 4 is writable"  [dict get $info writable]   == 0

        set cols [db columns $stmt]
        expect "all 4 columns are returned" {
            expr {
                [llength $cols] == 4 &&
                [lindex $cols 0] == $col1 &&
                [lindex $cols 1] == $col2 &&
                [lindex $cols 2] == $col3 &&
                [lindex $cols 3] == $col4
            }
        }
    }

    it "fetches rows of executed queries" {
        set stmt [db newstatement]
        set numRows [db execute $stmt "SET CURRENT_SCHEMA=hotel"]
        assert "no rows were affected" $numRows == 0
        set numRows [db execute $stmt "
            SELECT h.name  AS hotel_name
                 , r.type  AS room_type
                 , r.free  AS num_available_rooms
                 , r.price AS room_price
             FROM room r
             JOIN hotel h
               ON h.hno = r.hno
            WHERE h.zip = '60601'
              AND r.price < 150
            ORDER BY price
        "]
        assert "4 columns in the result set" [db columns $stmt -count] == 4
        assert "3 rows in the result set" $numRows == 3
        # row 1
        set fetched [db fetch $stmt row nulls]
        expect "row is fetched" { expr { !!$fetched } }
        assert "row has 4 data elements" [llength $row] == 4
        expect "all returned data are not null" {
            lassign $nulls hotel_name_is_null room_type_is_null num_available_rooms_is_null room_price_is_null
            expr {
                !$hotel_name_is_null && !$room_type_is_null && !$num_available_rooms_is_null && !$room_price_is_null
            }
        }
        lassign $row hotel_name room_type num_available_rooms room_price
        assert "hotel name is Best View Parkview Inn" $hotel_name eq "Best View Parkview Inn"
        assert "room type is single" $room_type eq "single"
        assert "number of free rooms is 87" $num_available_rooms == 87
        assert "room price is 63" $room_price == 63
        # row 2
        unset row
        unset nulls
        set fetched [db fetch -asarray $stmt row nulls]
        expect "row is fetched" { expr { !!$fetched } }
        assert "row has 4 data elements" [array size row] == 4
        expect "all returned data are not null" {
            expr { !$nulls(HOTEL_NAME) && !$nulls(ROOM_TYPE) && !$nulls(NUM_AVAILABLE_ROOMS) & !$nulls(ROOM_PRICE) }
        }
        assert "hotel name is Lake Michigan" $row(HOTEL_NAME) eq "Lake Michigan"
        assert "room type is single" $row(ROOM_TYPE) eq "single"
        assert "number of free rooms is 50" $row(NUM_AVAILABLE_ROOMS) == 50
        assert "room price is 105" $row(ROOM_PRICE) == 105
        # row 3
        set fetched [db fetch -asarray $stmt row nulls]
        expect "row is fetched" { expr { !!$fetched } }
        assert "row has 4 data elements" [array size row] == 4
        expect "all returned data are not null" {
            expr { !$nulls(HOTEL_NAME) && !$nulls(ROOM_TYPE) && !$nulls(NUM_AVAILABLE_ROOMS) & !$nulls(ROOM_PRICE) }
        }
        assert "hotel name is Best View Parkview Inn" $row(HOTEL_NAME) eq "Best View Parkview Inn"
        assert "room type is double" $row(ROOM_TYPE) eq "double"
        assert "number of free rooms is 43" $row(NUM_AVAILABLE_ROOMS) == 43
        assert "room price is 143" $row(ROOM_PRICE) == 143
        # row 4
        set fetched [db fetch -asarray $stmt row]
        expect "there is no row 4" { expr { !$fetched } }
    }

    epilogue {
        if {[llength [info commands db]] == 1} {
            db disconnect
        }
    }
}

describe "Prepared Statement" {
    prologue {
        if {[llength [info commands db]] == 0} {
            # The HOTEL tables use CHAR, hence the need for "-chopblanks"
            sdb connect db {*}[array get ::opts] -chopblanks 1
        }
        db execute "SET CURRENT_SCHEMA=hotel"
    }

    it "is created by the connection command" {
        set stmt [db prepare "
            SELECT h.name  AS hotel_name
                 , r.type  AS room_type
                 , r.free  AS num_available_rooms
                 , r.price AS room_price
             FROM hotel.room r
             JOIN hotel.hotel h
               ON h.hno = r.hno
            WHERE h.zip = :ZIP
              AND r.price <= :MAX_PRICE
              AND r.free >= :MIN_FREE
            ORDER BY price
        "]
        set numRows [db execute $stmt :ZIP "60601" :MAX_PRICE 150 :MIN_FREE 50]
        assert "2 rows returned" $numRows == 2
        # row 1
        assert "row 1 fetched" [db fetch -asarray $stmt row] == 1
        assert "hotel name is Best View Parkview Inn" $row(HOTEL_NAME) eq "Best View Parkview Inn"
        assert "room type is single" $row(ROOM_TYPE) eq "single"
        assert "number of free rooms is 87" $row(NUM_AVAILABLE_ROOMS) == 87
        assert "room price is 63" $row(ROOM_PRICE) == 63
        # row 2
        assert "row 2 fetched" [db fetch -asarray $stmt row] == 1
        assert "hotel name is Lake Michigan" $row(HOTEL_NAME) eq "Lake Michigan"
        assert "room type is single" $row(ROOM_TYPE) eq "single"
        assert "number of free rooms is 50" $row(NUM_AVAILABLE_ROOMS) == 50
        assert "room price is 105" $row(ROOM_PRICE) == 105

        assert "there is no row 3" [db fetch -asarray $stmt row] == 0
    }

    it "accepts positional parameters" {
        set queryRoom [db prepare "
            SELECT free
              FROM room
             WHERE hno = ?
               AND type = ?
        "]
        set numRows [db execute $queryRoom 20 "single"]
        assert "1 row returned" $numRows == 1
        assert "row fetched" [db fetch $queryRoom row] == 1
        lassign $row numFree

        set numReservations 2

        set updateRoom [db prepare "
            UPDATE room
               SET free = free - ?
             WHERE hno = ?
               AND type = ?
        "]
        set numRows [db execute $updateRoom $numReservations 20 "single"]
        assert "1 row updated" $numRows == 1

        set numRows [db execute $queryRoom 20 "single"]
        assert "1 row returned" $numRows == 1
        assert "row fetched" [db fetch $queryRoom row] == 1
        lassign $row numFreeNow

        expect "updated number of room matches update" {
            expr { $numFreeNow == $numFree - $numReservations }
        }
    }

    it "accepts OUT variables" {
        set numRows [db execute "
            SELECT Count(*)
              FROM dbprocedures
             WHERE schemaname = 'HOTEL'
               AND dbprocname = 'AVG_PRICE'
        "]
        if {$numRows == 0 || ![db fetch row] || [lindex $row 0] == 0} {
            set numRows [db execute {
                CREATE DBPROCEDURE hotel.avg_price (IN zip CHAR(5), OUT avg_price FIXED(6,2)) AS
                VAR sum FIXED(10,2); price FIXED(6,2); hotels INTEGER;
                TRY
                    SET sum = 0;
                    SET hotels = 0;
                    DECLARE dbproccursor CURSOR FOR
                        SELECT price
                          FROM hotel.room, hotel.hotel
                         WHERE zip = :zip
                           AND room.hno = hotel.hno
                           AND type = 'single';
                    WHILE $rc = 0 DO BEGIN
                        FETCH dbproccursor INTO :price;
                        SET sum = sum + price;
                        SET hotels = hotels + 1;
                    END;
                CATCH
                    IF $rc <> 100 THEN STOP ($rc, 'unexpected error');
                CLOSE dbproccursor;
                IF hotels > 0 THEN
                    SET avg_price = sum / hotels
                ELSE
                    STOP (100, 'no hotel found');
            }]
            db commit
        }

        set stmt [db prepare "CALL avg_price(:ZIP,:RES)"]
        db execute $stmt :ZIP "95971" :RES avgPrice
        assert "average price is 127.33" $avgPrice == 127.33
    }

    epilogue {
        if {[llength [info commands db]] == 1} {
            db disconnect
        }
    }
}

describe "LOB" {
    prologue {
        if {[llength [info commands db]] == 0} {
            # The HOTEL tables use CHAR, hence the need for "-chopblanks"
            sdb connect db {*}[array get ::opts] -chopblanks 1
        }
        db execute "SET CURRENT_SCHEMA=hotel"
    }

    it "writes data into a LOB using LOB API" {
        set stmt [db prepare "SELECT name, info FROM hotel WHERE hno = ? FOR UPDATE OF info"]
        set hno 20
        db execute -fetchsize 1 $stmt $hno
        assert "row is fetched" [db fetch $stmt row] == 1
        lassign $row name lob
        # puts "optimal size=[db optimalsize $lob]"
        if {[catch {db write $lob "Hello, $name Hotel!"} msg]} {
            #
            # write $lob via SQLDBC_LOB api fails. If there are steps to take to make it work,
            # they are not obvious and searching the Net thus far did not reveal any examples.
            # Also, SQLDBC_LOB does not expose the connection item functionality, thus making
            # error detail inaccessible to the API users.
            #
            return -code break $msg
        }
        db close $lob
    }

    it "can be created and updated as data" {
        set numRows [db exec "SELECT Count(*) FROM tables WHERE schemaname = 'HOTEL' and tablename = 'MEDIA'"]
        if { $numRows == 0 || ![db fetch row] || [lindex $row 0] == 0} {
            set numRows [db exec "
                CREATE TABLE media (
                    id SERIAL PRIMARY KEY,
                    txt CLOB UNICODE,
                    bin BLOB
                )
            "]
            db commit
        }
        set clobText "*** short clob ***"
        set blobData "<<< small blob >>>"
        set stmt [db prepare "INSERT INTO media (txt, bin) VALUES (?,?)"]
        set numRows [db exec $stmt $clobText $blobData]
        assert "row inserted" $numRows == 1
        set id [db serial $stmt]

        set query [db prepare "SELECT txt, bin FROM media WHERE id = ?"]
        assert "one row in the result" [db exec $query $id] == 1
        assert "row fetched" [db fetch $query row] == 1
        lassign $row clob blob
        set text [db read $clob 100]
        set data [db read $blob 100]
        assert "returned text matches original" $text eq $clobText
        assert "returned data match original" $data == $blobData

        set newClobText "***!*** short clob ***!***"
        set newBlobData "<<<!>>> small blob <<<!>>>"
        set stmt [db prepare "UPDATE media SET txt = ? WHERE id = ?"]
        set numRows [db exec $stmt $newClobText $id]
        assert "row updated" $numRows == 1
        set stmt [db prepare "UPDATE media SET bin = ? WHERE id = ?"]
        set numRows [db exec $stmt $newBlobData $id]
        assert "row updated" $numRows == 1

        assert "one row in the result" [db exec $query $id] == 1
        assert "row fetched" [db fetch $query row] == 1
        lassign $row clob blob
        set text [db read $clob 100]
        set data [db read $blob 100]
        assert "returned text matches updated text" $text eq $newClobText
        assert "returned data match updated data" $data == $newBlobData

        db rollback
    }

    it "reads LOB column content" {
        set stmt [db prepare "SELECT info FROM hotel WHERE hno = :HNO"]
        set hno 10
        assert "one row in the result" [db execute $stmt :HNO $hno] == 1
        assert "row is fetched" [db fetch $stmt row nulls] == 1
        assert "lob is not null" [lindex $nulls 0] == 0
        lassign $row lob
        set lobLen [db length $lob]
        set text ""
        for {set pos 1} {$pos < $lobLen} {incr pos $numRead} {
            set chunk [db read -from $pos $lob 128]
            set numRead [string length $chunk]
            append text $chunk
        }
        assert "CLOB is fully read" [string length $text] == $lobLen
        # or read the entire lob (if it is small enough)
        set fullText [db read -from 1 $lob $lobLen]
        expect "CLOB is fully read again" { expr {[string length $fullText] == $lobLen && $fullText eq $text} }
        db close $lob
    }

    epilogue {
        if {[llength [info commands db]] == 1} {
            db disconnect
        }
    }
}

