#pragma once

#include "sdbtcl.h"
#include <memory>
#include <unordered_set>

class SdbStmt;

class SdbConn {
    SQLDBC_Connection*           conn;    
    SdbEnv&                      env;
    Tcl_Command                  cmd;
    SdbStmt*                     stmt;
    std::unordered_set<SdbStmt*> statements;

    SdbStmt* myStmt();

public:
    SdbConn(SdbEnv& env);
    ~SdbConn();

    /**
     * Creates an SQLDBC statement object for sending SQL statements to the database.
     */
    SQLDBC_Statement* createStatement();

    /**
     * Creates an SQLDBC prepared statement object for sending SQL statements to the database.
     */
    SQLDBC_PreparedStatement* createPreparedStatement();

    /**
     * Removes the statement from the tracking set.
     */
    void eraseStatement (SdbStmt* stmt) { statements.erase(stmt); }

    /**
     * Creates TCL command to control database connection
     */
    int createCommand (Tcl_Interp* interp, const char* name);

    /**
     * Returns TCL string with the current connection property value.
     */
    Tcl_Obj* getConnProp (Tcl_Interp* interp, const char* key, bool uppercase = false);

    /**
     * Returns TCL string with the current isolation level.
     */
    Tcl_Obj* getIsolationLevel ();

    /**
     * Establlishes a new database connection.
     *
     * Example:
     *
     * ```tcl
     * sdb connect db -host localhost -database maxdb -user mona -password red
     * ```
     *
     * or
     *
     * ```tcl
     * sdb connect db -key xuserkey
     * ```
     *
     * See https://maxdb.sap.com/documentation/sqldbc/SQLDBC_API/classSQLDBC_1_1SQLDBC__ConnectProperties.html
     * for a list of other acceptable connection options.
     */
    int connect (Tcl_Interp* interp, int argc, Tcl_Obj* const argv[]);

    /**
     * Closes database session and deletes TCL database control command.
     *
     * Example:
     *
     * ```tcl
     * db disconnect
     * ```
     */
    int disconnect (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Configures and queries connection properties:
     * - autocommit
     * - isolationlevel
     * - sqlmode
     *
     * Example:
     *
     * ```tcl
     * db configure -autocommit on -isolationlevel "READ UNCOMMITTED"
     * ```
     */
    int configure (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Checks the connection and database state:
     *  - connected
     *      If the connection to the database was established. It does not check whether
     *      the connection timed out or the database server is still running.
     *  - usable
     *      Execute a special RTE-call to ensure that the connection is usable.
     *  - unicode
     *      Whether the database is a unicode database or not
     *
     * # Example
     *
     * ```tcl
     * set is_usable [db is usable]
     * ```
     */
    int is (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Retrieves database properties:
     *   - kernelversion
     *   - datetimeformat
     *
     * Example:
     *
     * ```tcl
     * set version [db get kernelversion]
     * ```
     *
     * For example, for version 7.9.10 version number 70910 is returned
     */
    int get (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * All changes made since the previous COMMIT/ROLLBACK statement are stored, any database locks
     * held by this connection are released.
     *
     * Example:
     *
     * ```tcl
     * db commit
     * ```
     */
    int commit (Tcl_Interp* interp);

    /**
     * Undoes all changes made in the current transaction and releases any database locks held by
     * this connection object.
     *
     * Example:
     *
     * ```tcl
     * db rollback
     * ```
     */
    int rollback (Tcl_Interp* interp);

    /**
     * Creates a statement handle for execution of unprepared SQL.
     */
    int newStatement (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Create a statement handle and 'prepares' provided SQL on the database server.
     * The prepared statement handle can use binding variables for input/output parameters.
     * 
     * ```tcl
     * set stmt [db prepare -cursor rooms -maxrows 100 {
     *   SELECT h.name, r.type, r.free, r.price
     *     FROM room r
     *     JOIN hotel h
     *       ON h.hno = r.hno
     *    WHERE h.zip = :ZIP
     *      AND r.price <= :MAX_PRICE
     *    ORDER BY r.price
     * }]
     * ```
     */
    int prepare (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Executes a batch of SQL statements.
     *
     * Statements for batched execution must not return result sets.
     * 
     * ```tcl
     * set results [db batch $stmt "CREATE TABLE ..." "CREATE INDEX ..."]
     * ```
     */
    int batch (Tcl_Interp* interp, int argc, Tcl_Obj* const argv[]);

    /**
     * Returns information about the result set columns.
     * 
     * ```tcl
     * set columns [db columns $stmt]
     * set numCols [db columns $stmt -count]
     * set colInfo [db columns $stmt 1]
     * ```
     */
    int columns (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Executes a single SQL statement.
     *
     * Example:
     *
     * ```tcl
     * set numRows [db execute $stmt "UPDATE room SET price = price * 0.95 WHERE hno IN (SELECT hno FROM hotel WHERE zip = '60601')"]
     * ```
     *
     * Query Example:
     *
     * ```tcl
     * set numRows [db execute -cursor rooms -maxrows 100 $stmt "
     *   SELECT h.name, r.type, r.free, r.price
     *     FROM room r
     *     JOIN hotel h
     *       ON h.hno = r.hno
     *    WHERE h.zip = '60601'
     *      AND r.price < 150
     *    ORDER BY r.price
     * "]
     * ```
     */
    int execute (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Retrieves the data from the result set at the specified cursor position.
     *
     * Initially the cursor is positioned before the first row. The following options
     * change it before fetching the data from the updated position:
     * - first     : moves the cursor to the first row in the result set.
     * - next      : moves the cursor down one row from its current position (default)
     * - previous  : moves the cursor to the previous row from its current position.
     * - last      : moves the cursor to the last row in the result set.
     * - seek #row : moves the cursor to the specified row number in the result set.
     * - seet dist : moves the cursor by a relative number of rows, either positive or negative.
     *
     * Data options:
     * - asarray   : the row will be stored as an array indexed by the labels of returned columns
     * 
     * ```tcl
     * while {[db fetch -asarray $stmt row]} {
     *   # ...
     * }
     * ```
     */
    int fetch (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Returns the current row number.
     *
     * The first row is row number 1, the second row number 2, and so on.
     *
     * The returned row number is 0 if the cursor is positioned outside the result set.
     */
    int rowNumber (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Retrieves the key that was inserted by the last insert operation.
     *
     * Options:
     * - first : return the first serial key
     * - last  : return the last serial key (default)
     * 
     * ```tcl
     * set id [db serial -last $stmt]
     * ```
     */
    int serial (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Closes the given LOB handle
     * 
     * ```tcl
     * db close $lob
     * ```
     */
    int close (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Retrieves the length of the given LOB in the database.
     * The length is returned in chars.
     * 
     * ```tcl
     * set len [db length $lob]
     * ```
     */
    int length (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Retrieves the optimal size of data for reading or writing (the maximum size
     * that can be transferred with one call to the database server).
     * 
     * ```tcl
     * set optSize [db optimalsize $lob]
     * ```
     */
    int optimalSize (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Get the current read/write position (in characters).
     *
     * The read/write position starts with 1.
     * If there is no position available, 0 is returned.
     * 
     * ```tcl
     * set pos [db position $lob]
     * ```
     */
    int position (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Retrieves the (poosibly partial) content of the LOB.
     *
     * After the operation, the internal position is the start position
     * plus the number of bytes/characters that have been read.
     * 
     * ```tcl
     * set data [db read -from $pos $lob 10000]
     * ```
     */
    int read (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

    /**
     * Puts data into the given LOB starting at the current position.
     *
     * ```tcl
     * db write $lob $data
     * ```
     */
    int write (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
};
