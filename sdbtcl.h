#pragma once

#ifdef _WIN32
#include <windows.h>
#define strncasecmp _strnicmp
#endif

#include <tcl.h>

const extern Tcl_ObjType* tclByteArrayType;
const extern Tcl_ObjType* tclDoubleType;
const extern Tcl_ObjType* tclWideIntType;
const extern Tcl_ObjType* tclIntType;
const extern Tcl_ObjType* tclStringType;
const extern Tcl_ObjType* tclIndexType;

#include <SQLDBC.h>

using namespace SQLDBC;

#include "sdbutil.h"

/**
 * Clone of TCL internal function that cleans up object's internal
 * representation.
 */
static inline void TclFreeIntRep (Tcl_Obj* objPtr)
{
    if (objPtr->typePtr != NULL && objPtr->typePtr->freeIntRepProc != NULL) {
        objPtr->typePtr->freeIntRepProc(objPtr);
    }
}

/**
 * Alternative version of Tcl_SetResult that uses const char *
 */
static inline void TclSetResult (Tcl_Interp* interp, const char* result, Tcl_FreeProc* freeProc)
{
    Tcl_SetResult(interp, (char*) result, freeProc);
}

class SdbEnv {
    SQLDBC_Environment env;
    Tcl_Interp*        interp;
    int                refCount;

public:
    SdbEnv(SQLDBC_IRuntime* runtime, Tcl_Interp* interp) : env(runtime), interp(interp), refCount(1) {}

    void preserve () { ++refCount; }
    void release ()
    {
        if (--refCount <= 0) {
            delete this;
        }
    }

    SQLDBC_Connection* createConnection () { return env.createConnection(); }

    void releaseConnection (SQLDBC_Connection* conn) { env.releaseConnection(conn); }

    /**
     * Returns the version of used SQLDBC runtime.
     */
    int version (Tcl_Interp* interp);

    /**
     * Establishes a database connection and creates a new TCL command to manipulate that connection.
     *
     * Example:
     *
     * ```tcl
     * sdb connect db -host localhost -database MAXDB -user MONA -password RED
     * ```
     *
     * or, if user mona is added to XUSER as mona,
     *
     * ```tcl
     * sdb connect db -key mona
     * ```
     *
     * See https://maxdb.sap.com/documentation/sqldbc/SQLDBC_API/SQLDBC__C_8h.html#a28 for a list of other acceptable
     * connection options.
     */
    int connect (Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
};
