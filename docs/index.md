# Sdbtcl

Sdbtcl is an extension to the Tcl language that provides access to an SAP MaxDB database.

## Example

```tcl
package require sdbtcl

sdb connect db -host localhost -database MAXDB -user MONA -password RED -chopblanks 1
db execute "SET CURRENT_SCHEMA=hotel"
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
# $numRows == 2
while {[db fetch -asarray $stmt row]} {
    puts "hotel name: $row(HOTEL_NAME)"
    puts " room type: $row(ROOM_TYPE)"
    puts "rooms free: $row(NUM_AVAILABLE_ROOMS)"
    puts "room price: $row(ROOM_PRICE)"
}
db disconnect
```

## Sdbtcl Commands

Sdbtcl provides a single command - `sdb`. That command supports the following operations:

**`sdb version`**

Returns the version of used SQLDBC runtime:

```tcl
set version [sdb version]
# $version eq "libSQLDBC 7.9.10    BUILD 011-123-272-517"
```

**`sdb connect`** *`cmdName ?-host nodename? -database dbname -user username -password password ?-option value ... ?`*
**`sdb connect`** *`cmdName -key XUSERKEY ?-option value ... ?`*

Opens a new database session and creates a new Tcl command *`cmdName`* that will be used to access that session.

> ⚠️ See the SQLDBC manual for list of other [connection options][1]. **`sdb connect`** accepts a lower case version of the listed options prefixed with a dash. For example, SQLDBC's `CHOPBLANKS` boolean option it should be passed as `-chopblanks 1`.

**`sdb connect`** handles `ISOLATIONLEVEL` option internally. Instead of the numeric values specified by the SQLDBC it accepts the following values:

  - *`READ UNCOMMITTED`*
  - *`READ COMMITTED`*
  - *`READ COMMITTED WITH TABLE LOCKS`*
  - *`REPEATABLE READ`*
  - *`SERIALIZABLE`*

**`sdb connect`** also accepts *`-autocommit`* option to allow changing the default session's autocommit mode upon connect. *`-autocommit`* accepts any Tcl boolean value.

```tcl
sdb connect db -key mona -chopblanks 1
```

## Database Session Operations

Command created by **`sdb connect`** to access a connected database session provides the follwing operations:

*`dbCmd`* **`configure`** *`?option ?value ?option value ... ???`*

Query or modify current session options. As most SQLDBC connection options can only be used upon connect and are not accessible from a session, only the following options can be queried - **`autocommit`**, **`isolationlevel`**, and **`sqlmode`**:

```tcl
set config [db configure]
# $config == {autocommit 0 isolationlevel {READ COMMITTED} sqlmode INTERNAL}
set isAutoCommit [db config -autocommit]
# $isAutoCommit == 0
db configure -autocommit on
```

*`dbCmd`* **`get`** *`propName`*

Queries database properties. *`propName`* can be one of these:

- **`kernelversion`** - Returns the kernel version as number computed as `major_release * 10000 + minor_release * 100 + correction_level`. For example, for version 7.9.10 version number `70910` is returned.
- **`datetimeformat`** - Returns the currently active date/time format, which can be one of these values - **`INTERNAL`**, **`ISO`**, **`USA`**, **`Europe`**, **`Japan`**.

```tcl
set ver [db kernelversion]
# $ver = 70910
```

*`dbCmd`* **`is`** *`state`*

Queries the connection and the database state. *`state`* can be one of these:

- **`connected`** - If the connection to the database was established. It does not check whether the connection timed out or the database server is still running.
- **`usable`** - Execute a special RTE-call to ensure that the connection is usable.
- **`unicode`** - Whether the database catalog of the database instance is able to store the names of database objects such as table and column names in Unicode.

```tcl
set isConnected [db is connected]
# $isConnected == 1
set isUsable [db is usable]
# $isUsable == 1
```

*`dbCmd`* **`commit`**

Commits all changes made in the current transaction (since the previous **`commit`** or **`rollback`**) and releases any database locks created by this database session.

```tcl
db commit 
```

*`dbCmd`* **`rollback`**

Undoes all changes made in the current transaction (since the previous **`commit`** or **`rollback`**) and releases any database locks created by this database session.

```tcl
db rollback 
```

*`dbCmd`* **`disconnect`**

Closes the current database session and deletes the database session access command.

> ⚠️ No expicit transaction control statement is sent to the database, so the uncommitted transaction
> may be implicitly rolled back by the database.

```tcl
db disconnect
# db command is no longer available
# [info commands db] == {}
```

## SQL Statements Execution

*`dbCmd`* **`newstatement`** *`?option value ... ?`*

Creates a new handle for execution of DDL and DML statements that do not have parameter markers in them.

> ⚠️ `dbCmd` has an internal handle that can be used (see **`execute`** subcommand) to execute these SQL statements. Thus, most of the time an explicit handle to execute SQL statements without parameters is not needed. The notable exception is when a statement returns a named cursor, so it can be further used with `UPDATE ... CURRENT OF cursor_name`. You would want to execute those using a dedicated statement handle.

*`option`* can be one of these:

- **`-cursor`** - Sets the cursor (result set) name. Setting the cursor name affects only execution of queries and database procedures that return cursors.
- **`-resultsettype`** - Sets the type of a result set. It can be one of these - **`FORWARD ONLY`**, **`SCROLL SENSITIVE`** (scrollable and updatable), **`SCROLL INSENSITIVE`** (scrollable, but not updatable).
- **`-concurrencytype`** - Sets the type of the result set concurrency. It can be one of these - **`READ ONLY`**, **`UPDATABLE`**, or **`UPDATABLE LOCK OPTIMISTIC`**.
- **`-maxrows`** - Limits the number of rows in the returned result set.
- **`-fetchsize`** - Sets a hint to the runtime about the desired fetch size. If it is 1, updates using the `CURRENT OF` predicate become possible.

```tcl
set stmt [db newstatement]
```

*`dbCmd`* **`prepare`** *`?option value ... ? sql`*

Creates a new handle for execution of the SQL statement that has parameter markers in them, and sends the given SQL statement to the database for parsing.

Parameters in the prepared SQL statement can be marked either as `?` or `:name`. Sdbtcl does not allow mixing them in the same SQL statement. Argument values for `?`, or positional, parameters are provided as ordered values, such that argument in the first position would be bould to the first `?`, and so on. Argument values for `:name` style parameters are provided as pair of elements - the parameter name and parameter value. Named arguments can be provided in any order.

> ⚠️ *`option`* are the same as those that are used in the **`newstatement`** subcommand.

```tcl
set stmt [db prepare "
  SELECT * 
    FROM room r 
    JOIN hotel h 
      ON h.hno = r.hno
   WHERE zip = :ZIP 
     AND price < :MIN_PRICE
"]
```

*`dbCmd`* **`batch`** *`sql ?sql ... ?`*

Executes a batch of SQL statements that (a) do not return result sets and (b) do not have parameter markers in them.

The result of the execution is a list of results returned by the execution of the individual statements in the batch.

```tcl
set results [db batch "CREATE TABLE ..." "CREATE INDEX ..." "INSERT ..." "INSERT ..."]
```

> ⚠️ Most of the commands listed below can be executed with either the internal implicit statement handle or with an explicit statement handle created either by the **`newstatement`** or by **`prepare`**.

*`dbCmd`* **`execute`** *`?option value ... ? ?stmtHandle? sql`*

Executes a single SQL statement that does not have parameter markers in them. Returns the number of rows in the result set for queries or procedures that return cursors, the number of affected rows for DML like `DELETE`, `UPDATE`, `INSERT`.

> ⚠️ *`option`* are the same as those that are used in the **`newstatement`** subcommand.

```tcl
set numRows [db execute "SET CURRENT_SCHEMA=hotel"]
# $numRows == 0
set numRows [db execute "SELECT * FROM dual]
# $numRows == 1
```

*`dbCmd`* **`execute`** *`?option value ... ? stmtHandle ?argVal ... ?`*

*`dbCmd`* **`execute`** *`?option value ... ? stmtHandle ?:argName argVal ... ?`*

Executes a prepared SQL statement. Returns the number of rows in the result set for queries or procedures that return cursors, the number of affected rows for DML like `DELETE`, `UPDATE`, `INSERT`.

THe first form is used for SQL statement with positional - `?` - parameter markers. The second - for statements with named - `:name` - parameter markers.

> ⚠️ *`option`* are the same as those that are used in the **`newstatement`** subcommand.

```tcl
set stmt [db prepare "
  SELECT * 
    FROM room r 
    JOIN hotel h 
      ON h.hno = r.hno
    JOIN city c
      ON c.zip = h.zip  
   WHERE state = :STATE 
     AND price < :MIN_PRICE
   ORDER BY price  
"]
set numRows [db execute $stmt :STATE TX :MIN_PRICE 75]
# $numRows == 90
set numRows [db execute -maxrows 10 $stmt :STATE TX :MIN_PRICE 75]
# $numRows == 10
```

*`dbCmd`* **`columns`** *`?stmtHandle? ?columnNumber|-count|-labels?`*

Returns information about the result set columns:

- **`-count`** returns number of columns in the result set
- **`columnNumber`** - where *columnNumber* is an integer between 1 and the number of columns, returns a list containing pairs of elements. The first element in each pair is the name of the following data element:

  - **`schema`** - Schema name of the column.
  - **`table`** - Table name of the column.
  - **`column`** - Name of the column.
  - **`label`** - Label of the column. Label is either tha column name or the column alias specified using `AS`.
  - **`type`** - Data type of the specified column
  - **`length`** - Column's maximum width in characters.
  - **`precision`** - Column's maximum number of decimal digits.
  - **`scale`** - The number of decimal places of the data type of the column.
  - **`bytelength`** - Column's maximum width in bytes.
  - **`nullable`** - Whether NULL values are allowed for the column values.
  - **`writable`** - Whether a write operation is possible on this column.

- **`-labels`** - returns a list of just the column labels

If **`columns`** is called without any arguments it would return a list of lists of individual column data.

> ⚠️ **`columns`** returns column information only after result set is obtained, i.e. after the statement has been executed. Querying column information before **`execute`** will return zero columns.

```tcl
set stmt [db prepare "SELECT * FROM city WHERE state = ?"]
set numRows [db execute $stmt "RI"]
# $numRows == 1
set nulCols [db columns $stmt -count]
# $numCols == 3
set labels [db columns $stmt -labels]
# $labels == {ZIP NAME STATE}
set col1 [db column $stmt 1]
# $col1 == {schema HOTEL table CITY column ZIP label ZIP type {CHAR ASCII} length 5 precision 5 scale 0 bytelength 5 nullable 0 writable 0}
```

*`dbCmd`* **`fetch`** *`?options? ?stmtHandle? rowVar ?nullIndVar?`*

Retrieves the data from the result set at the specified cursor position. Saves the data into *`rowVar`* either as a list or as an array if the **`-asarray`** option was specified. If the optional *`nullIndVar`* is specified, then null indicators for each columns will be saved into that variable either as a list or as an array if the **`-asarray`** option was specified.

The accepted options are:

- **`-first`** - moves the cursor to the first row in the result set.
- **`-last`** - moves the cursor to the last row in the result set.
- **`-next`** - moves the cursor down one row from its current position (this is the default)
- **`-previous`** - moves the cursor to the previous row from its current position.
- **`-seek`** *`#rowNum`* - moves the cursor to the specified row number in the result set.
- **`-seek`** *`offset`* - moves the cursor by a relative number of rows, either positive or negative.
- **`-asarray`** - the row will be stored as an array indexed by the labels of returned columns.

```tcl
set stmt [db prepare "SELECT * FROM city WHERE state = ?"]
db execute $stmt "RI"
while {[db fetch -asarray $stmt row]} {
  puts "$row(NAME), $row(STATE) $row(ZIP)"
}
# output: Exeter, RI 02822
```

*`dbCmd`* **`rownumber`** *`?stmtHandle?`*

Returns the current row number. The first row is row number 1, the second row number 2, and so on. The returned row number is 0 if the cursor is positioned outside the result set.

*`dbCmd`* **`serial`** *`?option? ?stmtHandle?`*

Retrieves the key that was inserted by the last insert operation.

The accepted options are:

- **`-first`** - return the first serial key
- **`-last`** - return the last serial key (this is the default)

```tcl
# Assuming that the current schema has followng table:
# CREATE TABLE media (id SERIAL PRIMARY KEY, path VARCHAR(500))
set stmt [db prepare "INSERT INTO media (path) VALUES (?)"]
db exec $stmt "/home/media/logo.png"
set id [db serial $stmt]
# $id >= 1 
```

## LOB Operations

*`dbCmd`* **`length`** *`lobHandle`*

Retrieves the length of the given LOB in the database. For CLOBs the length is returned in characters.

```tcl
set stmt [db prepare "SELECT name, info FROM hoter WHERE hno = ?"]
db exec $stmt 10
if {[db fetch $stmt row]} {
  lassign $row hotelName hotelInfo
  set infoLen [db length $hotelInfo]
  # $infoLen == 994
}
```

*`dbCmd`* **`read`** *`?-from position? lobHandle numChars`*

Retrieves the partial content of the LOB. After the operation, the internal position is the start position plus the number of bytes/characters that have been read.

```tcl
set stmt [db prepare "SELECT info FROM hotel WHERE hno = ?"]
db exec $stmt 10
if {[db fetch $stmt row]} {
  lassign $row lob
  set info ""
  set part [db read $lob 100]
  while {[string length $part] > 0} {
    append info $part
    set part [db read $lob 100]
  }
  # [string length $info] == 994
}
```

*`dbCmd`* **`position`** *`lobHandle`*

Retrieves the current read/write position. For CLOBs the length is returned in characters.

```tcl
set stmt [db prepare "SELECT name, info FROM hoter WHERE hno = ?"]
db exec $stmt 10
if {[db fetch $stmt row]} {
  lassign $row hotelName lob
  set position [db position $lob]
  # $position == 1
  set infoText [db read -from 61 $lob 140]
  set position [db position $lob]
  # $position == 201
}
```

*`dbCmd`* **`optimalsize`** *`lobHandle`*

Retrieves the optimal size of data for reading or writing (the maximum size that can be transferred with one call to the database server). An application may use this to optimize the communication, by using buffers that are multiples of the preferred size.

```tcl
set stmt [db prepare "SELECT name, info FROM hoter WHERE hno = ?"]
db exec $stmt 10
if {[db fetch $stmt row]} {
  lassign $row hotelName lob
  set optSize [db optimalsize $lob]
  # $optSize == 523512
}
```

*`dbCmd`* **`close`** *`lobHandle`*

Closes the given LOB handle.

```tcl
db close $lob
```

## Writing Into LOBs

⚠️ In the current version *`dbCmd`* **`write`** *`lobHandle data`* exists but always fails. At the moment there is no information about why that is happening. One of the obstacles is the SQLDBC LOB object itself as it does not expose its error handle, so there is no way to get the error message after the failure. Another obstacle is the lack of examples - MaxDB might require a certain steps to be taken before LOB can be written into. Alas, those are not discovered yet.

Present workaround is to treat LOB data as, well, one huge string and use it in INSERTs or UPDATEs as a string. While this is not ideal, it still allows inserting/updating fairly large LOBs.

```tcl
package require sdbtcl
package require http
package require tls

::http::register https 443 ::tls::socket

set resp [http::geturl https://www.gutenberg.org/cache/epub/1342/pg1342.txt]
set text [http::data $resp]
# Gutenberg texts have BOM
set text [string range $text 1 end]
# HOTEL.INFO is CLOB ASCII
set text [encoding convertto cp1252 $text]

sdb connect db -key mona -chopblanks 1
db exec "set current_schema=hotel"

set stmt [db prepare "UPDATE hotel SET info = ? WHERE hno = ?"]
db exec $stmt $text 20
db commit
```

[1]: https://maxdb.sap.com/documentation/sqldbc/SQLDBC_API/index.html
