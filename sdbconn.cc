#include "sdbconn.h"
#include "sdbstmt.h"
#include "sdblob.h"
#include <cstring>

SdbConn::SdbConn(SdbEnv& env) : env(env), conn(nullptr), stmt(nullptr), cmd(nullptr)
{
    env.preserve();
}

SdbConn::~SdbConn()
{
    for (auto it = statements.begin(); it != statements.end(); ++it) {
        SdbStmt* stmt = *it;
        stmt->releaseDatabaseHandles();
    }
    if (stmt) stmt->releaseDatabaseHandles();
    env.releaseConnection(conn);
    env.release();
}

static void SdbConn_Delete (SdbConn* sdbconn)
{
    delete sdbconn;
}

SdbStmt* SdbConn::myStmt()
{
    if (stmt == nullptr) {
        stmt = new SdbStmt(this);
    }
    return stmt;
}

static const NamedValue ISOLATION_LEVELS[] = {
    {"READ UNCOMMITTED",                16, 0 },
    {"READ COMMITTED",                  14, 1 },
    {"READ COMMITTED WITH TABLE LOCKS", 31, 15},
    {"REPEATABLE READ",                 15, 2 },
    {"SERIALIZABLE",                    12, 3 },
    NULL
};

/**
 *  Translates texual representation of the isolation level, passed as a command argument,
 *  into its SQLDBC numeric value.
 */
static int scanIsolationLevel (Tcl_Interp* interp, Tcl_Obj* arg, int* level)
{
    if (Tcl_GetIntFromObj(nullptr, arg, level) == TCL_OK)
        return TCL_OK;
    else
        return findNamedValue("isolation level", ISOLATION_LEVELS, interp, arg, level);
}

static const NamedValue SQL_MODES[] = {
    {"INTERNAL", 8, SQLDBC_INTERNAL},
    {"ANSI",     4, SQLDBC_ANSI    },
    {"DB2",      3, SQLDBC_DB2     },
    {"ORACLE",   6, SQLDBC_ORACLE  },
    {"SAPR3",    5, SQLDBC_SAPR3   },
    NULL
};

/**
 *  Translates textual representation of the SQL mode, passed as an argument, to
 *  its SQLDBC numeric value.
 */
static int scanSqlMode (Tcl_Interp* interp, Tcl_Obj* arg, SQLDBC_SQLMode* mode)
{
    return findNamedValue("SQL Mode", SQL_MODES, interp, arg, (int*) mode);
}

SQLDBC_Statement* SdbConn::createStatement()
{
    return conn->createStatement();
}

SQLDBC_PreparedStatement* SdbConn::createPreparedStatement()
{
    return conn->createPreparedStatement();
}

static const char* CONNECT_OPTIONS[] = {"-autocommit", "-database", "-host", "-isolationlevel", "-key", "-password", "-sqlmode", "-user", NULL};

enum ConnOption { AUTOCOMMIT, DATABASE, HOST, ISOLATIONLEVEL, KEY, PASSWORD, SQLMODE, USER };

int SdbConn::connect(Tcl_Interp* interp, int argc, Tcl_Obj* const argv[])
{
    if (conn == nullptr) {
        conn = env.createConnection();
        if (conn == nullptr) {
            TclSetResult(interp, "SQLDBC could not create a new connection object", TCL_STATIC);
            return TCL_ERROR;
        }
    }

    const char* host   = "";
    const char* dbName = "";
    const char* user   = "";
    const char* pass   = "";

    int hostLen   = 0;
    int dbNameLen = 0;
    int userLen   = 0;
    int passLen   = 0;

    bool keyProvided = false;

    int autoCommit     = -1;
    int isolationLevel = -1;
    union {
        SQLDBC_SQLMode mode;
        int            value;
    } sqlMode = {.value = -1};

    SQLDBC_ConnectProperties props;
    for (int i = 0; i < argc; i += 2) {
        ConnOption opt;
        if (Tcl_GetIndexFromObj(nullptr, argv[i], CONNECT_OPTIONS, "option", 0, (int*) &opt) == TCL_OK) {
            switch (opt) {
                case HOST:     host = Tcl_GetStringFromObj(argv[i + 1], &hostLen); break;
                case DATABASE: dbName = Tcl_GetStringFromObj(argv[i + 1], &dbNameLen); break;
                case USER:     user = Tcl_GetStringFromObj(argv[i + 1], &userLen); break;
                case PASSWORD: pass = Tcl_GetStringFromObj(argv[i + 1], &passLen); break;
                case KEY:
                    keyProvided = true;
                    props.setProperty("KEY", Tcl_GetString(argv[i + 1]));
                    break;
                case AUTOCOMMIT:
                    if (Tcl_GetBooleanFromObj(interp, argv[i + 1], &autoCommit) != TCL_OK) {
                        return TCL_ERROR;
                    }
                    break;
                case ISOLATIONLEVEL:
                    if (scanIsolationLevel(interp, argv[i + 1], &isolationLevel) != TCL_OK) {
                        return TCL_ERROR;
                    }
                    break;
                case SQLMODE:
                    if (scanSqlMode(interp, argv[i + 1], &sqlMode.mode) != TCL_OK) {
                        return TCL_ERROR;
                    }
                    break;
            }
        } else {
            int optNameLen;

            const char* optName = Tcl_GetStringFromObj(argv[i], &optNameLen);
            if (optNameLen < 2 || optName[0] != '-') {
                Tcl_AppendResult(interp, "expected connect option, found ", optName, NULL);
                return TCL_ERROR;
            }
            char key[optNameLen];
            strtoupper(optName + 1, key, sizeof(key));
            const char* value = Tcl_GetString(argv[i + 1]);
            props.setProperty(key, value);
        }
    }

    SQLDBC_Retcode rc;
    if (keyProvided) {
        rc = conn->connect(props);
    } else {
        rc = conn->connect(host, hostLen, dbName, dbNameLen, user, userLen, pass, passLen, SQLDBC_StringEncoding::UTF8, props);
    }
    if (rc != SQLDBC_OK) {
        setTclError(interp, conn->error());
        return TCL_ERROR;
    }
    if (autoCommit >= 0) {
        conn->setAutoCommit(autoCommit);
    }
    if (isolationLevel >= 0) {
        conn->setTransactionIsolation(isolationLevel);
    }
    if (sqlMode.value >= 0) {
        conn->setSQLMode(sqlMode.mode);
    }
    return TCL_OK;
}

Tcl_Obj* SdbConn::getConnProp(Tcl_Interp* interp, const char* name, bool uppercase)
{
    SQLDBC_ConnectProperties props;
    SQLDBC_Retcode           rc = conn->getConnectionFeatures(props);
    if (rc != SQLDBC_OK) {
        setTclError(interp, conn->error());
        return nullptr;
    }
    char key[28];
    if (!uppercase) {
        strtoupper(name, key, sizeof(key));
        name = key;
    }
    const char* value = props.getProperty(name, "");
    return Tcl_NewStringObj(value, -1);
}

Tcl_Obj* SdbConn::getIsolationLevel()
{
    int index;
    switch (conn->getTransactionIsolation()) {
        case 0:  index = 0; break;
        case 1:
        case 10: index = 1; break;
        case 15: index = 2; break;
        case 2:
        case 20: index = 3; break;
        case 3:
        case 30: index = 4; break;
        default: index = -1;
    }
    if (index >= 0) {
        return Tcl_NewStringObj(ISOLATION_LEVELS[index].name, ISOLATION_LEVELS[index].length);
    } else {
        return TCL_STR(UNKNOWN);
    }
}

int SdbConn::configure(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc == 2) {
        Tcl_Obj* sqlMode = getConnProp(interp, "SQLMODE", true);
        if (sqlMode == nullptr) {
            return TCL_ERROR;
        }
        Tcl_Obj* listItems[6];
        listItems[0] = TCL_STR(autocommit);
        listItems[1] = Tcl_NewBooleanObj(conn->getAutoCommit());
        listItems[2] = TCL_STR(isolationlevel);
        listItems[3] = getIsolationLevel();
        listItems[4] = TCL_STR(sqlmode);
        listItems[5] = sqlMode;
        Tcl_SetObjResult(interp, Tcl_NewListObj(6, listItems));
        return TCL_OK;
    }

    if (objc == 3) {
        ConnOption opt;
        if (Tcl_GetIndexFromObj(nullptr, objv[2], CONNECT_OPTIONS, "option", 0, (int*) &opt) == TCL_OK) {
            switch (opt) {
                case AUTOCOMMIT:     Tcl_SetObjResult(interp, Tcl_NewBooleanObj(conn->getAutoCommit())); return TCL_OK;
                case ISOLATIONLEVEL: Tcl_SetObjResult(interp, getIsolationLevel()); return TCL_OK;
                case SQLMODE:        {
                    Tcl_Obj* sqlMode = getConnProp(interp, "SQLMODE", true);
                    if (sqlMode == nullptr) {
                        return TCL_ERROR;
                    }
                    Tcl_SetObjResult(interp, sqlMode);
                    return TCL_OK;
                }
                default: Tcl_AppendResult(interp, Tcl_GetString(objv[2]), " is not retrievable", NULL); return TCL_ERROR;
            }
        } else {
            Tcl_Obj* value = getConnProp(interp, Tcl_GetString(objv[2]) + 1);
            if (value == nullptr) {
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, value);
            return TCL_OK;
        }
    }

    static const char* CONFIGURE_OPTIONS[] = {"-autocommit", "-isolationlevel", "-sqlmode", NULL};
    enum { AUTOCOMMIT, ISOLATIONLEVEL, SQLMODE } opt;
    for (int i = 2; i < objc;) {
        if (Tcl_GetIndexFromObj(interp, objv[i++], CONFIGURE_OPTIONS, "option", 0, (int*) &opt) != TCL_OK) {
            return TCL_ERROR;
        }
        switch (opt) {
            case AUTOCOMMIT: {
                int autocommit;
                if (Tcl_GetBooleanFromObj(interp, objv[i++], &autocommit) != TCL_OK) {
                    return TCL_ERROR;
                }
                conn->setAutoCommit(autocommit);
                break;
            }
            case ISOLATIONLEVEL: {
                int isolationLevel;
                if (scanIsolationLevel(interp, objv[i++], &isolationLevel) != TCL_OK) {
                    return TCL_ERROR;
                }
                conn->setTransactionIsolation(isolationLevel);
                break;
            }
            case SQLMODE: {
                SQLDBC_SQLMode sqlmode;
                if (scanSqlMode(interp, objv[i++], &sqlmode) != TCL_OK) {
                    return TCL_ERROR;
                }
                conn->setSQLMode(sqlmode);
                break;
            }
        }
    }

    return TCL_OK;
}

int SdbConn::is(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "state");
        return TCL_ERROR;
    }

    static const char* STATES[] = {"connected", "unicode", "usable", NULL};
    enum { IS_CONNECTED, IS_UNICODE, IS_USABLE } index;

    if (Tcl_GetIndexFromObj(interp, objv[2], STATES, "state", 0, (int*) &index) != TCL_OK) {
        return TCL_ERROR;
    }
    SQLDBC_Bool result;
    switch (index) {
        case IS_CONNECTED: result = conn->isConnected(); break;
        case IS_USABLE:    result = conn->checkConnection(); break;
        case IS_UNICODE:   result = conn->isUnicodeDatabase(); break;
    }
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result));
    return TCL_OK;
}

int SdbConn::get(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "property");
        return TCL_ERROR;
    }

    static const char* properties[] = {"datetimeformat", "kernelversion", NULL};
    enum { DATETIMEFORMAT, KERNELVERSION } index;

    if (Tcl_GetIndexFromObj(interp, objv[2], properties, "property", 0, (int*) &index) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (index) {
        case DATETIMEFORMAT: {
            static const char* format_names[] = {"Unknown", "INTERNAL", "ISO", "USA", "Europe", "Japan", "Oracle", "TSEurope"};

            size_t index = conn->getDateTimeFormat();
            if (index >= sizeof(format_names) / sizeof(format_names[0])) {
                index = 0;
            }
            TclSetResult(interp, format_names[index], TCL_STATIC);
            break;
        }
        case KERNELVERSION: Tcl_SetObjResult(interp, Tcl_NewIntObj(conn->getKernelVersion())); break;
    }
    return TCL_OK;
}

int SdbConn::commit(Tcl_Interp* interp)
{
    if (conn->commit() != SQLDBC_OK) {
        setTclError(interp, conn->error());
        return TCL_ERROR;
    }
    return TCL_OK;
}

int SdbConn::rollback(Tcl_Interp* interp)
{
    if (conn->rollback() != SQLDBC_OK) {
        setTclError(interp, conn->error());
        return TCL_ERROR;
    }
    return TCL_OK;
}

int SdbConn::newStatement(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc % 2 != 0) {
        Tcl_WrongNumArgs(interp, 2, objv, "?option value ...?");
        return TCL_ERROR;
    }

    return SdbStmt_New(this, interp, objc - 2, objv + 2);
}

int SdbConn::prepare(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc % 2 == 0) {
        // db prepare -cursorname data "SELECT ..."
        Tcl_WrongNumArgs(interp, 2, objv, "?option value ... ? sql");
        return TCL_ERROR;
    }

    return SdbPrepStmt_New(this, interp, objc - 2, objv + 2);
}

int SdbConn::batch(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc < 3) {
        // db batch $stmt "CREATE TABLE ..." "CREATE INDEX ..."
        Tcl_WrongNumArgs(interp, 2, objv, "?cursor? sql ?sql ... ?");
        return TCL_ERROR;
    }

    int      i = 2;
    SdbStmt* stmt;
    if (Tcl_GetSdbStmtFromObj(objv[i], &stmt) == TCL_OK) {
        i++;
    } else {
        stmt = myStmt();
    }

    return stmt->batch(interp, objc - i, objv + i);
}

int SdbConn::columns(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc > 4) {
        // db columns
        // db columns -count
        // db columns 1
        // db columns $stmt
        // db columns $stmt -count
        // db columns $stmt 1
        Tcl_WrongNumArgs(interp, 2, objv, "?cursor? ?columnNo|-count|-labels?");
        return TCL_ERROR;
    }

    static const char* options[] = {"-count", "-labels", NULL};

    enum { COUNT, LABELS, COLUMN, ALL } option = ALL;

    int      colNo;
    SdbStmt* stmt;

    if (objc == 2) {
        stmt = myStmt();
    } else {
        int i = 2;
        if (Tcl_GetSdbStmtFromObj(objv[i], &stmt) == TCL_OK) {
            i++;
        } else {
            stmt = myStmt();
        }
        if (i < objc) {
            if (Tcl_GetString(objv[i])[0] == '-') {
                if (Tcl_GetIndexFromObj(interp, objv[i], options, "option", 0, (int*) &option) != TCL_OK) {
                    return TCL_ERROR;
                }
            } else {
                if (Tcl_GetIntFromObj(interp, objv[i], &colNo) != TCL_OK) {
                    return TCL_ERROR;
                }
                option = COLUMN;
            }
        }
    }

    if (!stmt->isQuery()) {
        TclSetResult(interp, "the last executed statement did not return a result set", TCL_STATIC);
        return TCL_ERROR;
    } else if (option == COLUMN && (colNo < 1 || stmt->getColumnCount() < colNo)) {
        char numCols[8];
        snprintf(numCols, sizeof(numCols), "%u", stmt->getColumnCount());
        Tcl_AppendResult(interp, Tcl_GetString(objv[objc - 1]), " is outside the valid range (1..", numCols, ") for this query", nullptr);
        return TCL_ERROR;
    }

    Tcl_Obj* res;
    switch (option) {
        case COUNT:  res = Tcl_NewIntObj(stmt->getColumnCount()); break;
        case LABELS: res = stmt->getColumnLabels(); break;
        case COLUMN: res = stmt->getColumnInfo(interp, colNo); break;
        case ALL:    res = stmt->getAllColumnsInfo(interp); break;
    }
    Tcl_SetObjResult(interp, res);
    return TCL_OK;
}

int SdbConn::execute(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc < 3) {
        // db execute "SELECT * FROM dual"
        // db execute -name temp -maxrows 100 $stmt "SELECT * FROM dual"
        Tcl_WrongNumArgs(interp, 2, objv, "?option value ... ? ?cursor? ?sql|?arg ... ??");
        return TCL_ERROR;
    }

    ResultSetConfig rsetConfig;
    int             i = 2;
    if (rsetConfig.init(interp, &i, objc, objv) != TCL_OK) {
        return TCL_ERROR;
    }

    SdbStmt* stmt;
    if (Tcl_GetSdbStmtFromObj(objv[i], &stmt) == TCL_OK) {
        i++;
    } else {
        stmt = myStmt();
    }

    return stmt->execute(interp, i, objc, objv, rsetConfig);
}

int SdbConn::fetch(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc < 3) {
        // db fetch row
        // db fetch -asarray $stmt data nulls
        Tcl_WrongNumArgs(interp, 2, objv, "?options? ?stmt? rowVar ?nullIndVar?");
        return TCL_ERROR;
    }

    static const char* options[] = {"-asarray", "-first", "-last", "-next", "-previous", "-seek", NULL};
    enum { ASARRAY, FIRST, LAST, NEXT, PREVIOUS, SEEK } opt;

    int  row     = 0;
    bool asArray = false;

    SdbStmt::SeekType seek = SdbStmt::SeekType::Next;

    int i = 2;
    while (i < objc && maybeOption(objv[i])) {
        if (Tcl_GetIndexFromObj(interp, objv[i++], options, "option", 0, (int*) &opt) != TCL_OK) {
            return TCL_ERROR;
        }
        switch (opt) {
            case ASARRAY: asArray = true; break;
            case SEEK:    {
                if (i == objc) {
                    Tcl_AppendResult(interp, options[opt], " needs a row number/offset", NULL);
                    return TCL_ERROR;
                }
                const char* arg = Tcl_GetString(objv[i++]);
                if (arg[0] == '#') {
                    seek = SdbStmt::SeekType::Absolute;
                    arg++;
                } else {
                    seek = SdbStmt::SeekType::Relative;
                }
                if (Tcl_GetInt(interp, arg, &row) != TCL_OK) {
                    return TCL_ERROR;
                }
                break;
            }
            case FIRST:    seek = SdbStmt::SeekType::First; break;
            case LAST:     seek = SdbStmt::SeekType::Last; break;
            case NEXT:     seek = SdbStmt::SeekType::Next; break;
            case PREVIOUS: seek = SdbStmt::SeekType::Previous; break;
        }
    }

    if (i >= objc) {
        Tcl_WrongNumArgs(interp, i, objv, "?stmt? rowVar ?nullIndVar?");
        return TCL_ERROR;
    }

    SdbStmt* stmt;
    if (Tcl_GetSdbStmtFromObj(objv[i], &stmt) == TCL_OK) {
        ++i;
    } else {
        stmt = myStmt();
    }

    if (i >= objc) {
        Tcl_WrongNumArgs(interp, i, objv, "rowVar ?nullIndVar?");
        return TCL_ERROR;
    }

    Tcl_Obj* rowVarName = objv[i++];
    Tcl_Obj* nullsVarName = i < objc ? objv[i] : nullptr;

    int rc = stmt->fetch(interp, seek, row);
    if (rc == TCL_OK) {
        if (stmt->getRowData(interp, rowVarName, nullsVarName, asArray) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(true));
    } else if (rc == TCL_BREAK) {
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(false));
    } else {
        return TCL_ERROR;
    }
    return TCL_OK;
}

int SdbConn::rowNumber(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc != 2 && objc != 3) {
        // db rownumber $stmt
        Tcl_WrongNumArgs(interp, 2, objv, "?stmt?");
        return TCL_ERROR;
    }

    SdbStmt* stmt;
    if (objc == 2) {
        stmt = myStmt();
    } else if (Tcl_GetSdbStmtFromObj(objv[2], &stmt) != TCL_OK) {
        const char* typeName = objv[2]->typePtr ? objv[2]->typePtr->name : "string";
        Tcl_AppendResult(interp, "a statement handler is expected, but a ", typeName, " was given", nullptr);
        return TCL_ERROR;
    }

    if (!stmt->isQuery()) {
        TclSetResult(interp, "the last executed SQL was not a query", TCL_STATIC);
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(stmt->getRowNumber()));
    return TCL_OK;
}

int SdbConn::serial(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc > 4) {
        // db serial -last $stmt
        Tcl_WrongNumArgs(interp, 2, objv, "?-first|-last? ?stmtHandle?");
        return TCL_ERROR;
    }

    static const char* options[] = {"-first", "-last", NULL};
    enum { FIRST, LAST } option  = LAST;

    SdbStmt* stmt;
    if (objc == 2) {
        stmt = myStmt();
    } else {
        int i = objc - 1;
        if (Tcl_GetSdbStmtFromObj(objv[i], &stmt) == TCL_OK) {
            --i;
        } else {
            stmt = myStmt();
        }
        if (i == 2) {
            if (Tcl_GetIndexFromObj(interp, objv[i], options, "option", 0, (int*) &option) != TCL_OK) {
                return TCL_ERROR;
            }
        }
    }

    return stmt->serial(interp, option == LAST);
}

int SdbConn::close(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc != 3) {
        // db close $lob
        Tcl_WrongNumArgs(interp, 2, objv, "lob");
        return TCL_ERROR;
    }

    SdbLob* lob;
    if (Tcl_GetSdbLobFromObj(interp, objv[2], &lob) != TCL_OK) {
        return TCL_ERROR;
    }

    return lob->close(interp);
}

int SdbConn::length(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc != 3) {
        // db length $lob
        Tcl_WrongNumArgs(interp, 2, objv, "lob");
        return TCL_ERROR;
    }

    SdbLob* lob;
    if (Tcl_GetSdbLobFromObj(interp, objv[2], &lob) != TCL_OK) {
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, lob->getLength());
    return TCL_OK;
}

int SdbConn::optimalSize(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc != 3) {
        // db optimalsize $lob
        Tcl_WrongNumArgs(interp, 2, objv, "lob");
        return TCL_ERROR;
    }

    SdbLob* lob;
    if (Tcl_GetSdbLobFromObj(interp, objv[2], &lob) != TCL_OK) {
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, lob->getOptimalSize());
    return TCL_OK;
}

int SdbConn::position(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc != 3) {
        // db position $lob
        Tcl_WrongNumArgs(interp, 2, objv, "lob");
        return TCL_ERROR;
    }

    SdbLob* lob;
    if (Tcl_GetSdbLobFromObj(interp, objv[2], &lob) != TCL_OK) {
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, lob->getPosition());
    return TCL_OK;
}

int SdbConn::read(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc < 3 || objc > 6) {
        // db read -from $pos $lob $numChars
        Tcl_WrongNumArgs(interp, 2, objv, "?-from pos? lob numChars");
        return TCL_ERROR;
    }

    SdbLob* lob;
    if (Tcl_GetSdbLobFromObj(interp, objv[objc - 2], &lob) != TCL_OK) {
        return TCL_ERROR;
    }

    int length;
    if (Tcl_GetIntFromObj(interp, objv[objc - 1], &length) != TCL_OK) {
        return TCL_ERROR;
    }

    SQLDBC_Length position = 0;
    if (objc == 6) {
        static const char* options[] = {"-from", NULL};
        int                opt;
        if (Tcl_GetIndexFromObj(interp, objv[2], options, "option", 0, &opt) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Tcl_GetWideIntFromObj(interp, objv[3], &position) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    Tcl_Obj* data;
    if (lob->read(interp, position, length, &data) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, data);
    return TCL_OK;
}

int SdbConn::write(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc != 4) {
        // db write $lob $data
        Tcl_WrongNumArgs(interp, 2, objv, "lob data");
        return TCL_ERROR;
    }

    SdbLob* lob;
    if (Tcl_GetSdbLobFromObj(interp, objv[2], &lob) != TCL_OK) {
        return TCL_ERROR;
    }

    return lob->write(interp, objv[3]);
}

int SdbConn::disconnect(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    Tcl_DeleteCommandFromToken(interp, cmd);
    return TCL_OK;
}

static int SdbConn_Cmd (SdbConn* sdbconn, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "stmt-handle|lob-handle|db-subcommand ?arg ... ?");
        return TCL_ERROR;
    }

    static const char* subcommands[] = {"batch",   "close", "columns",  "commit",    "configure",    "disconnect",  "execute",
                                        "fetch",   "get",   "is",       "length",    "newstatement", "optimalsize", "position",
                                        "prepare", "read",  "rollback", "rownumber", "serial",       "write",       nullptr};
    enum {
        BATCH,
        CLOSE,
        COLUMNS,
        COMMIT,
        CONFIGURE,
        DISCONNECT,
        EXECUTE,
        FETCH,
        GET,
        IS,
        LENGTH,
        NEWSTATEMENT,
        OPTIMALSIZE,
        POSITION,
        PREPARE,
        READ,
        ROLLBACK,
        ROWNUMBER,
        SERIAL,
        WRITE
    } subcommand;

    if (Tcl_GetIndexFromObj(interp, objv[1], subcommands, "subcommand", 0, (int*) &subcommand) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (subcommand) {
        case COMMIT:       return sdbconn->commit(interp);
        case CONFIGURE:    return sdbconn->configure(interp, objc, objv);
        case DISCONNECT:   return sdbconn->disconnect(interp, objc, objv);
        case GET:          return sdbconn->get(interp, objc, objv);
        case IS:           return sdbconn->is(interp, objc, objv);
        case NEWSTATEMENT: return sdbconn->newStatement(interp, objc, objv);
        case PREPARE:      return sdbconn->prepare(interp, objc, objv);
        case ROLLBACK:     return sdbconn->rollback(interp);
        // Statements
        case BATCH:        return sdbconn->batch(interp, objc, objv);
        case COLUMNS:      return sdbconn->columns(interp, objc, objv);
        case EXECUTE:      return sdbconn->execute(interp, objc, objv);
        case FETCH:        return sdbconn->fetch(interp, objc, objv);
        case ROWNUMBER:    return sdbconn->rowNumber(interp, objc, objv);
        case SERIAL:       return sdbconn->serial(interp, objc, objv);
        // LOBs
        case CLOSE:        return sdbconn->close(interp, objc, objv);
        case LENGTH:       return sdbconn->length(interp, objc, objv);
        case OPTIMALSIZE:  return sdbconn->optimalSize(interp, objc, objv);
        case POSITION:     return sdbconn->position(interp, objc, objv);
        case READ:         return sdbconn->read(interp, objc, objv);
        case WRITE:        return sdbconn->write(interp, objc, objv);
    }
    return TCL_OK;
}

int SdbConn::createCommand(Tcl_Interp* interp, const char* name)
{
    cmd = Tcl_CreateObjCommand(interp, name, (Tcl_ObjCmdProc*) SdbConn_Cmd, this, (Tcl_CmdDeleteProc*) SdbConn_Delete);
    if (cmd == nullptr) {
        Tcl_AppendResult(interp, "cannot create ", name, " command", NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}
