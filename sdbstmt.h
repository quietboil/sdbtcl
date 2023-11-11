#pragma once

#include "sdbtcl.h"
#include <vector>

class SdbConn;
extern Tcl_ObjType sdbStmtType;
extern Tcl_ObjType sdbPrepStmtType;

struct Column {
    Tcl_Obj*        label;
    SQLDBC_Int2     length;
    SQLDBC_Int2     precision;
    SQLDBC_Int2     scale;
    SQLDBC_Int2     byteLength;
    SQLDBC_SQLType  sqlType;
    SQLDBC_HostType hostType;

    Column(SQLDBC_ResultSetMetaData* info, int columnNo);
    ~Column()
    {
        if (label) Tcl_DecrRefCount(label);
    }
};

struct ResultSetConfig {
    Tcl_Obj* type;
    Tcl_Obj* concurrency;
    Tcl_Obj* name;
    Tcl_Obj* maxRows;
    Tcl_Obj* fetchSize;

    ResultSetConfig () : type(nullptr), concurrency(nullptr), name(nullptr), maxRows(nullptr), fetchSize(nullptr) {}

    /**
     * Collects statement result set options.
     *
     * The following options are available:
     *  - cursor          : Sets the cursor name
     *  - maxrows         : Limits the number of rows in the returned result set
     *  - resultsettype   : Sets the type of a result set: "FORWARD ONLY", "SCROLL SENSITIVE", or "SCROLL INSENSITIVE"
     *  - concurrencytype : Sets the type of the result set concurrency: "READ ONLY", "UPDATABLE", or "UPDATABLE LOCK OPTIMISTIC"
     *  - fetchsize       : Sets the desired fetch size. If it is 1, updates using CURRENT OF become possible
     */
    int init (Tcl_Interp* interp, int* idxPtr, int objc, Tcl_Obj* const objv[]);
};

class SdbStmt {
protected:
    SQLDBC_Statement*         stmt;
    SQLDBC_ResultSet*         rset;
    SQLDBC_ResultSetMetaData* rsetInfo;
    std::vector<Column>       cols;
    SdbConn*                  conn;
    int                       refCount;
    SQLDBC_Int2               fetchSize;

    SdbStmt(SdbConn* conn, int refCount) : conn(conn), rset(nullptr), rsetInfo(nullptr), refCount(refCount), fetchSize(-1) {}

public:
    SdbStmt(SdbConn* conn);
    ~SdbStmt();

    void preserve () { ++refCount; }
    void release ()
    {
        if (--refCount <= 0) {
            delete this;
        }
    }

    /**
     * Releases all database handles without destroying the object.
     *
     * This call is initiated by sdb connection when it is being destroyed/closed.
     * As the instance of an sdb statement might still be referenced by TCL variables,
     * it will not be destroyed (just made inoperable). Only the database resourses
     * will be released.
     */
    virtual void releaseDatabaseHandles ();

    /**
     * Limits the number of rows of in a returned result set.
     */
    int setMaxRows (Tcl_Interp* interp, Tcl_Obj* num);

    /**
     * Sets the cursor name.
     */
    int setCursorName (Tcl_Interp* interp, Tcl_Obj* name);

    /**
     * Sets the type of a result set.
     *
     * A result set is only created by a query command.
     *
     * There are three kind of result sets:
     * - The result set can only be scrolled forward (default): "FORWARD ONLY"
     * - The result set is scrollable and may change: "SCROLL SENSITIVE"
     * - The result set is scrollable but does not change: "SCROLL INSENSITIVE"
     */
    int setResultSetType (Tcl_Interp* interp, Tcl_Obj* type);

    /**
     * Sets the type of the result set concurrency.
     *
     * There are two kinds of concurrency:
     * - The result set is read-only (default): "READ ONLY"
     * - The result set can be updated: "UPDATABLE" or "UPDATABLE LOCK OPTIMISTIC"
     */
    int setResultSetConcurrencyType (Tcl_Interp* interp, Tcl_Obj* type);

    /**
     * Sets the desired fetch size.
     */
    int setFetchSize (Tcl_Interp* interp, Tcl_Obj* size);

    /**
     * Closes results of previous executions.
     */
    void clearResults ();

    /**
     * Sets result set options from the provided config object.
     */
    int configure(Tcl_Interp* interp, ResultSetConfig& config);

    /**
     * Executes a single SQL statement.
     *
     * Example:
     *
     * ```tcl
     * set numRows [db $stmt execute "UPDATE room SET price = price * 0.95 WHERE hno IN (SELECT hno FROM hotel WHERE zip = '60601')"]
     * ```
     *
     * Query Example:
     *
     * ```tcl
     * set numRows [db execute -cursor rooms -maxrows 100 $stmt {
     *   SELECT h.name, r.type, r.free, r.price
     *     FROM room r
     *     JOIN hotel h
     *       ON h.hno = r.hno
     *    WHERE h.zip = '60601'
     *      AND r.price < 150
     *    ORDER BY r.price
     * }]
     * ```
     */
    virtual int execute (Tcl_Interp* interp, int idx, int argc, Tcl_Obj* const argv[], ResultSetConfig& config);

    enum SeekType { Next, Previous, First, Last, Relative, Absolute };

    /**
     * Fetches the specified row.
     */
    int fetch (Tcl_Interp* interp, SeekType seek, int row = 0);

    /**
     * Changes internal state and sets TCL result after `execute`.
     */
    int setExecuteResult (Tcl_Interp* interp);

    /**
     * Checks if the SQL statement is a query.
     */
    bool isQuery () { return stmt->isQuery(); }

    /**
     * Returns the number of columns in the result set.
     */
    int getColumnCount () { return cols.size(); }

    /**
     * Returns the current row number.
     */
    int getRowNumber ();

    /**
     * Retrieves the key that was inserted by the last insert operation.
     */
    int serial (Tcl_Interp* interp, bool last);

    /**
     * Returns a list of column names in the result set.
     */
    Tcl_Obj* getColumnLabels ();

    /**
     * Retrieves information about the specified column - types and properties of the column in a result set.
     *
     * Returns a key-value list of properties.
     */
    Tcl_Obj* getColumnInfo (Tcl_Interp* interp, int colNo);

    /**
     * Retrieves information about all result set columns.
     *
     * Returns a list of key-value lists.
     */
    Tcl_Obj* getAllColumnsInfo (Tcl_Interp* interp);

    /**
     * Reads column data from the current row.
     */
    int getRowData (Tcl_Interp* interp, Tcl_Obj* rowVar, Tcl_Obj* nullVar, bool returnAsArray);

    //-------

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
};

struct Param {
    union {
        char*       charValue;
        double      doubleValue;
        Tcl_WideInt wideIntValue;
        int         intValue;
    } outData;
    SQLDBC_Length   dataLength;
    Tcl_Obj*        outVarName;
    Tcl_Obj*        name;  /// :NAME of the parameter or NULL if the ? (positional parameter) was used
    SQLDBC_Int2     length;
    SQLDBC_Int2     precision;
    SQLDBC_Int2     scale;
    SQLDBC_Int2     byteLength;
    SQLDBC_SQLType  sqlType;
    SQLDBC_HostType hostType;

    SQLDBC_ParameterMetaData::ParameterMode inOutMode;

    Param(SQLDBC_ParameterMetaData* info, int paramNo);
    ~Param();

    Param& operator= (const Param&) = delete;

    bool isIn () { return inOutMode != SQLDBC_ParameterMetaData::ParameterMode::parameterModeOut; }
    bool isOut () { return inOutMode == SQLDBC_ParameterMetaData::ParameterMode::parameterModeOut || inOutMode == SQLDBC_ParameterMetaData::ParameterMode::parameterModeInOut; }
    bool isVarChar () { return hostType == SQLDBC_HOSTTYPE_BINARY || hostType == SQLDBC_HOSTTYPE_UTF8; }

    bool checkAndSetNull (Tcl_Obj* arg);
    int copyIntoOutDataBuffer (Tcl_Interp* interp, Tcl_Obj* arg, int idx);
    void bindOutDataBufferTo (SQLDBC_PreparedStatement* stmt, int idx);
    int bindInTo (SQLDBC_PreparedStatement* stmt, int idx, Tcl_Interp* interp, Tcl_Obj* arg);

    Tcl_Obj* getOutObj ();
};

class SdbPrepStmt : public SdbStmt {
    std::vector<Param> params;

    SQLDBC_PreparedStatement* prepstmt () { return (SQLDBC_PreparedStatement*) stmt; }

    int copyOutput (Tcl_Interp* interp);

public:
    SdbPrepStmt(SdbConn* conn);

    void releaseDatabaseHandles () override;

    /**
     * Prepares a given SQL statement for execution.
     *
     * ```tcl
     * set stmt [db prepare {
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
    int prepare (Tcl_Interp* interp, Tcl_Obj* sql);

    /**
     * Binds TCL arguments to corresponding SQL parameter placeholders.
     */
    int bind (Tcl_Interp* interp, int argc, Tcl_Obj* const argv[]);

    /**
     * Executes a prepared SQL statement.
     *
     * Example:
     *
     * ```tcl
     * set stmt [db prepare "
     *   UPDATE room
     *      SET price = price * :MULT
     *    WHERE hno IN (
     *            SELECT hno
     *              FROM hotel
     *             WHERE zip = :ZIP )
     * "]
     * set numRows [db execute $stmt :MULT 0.95 :ZIP "60101"]
     * ```
     *
     * Query Example:
     *
     * ```tcl
     * set stmt [db prepare {
     *   SELECT h.name, r.type, r.free, r.price
     *     FROM room r
     *     JOIN hotel h
     *       ON h.hno = r.hno
     *    WHERE h.zip = :ZIP
     *      AND r.price <= :MAX_PRICE
     *    ORDER BY r.price
     * }]
     * set numRows [db execute -maxrows 100 $stmt :ZIP "60601" :MAX_PRICE 150]
     * ```
     */
    int execute (Tcl_Interp* interp, int idx, int argc, Tcl_Obj* const argv[], ResultSetConfig& config) override;
};

/**
 * Creates new Tcl object that points to the sdb statement.
 */
Tcl_Obj* Tcl_NewSdbStmtObj (SdbStmt* stmt);

/**
 * Reads Tcl object that holds SdbStmt.
 * Returns TCL_ERRROR (and sets statement pointer to NULL) if the object
 * does not hold SdbStmt.
 */
int Tcl_GetSdbStmtFromObj (Tcl_Obj* obj, SdbStmt** stmtPtr);

/**
 * Statements subcommands multiplexor.
 */
int SdbStmt_Cmd (SdbStmt* sdbstmt, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

/**
 * Creates and configures new statement handle.
 */
int SdbStmt_New (SdbConn* sdbconn, Tcl_Interp* interp, int argc, Tcl_Obj* const argv[]);

/**
 * Creates and configures new prepared statement handle.
 */
int SdbPrepStmt_New (SdbConn* sdbconn, Tcl_Interp* interp, int argc, Tcl_Obj* const argv[]);