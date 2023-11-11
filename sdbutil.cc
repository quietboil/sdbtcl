#include "sdbutil.h"
#include <cctype>
#include <cstring>

static Tcl_Obj* tclStrings[NUM_TCL_LIT_STRINGS];

void strtoupper (const char* str, char* buff, int size)
{
    char* dst = buff;
    while (--size > 0) {
        *dst = toupper(*str++);
        if (*dst == '\0') {
            break;
        }
        dst++;
    }
    if (size == 0) {
        *dst = '\0';
    }
}

void setTclError (Tcl_Interp* interp, SQLDBC_ErrorHndl& error)
{
    TclSetResult(interp, error.getErrorText(), TCL_VOLATILE);
    char errorCode[16];
    sprintf(errorCode, "%d", error.getErrorCode());
    Tcl_SetErrorCode(interp, errorCode, NULL);
}

Tcl_Obj* getTclString (TclLit lit, const char* value)
{
    Tcl_Obj** strPtr = &tclStrings[lit];
    if (*strPtr == nullptr) {
        Tcl_IncrRefCount(*strPtr = Tcl_NewStringObj(value, -1));
    }
    Tcl_IncrRefCount(*strPtr);
    return *strPtr;
}

void releaseTclStrings ()
{
    Tcl_Obj** strPtr = &tclStrings[0];
    Tcl_Obj** endPtr = &tclStrings[NUM_TCL_LIT_STRINGS];
    while (strPtr < endPtr) {
        if (*strPtr != nullptr) {
            Tcl_DecrRefCount(*strPtr);
            *strPtr = nullptr;
        }
    }
}

int findNamedValue (const char* namedValueType, const NamedValue* namedValue, Tcl_Interp* interp, Tcl_Obj* arg, int* value)
{
    int strLen;

    const char* str = Tcl_GetStringFromObj(arg, &strLen);
    while (namedValue != nullptr) {
        if (strLen == namedValue->length && strncasecmp(str, namedValue->name, strLen) == 0) {
            *value = namedValue->value;
            return TCL_OK;
        }
        ++namedValue;
    }
    Tcl_AppendResult(interp, str, " is not a recognizable ", namedValueType, NULL);
    return TCL_ERROR;
}

Tcl_Obj* findNamedArg (Tcl_Obj* name, int argc, Tcl_Obj* const argv[])
{
    int nameLen;
    const char* nameStr = Tcl_GetStringFromObj(name, &nameLen);
    for (int i = 0; i < argc; ) {
        Tcl_Obj* key = argv[i++];
        Tcl_Obj* val = argv[i++];
        if (key->typePtr == nullptr || key->typePtr == tclStringType) {
            int keyLen;
            const char* keyStr = Tcl_GetStringFromObj(key, &keyLen);
            if (keyLen == nameLen && strncasecmp(keyStr, nameStr, keyLen) == 0) {
                return val;
            }
        }
    }
    return nullptr;
}
