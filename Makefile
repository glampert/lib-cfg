
BIN_TARGET = test

SRC_FILES = cfg_utils.cpp cfg_cmd.cpp cfg_cvar.cpp main.cpp
OBJ_FILES = $(patsubst %.cpp, %.o, $(SRC_FILES))

CXX = clang++

# "Paranoid" set of warnings for Clang:
CXXFLAGS = \
	-std=c++11 \
	-fno-exceptions \
	-fno-rtti \
	-fstrict-aliasing \
	-pedantic \
	-Wall \
	-Wextra \
	-Weffc++ \
	-Winit-self \
	-Wformat=2 \
	-Wstrict-aliasing \
	-Wuninitialized \
	-Wunused \
	-Wswitch \
	-Wswitch-default \
	-Wpointer-arith \
	-Wwrite-strings \
	-Wmissing-braces \
	-Wparentheses \
	-Wsequence-point \
	-Wreturn-type \
	-Wunknown-pragmas \
	-Wshadow \
	-Wdisabled-optimization \
	-Wgcc-compat \
	-Wheader-guard \
	-Waddress-of-array-temporary \
	-Wglobal-constructors \
	-Wexit-time-destructors \
	-Wheader-hygiene \
	-Woverloaded-virtual \
	-Wself-assign \
	-Wweak-vtables \
	-Wweak-template-vtables \
	-Wshorten-64-to-32

# Clang static analyzer:
#CXXFLAGS += --analyze -Xanalyzer -analyzer-output=text

#############################

all: $(BIN_TARGET)
	strip $(BIN_TARGET)

$(BIN_TARGET): $(OBJ_FILES)
	$(CXX) $(CXXFLAGS) -o $(BIN_TARGET) $(OBJ_FILES)

$(OBJ_FILES): %.o: %.cpp
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@

clean:
	rm -f $(BIN_TARGET)
	rm -f *.o

