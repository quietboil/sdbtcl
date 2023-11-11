#pragma once

#include "sdbtcl.h"

class SdbStmt;

class SdbLob {
    SQLDBC_LOB lob;
    SdbStmt&   stmt;
    int        refCount;
    struct {
        SQLDBC_HostType lobType : 8;
        bool            isLobOpen : 8;
    };

public:
    SdbLob(SQLDBC_LOB lob, SQLDBC_HostType lobType, SdbStmt& stmt) : stmt(stmt), lob(lob), lobType(lobType), isLobOpen(true), refCount(0) {}
    ~SdbLob()
    {
        if (isLobOpen) lob.close();
    }

    void preserve () { ++refCount; }
    void release ()
    {
        if (--refCount <= 0) {
            delete this;
        }
    }

    bool isOpen () { return isLobOpen; }

    /**
     * Retrieves the length of this LOB in the database. The length is returned in chars.
     */
    Tcl_Obj* getLength () { return Tcl_NewWideIntObj(lob.getLength()); }

    /**
     * Get the current read/write position (in characters).
     *
     * The read/write position starts with 1.
     * If there is no position available, 0 is returned.
     */
    Tcl_Obj* getPosition () { return Tcl_NewWideIntObj(lob.getPosition()); }

    /**
     * Retrieves the optimal size of data for reading or writing (the maximum size
     * that can be transferred with one call to the database server).
     */
    Tcl_Obj* getOptimalSize () { return Tcl_NewWideIntObj(lob.getPreferredDataSize()); }

    /**
     * Write the data into the LOB at the current position.
     */
    int write (Tcl_Interp* interp, Tcl_Obj* obj);

    /**
     * Retrieves the (poosibly partial) content of the LOB.
     *
     * After the operation, the internal position is the start position
     * plus the number of bytes/characters that have been read.
     */
    int read (Tcl_Interp* interp, SQLDBC_Length position, int length, Tcl_Obj** objPtr);

    /**
     * Closes the LOB. No further actions can take place.
     */
    int close (Tcl_Interp* interp);
};

extern Tcl_ObjType sdbLobType;

/**
 * Creates new Tcl object that points to the sdb LOB.
 */
Tcl_Obj* Tcl_NewSdbLobObj (SdbLob* lob);

/**
 * Reads Tcl object that holds SdbLob.
 * Returns TCL_ERRROR (and sets statement pointer to NULL) if the object
 * does not hold SdbLob.
 */
int Tcl_GetSdbLobFromObj (Tcl_Interp* interp, Tcl_Obj* obj, SdbLob** lobPtr);
