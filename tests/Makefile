
#------------------------------------------------

#
# Define 'VERBOSE' to get the full console output.
# Otherwise we print a shorter message for each rule.
#VERBOSE = 1
#
ifndef VERBOSE
  QUIET          = @
  ECHO_COMPILING = @echo "-> Building Lib CFG samples ..."
  ECHO_CLEANING  = @echo "-> Cleaning ..."
endif

#------------------------------------------------

INC_DIRS  = -I. -I..
CXXFLAGS += $(INC_DIRS)
CXXFLAGS += -std=c++11 -O3 -Wall -Wextra -Winit-self -Wunused -Wuninitialized -Wshadow -Wformat=2 -Wstrict-aliasing -Wdisabled-optimization

SRC_FILES_TERM_SAMPLE    = ../cfg.cpp native_terminal.cpp
SRC_FILES_CMDCVAR_SAMPLE = ../cfg.cpp cmd_cvar_registration.cpp

# Try to guess the platform for the native_terminal sample.
UNAME = $(shell uname -s)

ifeq ($(UNAME), Darwin)
  CXXFLAGS += -DCFG_BUILD_UNIX_TERMINAL=1
endif
ifeq ($(UNAME), Linux)
  CXXFLAGS += -DCFG_BUILD_UNIX_TERMINAL=1
endif

# Ad hoc Clang static analyzer run:
#CXXFLAGS += --analyze -Xanalyzer -analyzer-output=text

#------------------------------------------------

all:
	$(ECHO_COMPILING)
	$(QUIET) $(CXX) $(CXXFLAGS) $(SRC_FILES_TERM_SAMPLE)    -o cfg_native_terminal
	$(QUIET) $(CXX) $(CXXFLAGS) $(SRC_FILES_CMDCVAR_SAMPLE) -o cfg_cmds_cvars

clean:
	$(ECHO_CLEANING)
	$(QUIET) rm -f cfg_native_terminal
	$(QUIET) rm -f cfg_cmds_cvars

