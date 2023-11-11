# This is a modified (a bit more elaborate here) example from the README file.
# It, as all other examples, connects to a database using XUSER key.

package require sdbtcl

source [file join [file dir [info script]] formatQueryOutput.tcl]

array set auth {-key MONA}
array set auth $argv

sdb connect db {*}[array get auth] -chopblanks 1
db execute "SET CURRENT_SCHEMA=hotel"
set stmt [db prepare "
    SELECT h.name  AS hotel_name
         , r.type  AS room_type
         , r.free  AS free_rooms
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
# $numRows == 2
set rowFormat [formatQueryOutput [db columns $stmt]]
while {[db fetch $stmt row]} {
    puts [format $rowFormat {*}$row]
}
db disconnect
