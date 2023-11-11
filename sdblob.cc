#include "sdblob.h"
#include "sdbstmt.h"

int SdbLob::write(Tcl_Interp* interp, Tcl_Obj* obj)
{
    int   length;
    void* data;
    if (lobType == SQLDBC_HOSTTYPE_BLOB) {
        data = Tcl_GetByteArrayFromObj(obj, &length);
    } else {
        data = Tcl_GetStringFromObj(obj, &length);
    }
    SQLDBC_Length dataLen = length;
    if (lob.putData(data, &dataLen) == SQLDBC_NOT_OK) {
        TclSetResult(interp, "error writing to LOB", TCL_STATIC);
        return TCL_ERROR;
    }
    return TCL_OK;
}

int SdbLob::read(Tcl_Interp* interp, SQLDBC_Length position, int length, Tcl_Obj** objPtr)
{
    char bytes[lobType == SQLDBC_HOSTTYPE_BLOB ? length : length + 1];
    SQLDBC_Length  bytesRead;
    SQLDBC_Retcode rc = position == 0
        ? lob.getData(bytes, &bytesRead, sizeof(bytes))
        : lob.getData(bytes, &bytesRead, sizeof(bytes), position);
    if (rc == SQLDBC_NOT_OK) {
        TclSetResult(interp, "error reading LOB", TCL_STATIC);
        return TCL_ERROR;
    }
    if (rc == SQLDBC_NO_DATA_FOUND || bytesRead == SQLDBC_NULL_DATA) {
        *objPtr = Tcl_NewObj();
    } else if (lobType == SQLDBC_HOSTTYPE_BLOB) {
        *objPtr = Tcl_NewByteArrayObj((unsigned char*) bytes, bytesRead);
    } else {
        *objPtr = Tcl_NewStringObj(bytes, bytesRead);
    }
    return TCL_OK;
}

int SdbLob::close(Tcl_Interp* interp)
{
    if (lob.close() != SQLDBC_OK) {
        TclSetResult(interp, "error closing LOB", TCL_STATIC);
        return TCL_ERROR;
    }
    isLobOpen = false;

    return TCL_OK;
}

// ------------------------------------------------------------------------------------------------

static void freeIntRep (Tcl_Obj* obj)
{
    SdbLob* lob = (SdbLob*) obj->internalRep.otherValuePtr;
    lob->release();
    obj->internalRep.otherValuePtr = nullptr;
}

static void dupIntRep (Tcl_Obj* src, Tcl_Obj* dst)
{
    SdbLob* lob = (SdbLob*) (dst->internalRep.otherValuePtr = src->internalRep.otherValuePtr);
    if (lob) {
        lob->preserve();
    }
}

Tcl_ObjType sdbLobType = {
    .name           = (char*) "sdblob",
    .freeIntRepProc = freeIntRep,
    .dupIntRepProc  = dupIntRep,
};

Tcl_Obj* Tcl_NewSdbLobObj (SdbLob* lob)
{
    Tcl_Obj* obj = Tcl_NewObj();
    obj->typePtr = &sdbLobType;

    obj->internalRep.otherValuePtr = lob;
    lob->preserve();
    return obj;
}

int Tcl_GetSdbLobFromObj (Tcl_Interp* interp, Tcl_Obj* obj, SdbLob** lobPtr)
{
    if (obj->typePtr != &sdbLobType) {
        Tcl_AppendResult(interp, "sdblob is expected, ", (obj->typePtr ? obj->typePtr->name : "a string"), " was provided", nullptr);
        return TCL_ERROR;
    } else {
        *lobPtr = (SdbLob*) obj->internalRep.otherValuePtr;
        return TCL_OK;
    }
}
