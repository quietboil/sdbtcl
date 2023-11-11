#pragma once

#include "sdbtcl.h"

#define TCL_STR(s) getTclString(s, #s)

/**
 * Predefined strings that are used as constant literals
 */
enum TclLit {
    autocommit,
    isolationlevel,
    sqlmode,
    schema,
    table,
    column,
    label,
    type,
    length,
    precision,
    scale,
    bytelength,
    nullable,
    writable,
    UNKNOWN,
    NUM_TCL_LIT_STRINGS
};

/**
 * An item in an conversion table between textual and numeric
 * representation of values.
 */
struct NamedValue {
    const char * name;
    int          length;
    int          value;
};

/**
 * Looks up a name and returns its value.
 * Returns TCL error if the name cannot be found in the table.
 */
int findNamedValue (const char * namedValueType, const NamedValue * namedValue, Tcl_Interp * interp, Tcl_Obj * arg, int * value);

/**
 * Converts string into upper case.
 */
void strtoupper (const char * str, char * buff, int size);

/**
 * Sets TCL error message using UTF encoded error message returned from SQLDBC error handle.
 */
void setTclError (Tcl_Interp * interp, SQLDBC_ErrorHndl & error);

/**
 * Returns shared TCL string for the given literal
 */
Tcl_Obj * getTclString (TclLit lit, const char * value);

/**
 * Unshares pooled shared TCL strings.
 */
void releaseTclStrings ();

/**
 * Returns true if a Tcl object passed as an argument looks like it might be an option.
 */
static inline bool maybeOption (Tcl_Obj* arg) {
    return (arg->typePtr == nullptr || arg->typePtr == tclStringType || arg->typePtr == tclIndexType) && Tcl_GetString(arg)[0] == '-';
}