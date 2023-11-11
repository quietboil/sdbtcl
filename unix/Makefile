MAKE_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
ifeq ($(abspath $(MAKE_DIR)), $(CURDIR))
ROOT := ..
else
ROOT := $(patsubst %/,%,$(dir $(MAKE_DIR)))
endif

include $(MAKE_DIR)/local.mk

LIB_SEARCH_PATH = $(patsubst SEARCH_DIR("=%");,%,$(shell ld --verbose|grep SEARCH_DIR))
TCL_CONFIG_LIST = $(foreach dir,$(LIB_SEARCH_PATH),$(wildcard $(dir)/tclConfig.sh))
TCL_CONFIG := $(firstword $(TCL_CONFIG_LIST))
ifeq ($(TCL_CONFIG),)
$(error tclConfig.sh was not found on this system)
endif
include $(TCL_CONFIG)

TCL_INCLUDE_SPEC := $(TCL_INCLUDE_SPEC:'%'=%)
TCL_LIB_SPEC     := $(TCL_LIB_SPEC:'%'=%)
TCL_SHLIB_CFLAGS := $(TCL_SHLIB_CFLAGS:'%'=%)

CFLAGS  := $(TCL_SHLIB_CFLAGS) $(TCL_INCLUDE_SPEC) -I$(MAXDB_SDK)/incl -Os -Werror
LDFLAGS := $(TCL_LIB_SPEC) -L$(MAXDB_SDK)/lib -lSQLDBC

SRCS := $(wildcard $(ROOT)/*.cc)
OBJS := $(patsubst $(ROOT)/%.cc,$(MAKE_DIR)/%.o,$(SRCS))
DEPS := $(patsubst %.o,%.d,$(OBJS))

SDBTCL := $(ROOT)/sdbtcl.so

all: $(SDBTCL)

$(MAKE_DIR)/%.o: $(ROOT)/%.cc
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

include $(DEPS)	

$(DEPS): ;

$(SDBTCL): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

test: export TCLLIBPATH := $(ROOT)

test: $(SDBTCL)
	@tclsh $(ROOT)/test.tcl -color $(TEST_ARGS)

clean:
	rm -f $(MAKE_DIR)/*.o
	rm -f $(MAKE_DIR)/*.d
	rm -f $(SDBTCL)
