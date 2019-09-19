# EDIT: path to  dyninst binaries
DYNINST_ROOT = /usr/local

# EDIT: you must set this to your dyninst build directory
DYNINST_BUILD = /path/to/dyninst/build-directory
#DYNINST_BUILD = /prg/tmp/dyninst/build-10.0.1

# EDIT: path to afl src if you do not set a symlink from ./afl to the afl directory
AFL_ROOT = ./afl 

# better dont touch these
DYNINST9=-lcommon -liberty -lboost_system
DYNINST10=-I$(DYNINST_BUILD)/tbb/src/TBB/src/include -lboost_system -ltbb

# EDIT: set this to either DYNINST9 or DYNINST10 depending on what you installed
DYNINST_OPT = $(DYNINST10)

# path to libelf and libdwarf
DEPS_ROOT = /usr/local

# where to install
INSTALL_ROOT = /usr/local

#CXX = g++
CXXFLAGS = -Wall -O3 -std=c++11 -g -O3 -std=c++11 
LIBFLAGS = -fpic -shared

CC = gcc
CFLAGS = -Wall -O3 -g -std=gnu99

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
