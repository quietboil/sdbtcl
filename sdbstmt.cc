#include "sdbconn.h"
#include "sdbstmt.h"
#include "sdblob.h"
#include <memory>
#include <cstring>

static const NamedValue RESULT_SET_TYPES[] = {
    {"FORWARD ONLY",       12, SQLDBC_Statement::ResultSetType::FORWARD_ONLY      },
    {"SCROLL SENSITIVE",   16, SQLDBC_Statement::ResultSetType::SCROLL_SENSITIVE  },
    {"SCROLL INSENSITIVE", 18, SQLDBC_Statement::ResultSetType::SCROLL_INSENSITIVE}
};

static const NamedValue RESULT_SET_CONCURRENCY_TYPES[] = {
    {"UPDATABLE",                 9,  SQLDBC_Statement::ConcurrencyType::CONCUR_UPDATABLE                },
    {"READ ONLY",                 9,  SQLDBC_Statement::ConcurrencyType::CONCUR_READ_ONLY                },
    {"UPDATABLE LOCK OPTIMISTIC", 25, SQLDBC_Statement::ConcurrencyType::CONCUR_UPDATABLE_LOCK_OPTIMISTIC}
};

static const char* CURSOR_OPTIONS[] = {"-concurrencytype", "-cursor", "-fetchsize", "-maxrows", "-resultsettype", NULL};
enum CursorOptions { CONCURRENCYTYPE, CURSOR, FETCHSIZE, MAXROWS, RESULTSETTYPE };

// ------------------------------------------------------------------------------------------------

Column::Column(SQLDBC_ResultSetMetaData* info, int columnNo)
{
    char          buffer[100];
    SQLDBC_Length strLen;
    if (info->getColumnLabel(columnNo, buffer, SQLDBC_StringEncoding::UTF8, sizeof(buffer), &strLen) == SQLDBC_OK) {
        Tcl_IncrRefCount(label = Tcl_NewStringObj(buffer, strLen));
    } else if (info->getColumnName(columnNo, buffer, SQLDBC_StringEncoding::UTF8, sizeof(buffer), &strLen) == SQLDBC_OK) {
        Tcl_IncrRefCount(label = Tcl_NewStringObj(buffer, strLen));
    } else {
        label = TCL_STR(UNKNOWN);
    }
    length     = info->getColumnLength(columnNo);
    precision  = info->getPrecision(columnNo);
    scale      = info->getScale(columnNo);
    byteLength = info->getPhysicalLength(columnNo);
    sqlType    = info->getColumnType(columnNo);
    switch (sqlType) {
        case SQLDBC_SQLTYPE_FIXED:
        case SQLDBC_SQLTYPE_FLOAT:
        case SQLDBC_SQLTYPE_VFLOAT:
            if (precision > 15)
                hostType = SQLDBC_HOSTTYPE_UTF8;
            else if (scale > 0)
                hostType = SQLDBC_HOSTTYPE_DOUBLE;
            else if (precision > 9)
                hostType = SQLDBC_HOSTTYPE_INT8;
            else
                hostType = SQLDBC_HOSTTYPE_INT4;
            break;
        case SQLDBC_SQLTYPE_BOOLEAN:
        case SQLDBC_SQLTYPE_SMALLINT:
        case SQLDBC_SQLTYPE_INTEGER:  hostType = SQLDBC_HOSTTYPE_INT4; break;
        case SQLDBC_SQLTYPE_CHB:
        case SQLDBC_SQLTYPE_VARCHARB: hostType = SQLDBC_HOSTTYPE_BINARY; break;
        case SQLDBC_SQLTYPE_STRB:
        case SQLDBC_SQLTYPE_LONGB:    hostType = SQLDBC_HOSTTYPE_BLOB; break;
        case SQLDBC_SQLTYPE_STRA:
        case SQLDBC_SQLTYPE_STRE:
        case SQLDBC_SQLTYPE_STRUNI:
        case SQLDBC_SQLTYPE_LONGA:
        case SQLDBC_SQLTYPE_LONGE:
        case SQLDBC_SQLTYPE_LONGUNI:  hostType = SQLDBC_HOSTTYPE_UTF8_CLOB; break;
        default:                      hostType = SQLDBC_HOSTTYPE_UTF8; break;
    }
}

int ResultSetConfig::init(Tcl_Interp* interp, int* idxPtr, int objc, Tcl_Obj* const objv[])
{
    int i = *idxPtr;
    while (i < objc - 1 && maybeOption(objv[i])) {
        Tcl_Obj* opt = objv[i++];
        Tcl_Obj* val = objv[i++];

        CursorOptions option;
        if (Tcl_GetIndexFromObj(interp, opt, CURSOR_OPTIONS, "option", 0, (int*) &option) != TCL_OK) {
            return TCL_ERROR;
        }
        switch (option) {
            case CONCURRENCYTYPE: concurrency = val; break;
            case CURSOR:          name = val; break;
            case FETCHSIZE:       fetchSize = val; break;
            case MAXROWS:         maxRows = val; break;
            case RESULTSETTYPE:   type = val; break;
        }
    }
    *idxPtr = i;
    return TCL_OK;
}

// ------------------------------------------------------------------------------------------------

SdbStmt::SdbStmt(SdbConn* conn) : SdbStmt(conn, 0)
{
    stmt = conn->createStatement();
}

SdbStmt::~SdbStmt()
{
    if (conn) {
        conn->eraseStatement(this);
        releaseDatabaseHandles();
    }
}

void SdbStmt::releaseDatabaseHandles()
{
    if (conn) {
        if (stmt) {
            if (rset) {
                rset     = nullptr;
                rsetInfo = nullptr;
                cols.clear();
            }
            stmt->getConnection()->releaseStatement(stmt);
            stmt = nullptr;
        }
        conn = nullptr;
    }
}

int SdbStmt::setMaxRows(Tcl_Interp* interp, Tcl_Obj* numObj)
{
    int maxRows;
    if (Tcl_GetIntFromObj(interp, numObj, &maxRows) != TCL_OK) {
        return TCL_ERROR;
    }
    stmt->setMaxRows(maxRows);
    return TCL_OK;
}

int SdbStmt::setResultSetType(Tcl_Interp* interp, Tcl_Obj* typeObj)
{
    SQLDBC_Statement::ResultSetType type;
    if (findNamedValue("result set type", RESULT_SET_TYPES, interp, typeObj, (int*) &type) != TCL_OK) {
        return TCL_ERROR;
    }
    stmt->setResultSetType(type);
    return TCL_OK;
}

int SdbStmt::setResultSetConcurrencyType(Tcl_Interp* interp, Tcl_Obj* typeObj)
{
    SQLDBC_Statement::ConcurrencyType type;
    if (findNamedValue("result set concurrency type", RESULT_SET_CONCURRENCY_TYPES, interp, typeObj, (int*) &type) != TCL_OK) {
        return TCL_ERROR;
    }
    stmt->setResultSetConcurrencyType(type);
    return TCL_OK;
}

int SdbStmt::setCursorName(Tcl_Interp* interp, Tcl_Obj* nameObj)
{
    int         nameLen;
    const char* name = Tcl_GetStringFromObj(nameObj, &nameLen);
    stmt->setCursorName(name, nameLen, SQLDBC_StringEncoding::UTF8);
    return TCL_OK;
}

int SdbStmt::setFetchSize(Tcl_Interp* interp, Tcl_Obj* sizeObj)
{
    int fetchSize;
    if (Tcl_GetIntFromObj(interp, sizeObj, &fetchSize) != TCL_OK) {
        return TCL_ERROR;
    }
    if (fetchSize < 0 || INT16_MAX < fetchSize) {
        TclSetResult(interp, "outside of allowed range 0..32767", TCL_STATIC);
        return TCL_ERROR;
    }
    this->fetchSize = fetchSize;
    return TCL_OK;
}

int SdbStmt::configure(Tcl_Interp* interp, ResultSetConfig& config)
{
    int rc = TCL_OK;
    if (rc == TCL_OK && config.name) rc = setCursorName(interp, config.name);
    if (rc == TCL_OK && config.type) rc = setResultSetType(interp, config.type);
    if (rc == TCL_OK && config.concurrency) rc = setResultSetConcurrencyType(interp, config.concurrency);
    if (rc == TCL_OK && config.fetchSize) rc = setFetchSize(interp, config.fetchSize);
    if (rc == TCL_OK && config.maxRows) rc = setMaxRows(interp, config.maxRows);
    return rc;
}

void SdbStmt::clearResults()
{
    if (rset) {
        rset->close();
        rset     = nullptr;
        rsetInfo = nullptr;
        cols.clear();
    }
}

int SdbStmt::execute(Tcl_Interp* interp, int idx, int objc, Tcl_Obj* const objv[], ResultSetConfig& config)
{
    if (objc - idx != 1) {
        // db execute $stmt "select * from dual"
        Tcl_WrongNumArgs(interp, idx, objv, "sql");
        return TCL_ERROR;
    }

    clearResults();

    if (configure(interp, config) != TCL_OK) {
        return TCL_ERROR;
    }

    const char* sql = Tcl_GetString(objv[idx]);
    if (stmt->execute(sql) != SQLDBC_OK) {
        setTclError(interp, stmt->error());
        return TCL_ERROR;
    }

    return setExecuteResult(interp);
}

int SdbStmt::setExecuteResult(Tcl_Interp* interp)
{
    int numRows;
    if (stmt->isQuery()) {
        rset = stmt->getResultSet();
        if (fetchSize >= 0) {
            rset->setFetchSize(fetchSize);
        }
        rsetInfo    = rset->getResultSetMetaData();
        int numCols = rsetInfo->getColumnCount();
        cols.reserve(numCols);
        for (int col = 1; col <= numCols; col++) {
            cols.emplace_back(rsetInfo, col);
        }
        numRows = rset->getResultCount();
    } else {
        numRows = stmt->getRowsAffected();
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(numRows));
    return TCL_OK;
}

int SdbStmt::batch(Tcl_Interp* interp, int argc, Tcl_Obj* const argv[])
{
    clearResults();

    for (int i = 0; i < argc; i++) {
        int sqlLen;

        const char* sql = Tcl_GetStringFromObj(argv[i], &sqlLen);
        if (stmt->addBatch(sql, sqlLen, SQLDBC_StringEncoding::UTF8) != SQLDBC_OK) {
            goto Error_Exit;
        }
    }

    if (stmt->executeBatch() == SQLDBC_OK) {
        int        batchSize = stmt->getBatchSize();
        const int* rowStats  = stmt->getRowStatus();

        Tcl_Obj* items[batchSize];
        for (int i = 0; i < batchSize; i++) {
            items[i] = Tcl_NewIntObj(rowStats[i]);
        }
        Tcl_SetObjResult(interp, Tcl_NewListObj(batchSize, items));
        stmt->clearBatch();
        return TCL_OK;
    }
Error_Exit:
    setTclError(interp, stmt->error());
    stmt->clearBatch();
    return TCL_ERROR;
}

static Tcl_Obj* const DATA_TYPES[] = {
    Tcl_NewStringObj("FIXED", 5),             // SQLDBC_SQLTYPE_FIXED         = 0,
    Tcl_NewStringObj("FLOAT", 5),             // SQLDBC_SQLTYPE_FLOAT         = 1,
    Tcl_NewStringObj("CHAR ASCII", 10),       // SQLDBC_SQLTYPE_CHA           = 2,
    Tcl_NewStringObj("CHAR EBCDIC", 11),      // SQLDBC_SQLTYPE_CHE           = 3,
    Tcl_NewStringObj("CHAR BYTE", 9),         // SQLDBC_SQLTYPE_CHB           = 4,
    Tcl_NewStringObj("ROWID", 5),             // SQLDBC_SQLTYPE_ROWID         = 5,
    Tcl_NewStringObj("CLOB ASCII", 10),       // SQLDBC_SQLTYPE_STRA          = 6,
    Tcl_NewStringObj("LONG EBCDIC", 11),      // SQLDBC_SQLTYPE_STRE          = 7,
    Tcl_NewStringObj("BLOB", 4),              // SQLDBC_SQLTYPE_STRB          = 8,
    Tcl_NewStringObj("STRDB", 5),             // SQLDBC_SQLTYPE_STRDB         = 9,
    Tcl_NewStringObj("DATE", 4),              // SQLDBC_SQLTYPE_DATE          = 10,
    Tcl_NewStringObj("TIME", 4),              // SQLDBC_SQLTYPE_TIME          = 11,
    Tcl_NewStringObj("VFLOAT", 6),            // SQLDBC_SQLTYPE_VFLOAT        = 12,
    Tcl_NewStringObj("TIMESTAMP", 9),         // SQLDBC_SQLTYPE_TIMESTAMP     = 13,
    Tcl_NewStringObj("UNKNOWN", 7),           // SQLDBC_SQLTYPE_UNKNOWN       = 14,
    Tcl_NewStringObj("NUMBER", 6),            // SQLDBC_SQLTYPE_NUMBER        = 15,
    Tcl_NewStringObj("NONUMBER", 8),          // SQLDBC_SQLTYPE_NONUMBER      = 16,
    Tcl_NewStringObj("DURATION", 8),          // SQLDBC_SQLTYPE_DURATION      = 17,
    Tcl_NewStringObj("DBYTEEBCDIC", 11),      // SQLDBC_SQLTYPE_DBYTEEBCDIC   = 18,
    Tcl_NewStringObj("LONG ASCII", 10),       // SQLDBC_SQLTYPE_LONGA         = 19,
    Tcl_NewStringObj("LONG EBCDIC", 11),      // SQLDBC_SQLTYPE_LONGE         = 20,
    Tcl_NewStringObj("LONG BYTE", 9),         // SQLDBC_SQLTYPE_LONGB         = 21,
    Tcl_NewStringObj("LONGDB", 6),            // SQLDBC_SQLTYPE_LONGDB        = 22,
    Tcl_NewStringObj("BOOLEAN", 7),           // SQLDBC_SQLTYPE_BOOLEAN       = 23,
    Tcl_NewStringObj("CHAR UNICODE", 12),     // SQLDBC_SQLTYPE_UNICODE       = 24,
    Tcl_NewStringObj("DTFILLER1", 9),         // SQLDBC_SQLTYPE_DTFILLER1     = 25,
    Tcl_NewStringObj("DTFILLER2", 9),         // SQLDBC_SQLTYPE_DTFILLER2     = 26,
    Tcl_NewStringObj("VOID", 4),              // SQLDBC_SQLTYPE_VOID          = 27,
    Tcl_NewStringObj("DTFILLER4", 9),         // SQLDBC_SQLTYPE_DTFILLER4     = 28,
    Tcl_NewStringObj("SMALLINT", 8),          // SQLDBC_SQLTYPE_SMALLINT      = 29,
    Tcl_NewStringObj("INTEGER", 7),           // SQLDBC_SQLTYPE_INTEGER       = 30,
    Tcl_NewStringObj("VARCHAR ASCII", 13),    // SQLDBC_SQLTYPE_VARCHARA      = 31,
    Tcl_NewStringObj("VARCHAR EBCDIC", 14),   // SQLDBC_SQLTYPE_VARCHARE      = 32,
    Tcl_NewStringObj("VARCHAR BYTE", 12),     // SQLDBC_SQLTYPE_VARCHARB      = 33,
    Tcl_NewStringObj("CLOB UNICODE", 12),     // SQLDBC_SQLTYPE_STRUNI        = 34,
    Tcl_NewStringObj("LONG UNICODE", 12),     // SQLDBC_SQLTYPE_LONGUNI       = 35,
    Tcl_NewStringObj("VARCHAR UNICODE", 15),  // SQLDBC_SQLTYPE_VARCHARUNI    = 36,
    Tcl_NewStringObj("UDT", 3),               // SQLDBC_SQLTYPE_UDT           = 37,
    Tcl_NewStringObj("ABAPTABHANDLE", 13),    // SQLDBC_SQLTYPE_ABAPTABHANDLE = 38,
    Tcl_NewStringObj("DWYDE", 5)              // SQLDBC_SQLTYPE_DWYDE         = 39,
};

int SdbStmt::getRowNumber()
{
    return rset ? rset->getRowNumber() : 0;
}

int SdbStmt::serial(Tcl_Interp* interp, bool last)
{
    SQLDBC_Length  keyLen;
    Tcl_WideInt    keyVal;
    SQLDBC_Int4    tag = last ? SQLDBC_LAST_INSERTED_SERIAL : SQLDBC_FIRST_INSERTED_SERIAL;
    SQLDBC_Retcode rc  = stmt->getLastInsertedKey(tag, SQLDBC_HOSTTYPE_INT8, &keyVal, &keyLen, sizeof(keyVal));
    if (rc == SQLDBC_NOT_OK) {
        setTclError(interp, stmt->error());
        return TCL_ERROR;
    }
    if (rc == SQLDBC_OK) {
        Tcl_SetObjResult(interp, Tcl_NewWideIntObj(keyVal));
    } else {
        // SQLDBC_NO_DATA_FOUND will return void value
    }
    return TCL_OK;
}

Tcl_Obj* SdbStmt::getColumnLabels()
{
    Tcl_Obj*  names[cols.size()];
    Tcl_Obj** item = names;
    for (auto it = cols.cbegin(); it != cols.cend(); ++it) {
        Tcl_IncrRefCount(*item++ = it->label);
    }
    return Tcl_NewListObj(item - names, names);
}

Tcl_Obj* SdbStmt::getColumnInfo(Tcl_Interp* interp, int colNo)
{
    Tcl_Obj*  items[22];
    Tcl_Obj** item = &items[0];

    char          buffer[100];
    SQLDBC_Length strLen;

    auto& col = cols.at(colNo - 1);

    if (rsetInfo->getSchemaName(colNo, buffer, SQLDBC_StringEncoding::UTF8, sizeof(buffer), &strLen) == SQLDBC_OK) {
        *item++ = TCL_STR(schema);
        *item++ = Tcl_NewStringObj(buffer, strLen);
    }
    if (rsetInfo->getTableName(colNo, buffer, SQLDBC_StringEncoding::UTF8, sizeof(buffer), &strLen) == SQLDBC_OK) {
        *item++ = TCL_STR(table);
        *item++ = Tcl_NewStringObj(buffer, strLen);
    }
    if (rsetInfo->getColumnName(colNo, buffer, SQLDBC_StringEncoding::UTF8, sizeof(buffer), &strLen) == SQLDBC_OK) {
        *item++ = TCL_STR(column);
        *item++ = Tcl_NewStringObj(buffer, strLen);
    }
    *item++ = TCL_STR(label);
    Tcl_IncrRefCount(*item++ = col.label);

    SQLDBC_SQLType sqlType  = col.sqlType;
    Tcl_Obj*       typeName = DATA_TYPES[sqlType];
    // check if this is the first run and pin the string
    if (typeName->refCount == 0) {
        typeName->refCount = 1;
    }
    *item++ = TCL_STR(type);
    Tcl_IncrRefCount(*item++ = typeName);

    *item++ = TCL_STR(length);
    *item++ = Tcl_NewIntObj(col.length);

    *item++ = TCL_STR(precision);
    *item++ = Tcl_NewIntObj(col.precision);

    *item++ = TCL_STR(scale);
    *item++ = Tcl_NewIntObj(col.scale);

    *item++ = TCL_STR(bytelength);
    *item++ = Tcl_NewIntObj(col.byteLength);

    SQLDBC_ResultSetMetaData::ColumnNullBehavior isNullable = rsetInfo->isNullable(colNo);

    *item++ = TCL_STR(nullable);
    if (isNullable == SQLDBC_ResultSetMetaData::ColumnNullBehavior::columnNullableUnknown) {
        *item++ = Tcl_NewObj();
    } else {
        *item++ = Tcl_NewBooleanObj(isNullable);
    }

    bool isWritable = rsetInfo->isWritable(colNo);
    *item++         = TCL_STR(writable);
    *item++         = Tcl_NewBooleanObj(isWritable);

    return Tcl_NewListObj(item - items, items);
}

int SdbStmt::fetch(Tcl_Interp* interp, SeekType seek, int row)
{
    SQLDBC_Retcode rc;
    switch (seek) {
        case Next:     rc = rset->next(); break;
        case Previous: rc = rset->previous(); break;
        case First:    rc = rset->first(); break;
        case Last:     rc = rset->last(); break;
        case Absolute: rc = rset->absolute(row); break;
        case Relative: rc = rset->relative(row); break;
    }
    if (rc == SQLDBC_NOT_OK) {
        setTclError(interp, rset->error());
        return TCL_ERROR;
    }
    return rc == SQLDBC_OK ? TCL_OK : TCL_BREAK;
}

Tcl_Obj* SdbStmt::getAllColumnsInfo(Tcl_Interp* interp)
{
    int       numCols = cols.size();
    Tcl_Obj*  items[numCols];
    Tcl_Obj** item = &items[0];
    for (int colNo = 1; colNo <= numCols; colNo++) {
        *item++ = getColumnInfo(interp, colNo);
    }
    return Tcl_NewListObj(item - items, items);
}

int SdbStmt::getRowData(Tcl_Interp* interp, Tcl_Obj* rowVar, Tcl_Obj* nullVar, bool returnAsArray)
{
    Tcl_Obj* data[returnAsArray ? 0 : cols.size()];
    Tcl_Obj* nulls[returnAsArray ? 0 : cols.size()];

    Tcl_Obj** dataItem = data;
    Tcl_Obj** nullItem = nulls;

    Tcl_Obj* tclTrue  = (nullVar ? Tcl_NewBooleanObj(1) : nullptr);
    Tcl_Obj* tclFalse = (nullVar ? Tcl_NewBooleanObj(0) : nullptr);
    Tcl_Obj* tclNull  = Tcl_NewObj();

    int colNo = 1;
    for (auto it = cols.cbegin(); it != cols.cend(); ++it, ++colNo) {
        union {
            double        d;
            Tcl_WideInt   w;
            int           i;
            char          c[TCL_UTF_MAX * 4000];
            unsigned char b[8000];
            SQLDBC_LOB    h;
        } val;
        SQLDBC_Length  len;
        SQLDBC_Retcode rc = rset->getObject(colNo, it->hostType, &val, &len, sizeof(val), false);
        if (rc == SQLDBC_NOT_OK) {
            setTclError(interp, rset->error());
            goto Error_Exit;
        }

        Tcl_Obj* colData;
        Tcl_Obj* isNull;

        if (len == SQLDBC_NULL_DATA) {
            colData = tclNull;
            isNull  = tclTrue;
        } else {
            switch (it->hostType) {
                case SQLDBC_HOSTTYPE_BLOB:
                case SQLDBC_HOSTTYPE_UTF8_CLOB: colData = Tcl_NewSdbLobObj(new SdbLob(val.h, it->hostType, *this)); break;
                case SQLDBC_HOSTTYPE_INT4:      colData = Tcl_NewIntObj(val.i); break;
                case SQLDBC_HOSTTYPE_INT8:      colData = Tcl_NewWideIntObj(val.w); break;
                case SQLDBC_HOSTTYPE_DOUBLE:    colData = Tcl_NewDoubleObj(val.d); break;
                case SQLDBC_HOSTTYPE_BINARY:    colData = Tcl_NewByteArrayObj(val.b, len); break;
                default:                        colData = Tcl_NewStringObj(val.c, len);
            }
            isNull = tclFalse;
        }
        if (returnAsArray) {
            if (Tcl_ObjSetVar2(interp, rowVar, it->label, colData, TCL_LEAVE_ERR_MSG) == NULL) {
                return TCL_ERROR;
            }
            if (nullVar != nullptr && Tcl_ObjSetVar2(interp, nullVar, it->label, isNull, TCL_LEAVE_ERR_MSG) == NULL) {
                return TCL_ERROR;
            }
        } else {
            Tcl_IncrRefCount(*dataItem++ = colData);
            if (isNull) {
                Tcl_IncrRefCount(*nullItem++ = isNull);
            }
        }
    }
    if (!returnAsArray) {
        if (Tcl_ObjSetVar2(interp, rowVar, nullptr, Tcl_NewListObj(dataItem - data, data), TCL_LEAVE_ERR_MSG) == NULL) {
            goto Error_Exit;
        }
        if (nullVar != nullptr && Tcl_ObjSetVar2(interp, nullVar, nullptr, Tcl_NewListObj(nullItem - nulls, nulls), TCL_LEAVE_ERR_MSG) == NULL) {
            goto Error_Exit;
        }
    }

    return TCL_OK;

Error_Exit:
    for (Tcl_Obj** objPtr = data; objPtr < dataItem; ++objPtr) {
        Tcl_DecrRefCount(*objPtr);
    }
    for (Tcl_Obj** objPtr = nulls; objPtr < nullItem; ++objPtr) {
        Tcl_DecrRefCount(*objPtr);
    }
    return TCL_ERROR;
}

// ------------------------------------------------------------------------------------------------

Param::Param(SQLDBC_ParameterMetaData* info, int paramNo) : name(nullptr), outVarName(nullptr)
{
    sqlType    = info->getParameterType(paramNo);
    length     = info->getParameterLength(paramNo);
    precision  = info->getPrecision(paramNo);
    scale      = info->getScale(paramNo);
    byteLength = info->getPhysicalLength(paramNo);
    inOutMode  = info->getParameterMode(paramNo);

    switch (sqlType) {
        case SQLDBC_SQLTYPE_FIXED:
        case SQLDBC_SQLTYPE_FLOAT:
        case SQLDBC_SQLTYPE_VFLOAT:
            if (precision > 15)
                hostType = SQLDBC_HOSTTYPE_UTF8;
            else if (scale > 0)
                hostType = SQLDBC_HOSTTYPE_DOUBLE;
            else if (precision > 9)
                hostType = SQLDBC_HOSTTYPE_INT8;
            else
                hostType = SQLDBC_HOSTTYPE_INT4;
            break;
        case SQLDBC_SQLTYPE_BOOLEAN:
        case SQLDBC_SQLTYPE_SMALLINT:
        case SQLDBC_SQLTYPE_INTEGER:  hostType = SQLDBC_HOSTTYPE_INT4; break;
        case SQLDBC_SQLTYPE_CHB:
        case SQLDBC_SQLTYPE_VARCHARB:
        case SQLDBC_SQLTYPE_STRB:
        case SQLDBC_SQLTYPE_LONGB:    hostType = SQLDBC_HOSTTYPE_BINARY; break;
        default:                      hostType = SQLDBC_HOSTTYPE_UTF8;
    }

    dataLength        = 0;
    outData.charValue = nullptr;

    if (isOut()) {
        switch (hostType) {
            case SQLDBC_HOSTTYPE_INT4:   byteLength = sizeof(int); break;
            case SQLDBC_HOSTTYPE_INT8:   byteLength = sizeof(Tcl_WideInt); break;
            case SQLDBC_HOSTTYPE_DOUBLE: byteLength = sizeof(double); break;
            case SQLDBC_HOSTTYPE_BINARY: {
                outData.charValue = Tcl_Alloc(byteLength);
                break;
            }
            default: {
                byteLength        = length * TCL_UTF_MAX;
                outData.charValue = Tcl_Alloc(byteLength + 1);
            }
        }
    }
}

Param::~Param()
{
    if (name) Tcl_DecrRefCount(name);
    if (isVarChar() && outData.charValue != nullptr) Tcl_Free(outData.charValue);
}

bool Param::checkAndSetNull(Tcl_Obj* arg)
{
    if (arg->typePtr == nullptr || arg->typePtr == tclStringType) {
        int strLen;
        Tcl_GetStringFromObj(arg, &strLen);
        if (strLen == 0) {
            dataLength = SQLDBC_NULL_DATA;
            return true;
        }
    }
    return false;
}

int Param::copyIntoOutDataBuffer(Tcl_Interp* interp, Tcl_Obj* arg, int idx)
{
    if (checkAndSetNull(arg)) {
        return TCL_OK;
    }

    switch (hostType) {
        case SQLDBC_HOSTTYPE_INT4:
            if (Tcl_GetIntFromObj(interp, arg, &outData.intValue) != TCL_OK) return TCL_ERROR;
            dataLength = sizeof(int);
            break;
        case SQLDBC_HOSTTYPE_INT8:
            if (Tcl_GetWideIntFromObj(interp, arg, &outData.wideIntValue) != TCL_OK) return TCL_ERROR;
            dataLength = sizeof(Tcl_WideInt);
            break;
        case SQLDBC_HOSTTYPE_DOUBLE:
            if (Tcl_GetDoubleFromObj(interp, arg, &outData.doubleValue) != TCL_OK) return TCL_ERROR;
            dataLength = sizeof(double);
            break;
        case SQLDBC_HOSTTYPE_BINARY: {
            int   len;
            void* data = Tcl_GetByteArrayFromObj(arg, &len);
            if (len > byteLength) {
                char posStr[8], lenStr[12], byteLenStr[8];
                snprintf(posStr, sizeof(posStr), "%u", idx);
                snprintf(lenStr, sizeof(lenStr), "%u", len);
                snprintf(byteLenStr, sizeof(byteLenStr), "%u", byteLength);
                Tcl_AppendResult(interp, "argument ", posStr, "[", lenStr, "] is longer than max paramerer length of ", byteLength, nullptr);
                return TCL_ERROR;
            }
            dataLength = len;
            std::memcpy(outData.charValue, data, len);
            break;
        }
        default: {
            int   len;
            void* data = Tcl_GetStringFromObj(arg, &len);
            if (len > byteLength) {
                char posStr[8], lenStr[12], byteLenStr[8];
                snprintf(posStr, sizeof(posStr), "%u", idx);
                snprintf(lenStr, sizeof(lenStr), "%u", len);
                snprintf(byteLenStr, sizeof(byteLenStr), "%u", byteLength);
                Tcl_AppendResult(interp, "argument ", posStr, "[", lenStr, "] is longer than max paramerer length of ", byteLength, nullptr);
                return TCL_ERROR;
            }
            dataLength = len;
            std::memcpy(outData.charValue, data, len);
        }
    }
    return TCL_OK;
}

void Param::bindOutDataBufferTo(SQLDBC_PreparedStatement* stmt, int idx)
{
    stmt->bindParameter(idx, hostType, &outData, &dataLength, byteLength, false);
}

int Param::bindInTo(SQLDBC_PreparedStatement* stmt, int idx, Tcl_Interp* interp, Tcl_Obj* arg)
{
    void* data = nullptr;
    if (!checkAndSetNull(arg)) {
        switch (hostType) {
            case SQLDBC_HOSTTYPE_INT4: {
                if (Tcl_GetIntFromObj(interp, arg, &outData.intValue) != TCL_OK) return TCL_ERROR;
                data       = &outData.intValue;
                dataLength = sizeof(int);
                break;
            }
            case SQLDBC_HOSTTYPE_INT8:
                if (Tcl_GetWideIntFromObj(interp, arg, &outData.wideIntValue) != TCL_OK) return TCL_ERROR;
                data       = &outData.wideIntValue;
                dataLength = sizeof(Tcl_WideInt);
                break;
            case SQLDBC_HOSTTYPE_DOUBLE:
                if (Tcl_GetDoubleFromObj(interp, arg, &outData.doubleValue) != TCL_OK) return TCL_ERROR;
                data       = &outData.doubleValue;
                dataLength = sizeof(double);
                break;
            case SQLDBC_HOSTTYPE_BINARY: {
                int len;
                data       = Tcl_GetByteArrayFromObj(arg, &len);
                dataLength = len;
                break;
            }
            default: {
                int len;
                data       = Tcl_GetStringFromObj(arg, &len);
                dataLength = len;
            }
        }
    }
    stmt->bindParameter(idx, hostType, data, &dataLength, dataLength, false);
    return TCL_OK;
}

Tcl_Obj* Param::getOutObj()
{
    switch (hostType) {
        case SQLDBC_HOSTTYPE_INT4:   return Tcl_NewIntObj(outData.intValue);
        case SQLDBC_HOSTTYPE_INT8:   return Tcl_NewWideIntObj(outData.wideIntValue);
        case SQLDBC_HOSTTYPE_DOUBLE: return Tcl_NewDoubleObj(outData.doubleValue);
        case SQLDBC_HOSTTYPE_BINARY: return Tcl_NewByteArrayObj((unsigned char*) outData.charValue, dataLength);
        default:                     return Tcl_NewStringObj(outData.charValue, dataLength);
    }
}

SdbPrepStmt::SdbPrepStmt(SdbConn* conn) : SdbStmt(conn, 0)
{
    stmt = conn->createPreparedStatement();
}

void SdbPrepStmt::releaseDatabaseHandles()
{
    if (conn) {
        if (stmt) {
            if (rset) {
                rset     = nullptr;
                rsetInfo = nullptr;
                cols.clear();
            }
            stmt->getConnection()->releaseStatement((SQLDBC_PreparedStatement*) stmt);
            stmt = nullptr;
        }
        conn = nullptr;
    }
}

int SdbPrepStmt::prepare(Tcl_Interp* interp, Tcl_Obj* sqlObj)
{
    int         sqlLen;
    const char* sql = Tcl_GetStringFromObj(sqlObj, &sqlLen);
    if (prepstmt()->prepare(sql, sqlLen, SQLDBC_StringEncoding::UTF8) != SQLDBC_OK) {
        setTclError(interp, stmt->error());
        return TCL_ERROR;
    }

    SQLDBC_ParameterMetaData* paramsInfo = prepstmt()->getParameterMetaData();

    int paramCount = paramsInfo->getParameterCount();
    params.clear();
    params.reserve(paramCount);
    for (int paramNo = 1; paramNo <= paramCount; paramNo++) {
        params.emplace_back(paramsInfo, paramNo);
    }

    if (paramCount > 0) {
        static Tcl_Obj* paramFindPattern = Tcl_NewStringObj(":\\w+|\\?", 7);
        Tcl_RegExp      re               = Tcl_GetRegExpFromObj(interp, paramFindPattern, TCL_REG_ADVANCED);
        enum { NONE, POS, NAMED, BADMIX };
        int typeMix = NONE;
        int paramNo = 0;
        int res     = Tcl_RegExpExec(interp, re, sql, sql);
        while (res > 0) {
            if (paramNo >= paramCount) {
                TclSetResult(interp, "sdbtcl SQL scanner found more parameters than the database reported", TCL_STATIC);
                return TCL_ERROR;
            }
            const char* start;
            const char* end;
            Tcl_RegExpRange(re, 0, &start, &end);
            if (*start == '?') {
                typeMix |= POS;
                ++paramNo;
            } else {
                typeMix |= NAMED;
                Param& param = params.at(paramNo++);
                param.name   = Tcl_NewStringObj(start, end - start);
            }
            res = Tcl_RegExpExec(interp, re, end, sql);
        }
        if (paramNo < paramCount) {
            TclSetResult(interp, "sdbtcl SQL scanner has not found all the parameters that the database reported", TCL_STATIC);
            return TCL_ERROR;
        }
        if (typeMix == BADMIX) {
            TclSetResult(interp, "sdbtcl does not support mixing named and positional parameters in the same SQL", TCL_STATIC);
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

int SdbPrepStmt::bind(Tcl_Interp* interp, int argc, Tcl_Obj* const argv[])
{
    bool isPositional = params.size() > 0 && params.at(0).name == nullptr;
    if (isPositional && argc != params.size()) {
        char sizeStr[12], argcStr[12];
        snprintf(sizeStr, sizeof(sizeStr), "%zu", params.size());
        snprintf(argcStr, sizeof(argcStr), "%i", argc);
        Tcl_AppendResult(interp, sizeStr, " arguments are expected, ", argcStr, " were provided", nullptr);
        return TCL_ERROR;
    }
    if (!isPositional && argc != params.size() * 2) {
        char sizeStr[12], argcStr[12];
        snprintf(sizeStr, sizeof(sizeStr), "%zu", params.size());
        snprintf(argcStr, sizeof(argcStr), "%i", argc / 2);
        Tcl_AppendResult(interp, sizeStr, " named arguments are expected, ", argcStr, " were provided", nullptr);
        return TCL_ERROR;
    }

    int bindIdx = 0;
    for (int i = 0; i < argc;) {
        Param* param;
        if (isPositional) {
            param = &params.at(bindIdx++);
        } else if (argv[i]->typePtr == nullptr || argv[i]->typePtr == tclStringType) {
            int         len;
            const char* name = Tcl_GetStringFromObj(argv[i++], &len);

            int idx = 0;
            for (auto it = params.begin(); it != params.end(); ++it, ++idx) {
                int         paramNameLen;
                const char* paramName = Tcl_GetStringFromObj(it->name, &paramNameLen);
                if (paramNameLen == len && strncasecmp(paramName, name, len) == 0) {
                    break;
                }
            }
            if (idx == params.size()) {
                Tcl_AppendResult(interp, "cannot find parameter ", name, " in the statement", nullptr);
                return TCL_ERROR;
            }
            param   = &params.at(idx);
            bindIdx = idx + 1;
        } else {
            Tcl_AppendResult(interp, "cannot use ", Tcl_GetString(argv[i]), " as a parameter name", nullptr);
            return TCL_ERROR;
        }
        Tcl_Obj* arg = argv[i++];

        if (param->isOut()) {
            if (param->isIn()) {
                Tcl_Obj* val = Tcl_ObjGetVar2(interp, arg, nullptr, TCL_LEAVE_ERR_MSG);
                if (val == nullptr) {
                    return TCL_ERROR;
                }
                if (param->copyIntoOutDataBuffer(interp, val, bindIdx) != TCL_OK) {
                    return TCL_ERROR;
                }
            }
            param->bindOutDataBufferTo(prepstmt(), bindIdx);
            param->outVarName = arg;
        } else {
            if (param->bindInTo(prepstmt(), bindIdx, interp, arg) != TCL_OK) {
                return TCL_ERROR;
            }
            param->outVarName = nullptr;
        }
    }
    return TCL_OK;
}

int SdbPrepStmt::copyOutput(Tcl_Interp* interp)
{
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (it->outVarName == nullptr) continue;
        Tcl_Obj* output = it->getOutObj();
        if (Tcl_ObjSetVar2(interp, it->outVarName, nullptr, output, TCL_LEAVE_ERR_MSG) == nullptr) {
            Tcl_DecrRefCount(output);
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

int SdbPrepStmt::execute(Tcl_Interp* interp, int idx, int objc, Tcl_Obj* const objv[], ResultSetConfig& config)
{
    clearResults();

    if (configure(interp, config) != TCL_OK) {
        return TCL_ERROR;
    }

    if (bind(interp, objc - idx, objv + idx) != TCL_OK) {
        return TCL_ERROR;
    }

    if (prepstmt()->execute() != SQLDBC_OK) {
        setTclError(interp, stmt->error());
        return TCL_ERROR;
    }

    if (copyOutput(interp) != TCL_OK) {
        return TCL_ERROR;
    }

    return setExecuteResult(interp);
}

// ------------------------------------------------------------------------------------------------

static void freeIntRep (Tcl_Obj* obj)
{
    SdbStmt* stmt = (SdbStmt*) obj->internalRep.otherValuePtr;
    stmt->release();
    obj->internalRep.otherValuePtr = nullptr;
}

static void dupIntRep (Tcl_Obj* src, Tcl_Obj* dst)
{
    SdbStmt* stmt = (SdbStmt*) (dst->internalRep.otherValuePtr = src->internalRep.otherValuePtr);
    if (stmt) {
        stmt->preserve();
    }
}

Tcl_ObjType sdbStmtType = {
    .name           = (char*) "sdbstmt",
    .freeIntRepProc = freeIntRep,
    .dupIntRepProc  = dupIntRep,
};

Tcl_ObjType sdbPrepStmtType = {
    .name           = (char*) "sdbprepstmt",
    .freeIntRepProc = freeIntRep,
    .dupIntRepProc  = dupIntRep,
};

Tcl_Obj* Tcl_NewSdbStmtObj (SdbStmt* stmt)
{
    Tcl_Obj* obj = Tcl_NewObj();
    obj->typePtr = &sdbStmtType;

    obj->internalRep.otherValuePtr = stmt;
    stmt->preserve();
    return obj;
}

Tcl_Obj* Tcl_NewSdbPrepStmtObj (SdbStmt* stmt)
{
    Tcl_Obj* obj = Tcl_NewObj();
    obj->typePtr = &sdbPrepStmtType;

    obj->internalRep.otherValuePtr = stmt;
    stmt->preserve();
    return obj;
}

int Tcl_GetSdbStmtFromObj (Tcl_Obj* obj, SdbStmt** stmtPtr)
{
    if (obj->typePtr != &sdbStmtType && obj->typePtr != &sdbPrepStmtType) {
        return TCL_ERROR;
    }
    *stmtPtr = (SdbStmt*) obj->internalRep.otherValuePtr;
    return TCL_OK;
}

// ------------------------------------------------------------------------------------------------

int SdbStmt_New (SdbConn* sdbconn, Tcl_Interp* interp, int argc, Tcl_Obj* const argv[])
{
    auto stmt = std::make_unique<SdbStmt>(sdbconn);

    int i = 0;

    ResultSetConfig rsetConfig;
    if (rsetConfig.init(interp, &i, argc, argv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (i > 0 && stmt->configure(interp, rsetConfig) != TCL_OK) {
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewSdbStmtObj(stmt.release()));
    return TCL_OK;
}

int SdbPrepStmt_New (SdbConn* sdbconn, Tcl_Interp* interp, int argc, Tcl_Obj* const argv[])
{
    auto stmt = std::make_unique<SdbPrepStmt>(sdbconn);

    int i = 0;

    ResultSetConfig rsetConfig;
    if (rsetConfig.init(interp, &i, argc, argv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (i > 0 && stmt->configure(interp, rsetConfig) != TCL_OK) {
        return TCL_ERROR;
    }
    if (stmt->prepare(interp, argv[i]) != TCL_OK) {
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewSdbStmtObj(stmt.release()));
    return TCL_OK;
}
