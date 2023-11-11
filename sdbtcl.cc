#include "sdbtcl.h"

#include <memory>
#include <cstring>

#include "sdbconn.h"

static_assert(TCL_UTF_MAX == 3, "TCL core built with UCS-2 Tcl_UniChar(s)");

/**
 * Decrements environment reference counter. The environment object would exist,
 * even after sdb command gets deleted, if there is at least one database command
 * that references it.
 */
static void SdbEnv_Release (SdbEnv* sdbenv)
{
    sdbenv->release();
}

int SdbEnv::version(Tcl_Interp* interp)
{
    TclSetResult(interp, env.getLibraryVersion(), TCL_STATIC);
    return TCL_OK;
}

int SdbEnv::connect(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc < 5 || objc % 2 == 0) {
        // sdb connect dbcmd -key mona -chopblanks 1
        Tcl_WrongNumArgs(interp, 2, objv, "cmdname ?-host nodename? ?-database dbname -user username -password password? ?-key xuserkey? ?-option value ...?");
        return TCL_ERROR;
    }

    int cmdNameLen;

    const char* cmdName = Tcl_GetStringFromObj(objv[2], &cmdNameLen);
    if (cmdNameLen == 0) {
        TclSetResult(interp, "database command name is required", TCL_STATIC);
        return TCL_ERROR;
    }
    if (Tcl_GetCommandFromObj(interp, objv[2]) != NULL) {
        Tcl_AppendResult(interp, "command ", cmdName, " already exists", NULL);
        return TCL_ERROR;
    }

    auto conn = std::make_unique<SdbConn>(*this);
    if (conn->connect(interp, objc - 3, objv + 3) != TCL_OK) {
        return TCL_ERROR;
    }
    if (conn->createCommand(interp, cmdName) != TCL_OK) {
        return TCL_ERROR;
    }
    conn.release();

    return TCL_OK;
}

static int Sdb_Cmd (SdbEnv* sdb, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "subcommand ?arg ... ?");
        return TCL_ERROR;
    }

    static const char* subcommands[] = {"connect", "version", NULL};
    enum { CONNECT, VERSION } index;

    if (Tcl_GetIndexFromObj(interp, objv[1], subcommands, "subcommand", 0, (int*) &index) != TCL_OK) {
        return TCL_ERROR;
    }
    if (tclIndexType == nullptr && objv[1]->typePtr != nullptr && strcmp(objv[1]->typePtr->name, "index") == 0) {
        tclIndexType = objv[1]->typePtr;
    }
    switch (index) {
        case CONNECT: return sdb->connect(interp, objc, objv);
        case VERSION: return sdb->version(interp);
    }
    return TCL_OK;
}

const Tcl_ObjType* tclByteArrayType;
const Tcl_ObjType* tclDoubleType;
const Tcl_ObjType* tclWideIntType;
const Tcl_ObjType* tclIntType;
const Tcl_ObjType* tclStringType;
const Tcl_ObjType* tclIndexType = nullptr;

extern "C" DLLEXPORT int Sdbtcl_Init (Tcl_Interp* interp)
{
#ifdef USE_TCL_STUBS
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
        return TCL_ERROR;
    }
#endif
    char errorText[256];

    tclByteArrayType = Tcl_GetObjType("bytearray");
    tclDoubleType    = Tcl_GetObjType("double");
    tclWideIntType   = Tcl_GetObjType("wideInt");
    tclIntType       = Tcl_GetObjType("int");
    tclStringType    = Tcl_GetObjType("string");

    SQLDBC_IRuntime* runtime = GetClientRuntime(errorText, sizeof(errorText));
    if (runtime == nullptr) {
        Tcl_SetResult(interp, errorText, TCL_VOLATILE);
        return TCL_ERROR;
    }
    SdbEnv* sdb = new SdbEnv(runtime, interp);
    if (Tcl_CreateObjCommand(interp, "sdb", (Tcl_ObjCmdProc*) Sdb_Cmd, sdb, (Tcl_CmdDeleteProc*) SdbEnv_Release) == NULL) {
        delete sdb;
        TclSetResult(interp, "cannot create sdb command", TCL_STATIC);
        return TCL_ERROR;
    }
    return Tcl_PkgProvide(interp, "sdbtcl", "1.0");
}
