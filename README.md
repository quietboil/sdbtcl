# Sdbtcl

Sdbtcl is an extension to the Tcl language that provides access to an SAP MaxDB database server.

## Example

```tcl
package require sdbtcl

sdb connect db -host localhost -database MAXDB -user MONA -password RED -chopblanks 1
db execute "SET CURRENT_SCHEMA=hotel"
set stmt [db prepare "
    SELECT h.name  AS hotel_name
         , r.type  AS room_type
         , r.free  AS num_available_rooms
         , r.price AS room_price
      FROM hotel.room r
      JOIN hotel.hotel h
        ON h.hno = r.hno
     WHERE h.zip = :ZIP
       AND r.price <= :MAX_PRICE
       AND r.free >= :MIN_FREE
     ORDER BY price
"]
set numRows [db execute $stmt :ZIP "60601" :MAX_PRICE 150 :MIN_FREE 50]
# $numRows == 2
while {[db fetch -asarray $stmt row]} {
    puts "hotel name: $row(HOTEL_NAME)"
    puts " room type: $row(ROOM_TYPE)"
    puts "rooms free: $row(NUM_AVAILABLE_ROOMS)"
    puts "room price: $row(ROOM_PRICE)"
}
db disconnect
```

## Building

Sdbtcl provides Makefiles to build it from sources for Windows and Linux.

### Prerequisites

Before Sdbtcl can be built, a `local.mk` file must be creaated (in the same directory
where the `Makefile` is) with the description of the local build environment - where
MaxDB SDK can be found, where the `sqlcli` executable is located (as it is used in Sdbtcl
tests for validations), and on Windows where Tcl include files and libraries can be found.

The provided Makefiles allow building Sdbtcl either with gcc or clang. Which one to
use also must be specified in the `local.mk`.

### Windows

Content of a sample `local.mk` is below. Note that specific paths used in it are just
examples and must be changed to match the actual location on the build system.

```make
# Directory where MaxDB SQLDBC SDK can be found:
MAXDB_SDK = C:/Apps/SAP/MaxDB/sdk/sqldbc

TCL = C:/Apps/Tcl
# Path to the tclsh, which is used to tun sdbtcl tests
TCLSH = $(TCL)/bin/tclsh
# How to find <tcl.h>
TCL_INCLUDE_SPEC = -I $(TCL)/include
# Where Tcl libraries are and which one to use to build the extension.
TCL_LIB_SPEC = -L $(TCL)/lib -l libtcl86.dll

# What compiler to use to build the extension.
CC = C:/Apps/LLVM64/bin/clang++

# Where the sqlcli is, which is used in sdbtcl tests.
export SQLCLI := C:/Apps/SAP/bin/sqlcli
```

> ⚠️ On Windows `sdbtcl` was compiled and tested using Thomas Perschak's binary distributions
> of tcl 8.6. Specifically, 8.6.13.5. As it is compiled with gcc, the Tcl library it provides is
> `libtcl86.dll.a`, which clang cannot find due to its use of the WinSDK linker. Rename (or copy)
> `libtcl86.dll.a` to `libtcl86.dll.lib` to make clang on Windows see the SQLDBC library.

To build the extension execute `make`:

```bat
cd win
make
```

### Linux

Becuse `unix` variant of `Makefile` uses `tclConfig.sh` to discover information about the
locally installed Tcl, `local.mk` for Linux is quite a bit shorter:

```make
# Directory where MaxDB SQLDBC SDK can be found:
MAXDB_SDK := /opt/MaxDB/sdk/sqldbc

# May or may not be necessary depending on
# how MaxDB SDK was installed. 
export LD_LIBRARY_PATH := /opt/MaxDB/lib

# Where the sqlcli is, which is used in sdbtcl tests.
export SQLCLI := /opt/MaxDB/bin/sqlcli

# What compiler to use to build the extension.
CC := g++
```

To build the extension execute `make`:

```bash
make
```

### On Testing

Sdbtcl tests assume that the the database server runs on localhost and it has the example (MAXDB)
database where the demo (HOTEL) schema was created. Also, ther database has a demo user MONA with
password RED. If that is not the case - the example database name was changed when it was created,
or there is no user MONA, or that user credentials are different, then you can specify locally
appropriate arguments for the test script in the `local.mk` file:

```make
TEST_ARGS := -host maxdb.example.com -database MAXDB1 -user LISA -password GREEN
```

Also, note that some of the tests check Sdbtcl ability to connect to a database using XUSER stored
credentials. To make them pass create an XUSER data and connection option for the test user using
user name as the XUSER key:

```bash
xuser -U mona -n maxdb.example.com -d MAXDB1 -u MONA,RED
```

The tests can be executed using `make`:

```bash
make test
```

> ⚠️ `try` and `tspec` folders contain packages that are there to support Sdbtcl tests.
> You do not need to copy them when installing Sdbtcl.

## Documentation

Sdbtcl [API manual][1] can be found in the [docs](docs) directory.

[1]: <https://quietboil.github.io/sdbtcl>
