MAKE_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
ifeq ($(abspath $(MAKE_DIR)), $(CURDIR))
ROOT := ..
else
ROOT := $(patsubst %/,%,$(dir $(MAKE_DIR)))
endif

include $(MAKE_DIR)/local.mk

MAXDB_INCLUDE_SPEC = -I $(MAXDB_SDK)/incl
MAXDB_LIB_SPEC = -L $(MAXDB_SDK)/lib -l libSQLDBC

CFLAGS  := $(TCL_INCLUDE_SPEC) $(MAXDB_INCLUDE_SPEC) -Os -Werror
LDFLAGS := $(TCL_LIB_SPEC) $(MAXDB_LIB_SPEC)

SRCS := $(wildcard $(ROOT)/*.cc)
OBJS := $(patsubst $(ROOT)/%.cc,$(MAKE_DIR)/%.o,$(SRCS))
DEPS := $(patsubst %.o,%.d,$(OBJS))

SDBTCL := $(ROOT)/sdbtcl.dll

all: $(SDBTCL)

$(MAKE_DIR)/%.o: $(ROOT)/%.cc
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

include $(DEPS)

$(DEPS): ;

$(SDBTCL): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

test: export TCLLIBPATH := $(ROOT)

test: $(SDBTCL)
	@$(TCLSH) $(ROOT)/test.tcl -color $(TEST_ARGS)

clean:
	rm -f $(MAKE_DIR)/*.o
	rm -f $(MAKE_DIR)/*.d
	rm -f $(SDBTCL)
	rm -f $(patsubst %.dll,%.lib,$(SDBTCL))
	rm -f $(patsubst %.dll,%.exp,$(SDBTCL))
