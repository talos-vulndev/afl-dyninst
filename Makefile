# EDIT: path to  dyninst binaries
DYNINST_ROOT = /usr/local

# EDIT: you must set this to your dyninst build directory if you build with v10
DYNINST_BUILD = /path/to/dyninst/build

# better dont touch these
DYNINST9=-lcommon -liberty -lboost_system
DYNINST10=-I$(DYNINST_BUILD)/tbb/src/TBB/src/include -lboost_system -L$(DYNINST_BUILD)/tbb/lib -ltbb -Wl,-rpath $(DYNINST_BUILD)/tbb/lib

# EDIT: set this to either DYNINST9 or DYNINST10 depending on what you installed
DYNINST_OPT = $(DYNINST9)

# path to afl src 
AFL_ROOT = ./afl 

# path to libelf and libdwarf
DEPS_ROOT = /usr/local

# where to install
INSTALL_ROOT = /usr/local

CXX = g++
CXXFLAGS = -Wall -O3 -std=c++11 -g -O3 -std=c++11 
LIBFLAGS = -fpic -shared

CC = gcc
CFLAGS = -Wall -pedantic -g -std=gnu99

all: afl-dyninst libAflDyninst.so

afl-dyninst:	afl-dyninst.o
	$(CXX) $(CXXFLAGS) -L$(DYNINST_ROOT)/lib \
		-L$(DEPS_ROOT)/lib \
		-o afl-dyninst afl-dyninst.o \
		$(DYNINST_OPT) \
		-ldyninstAPI

libAflDyninst.so: libAflDyninst.cpp
	$(CXX) -O3 -std=c++11 $(LIBFLAGS) -I$(AFL_ROOT) -I$(DEPS_ROOT)/include libAflDyninst.cpp -o libAflDyninst.so

afl-dyninst.o: afl-dyninst.cpp
	$(CXX) $(CXXFLAGS) $(DYNINST_OPT) -I$(DEPS_ROOT)/include -I$(DYNINST_ROOT)/include  -c afl-dyninst.cpp

clean:
	rm -f afl-dyninst *.so *.o 

install: all
	install -d $(INSTALL_ROOT)/bin
	install -d $(INSTALL_ROOT)/lib
	install afl-dyninst afl-dyninst.sh afl-fuzz-dyninst.sh $(INSTALL_ROOT)/bin
	install libAflDyninst.so $(INSTALL_ROOT)/lib	
