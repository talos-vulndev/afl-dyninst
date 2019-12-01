# path to  dyninst binaries
DYNINST_ROOT = /usr/local

# path to afl src 
AFL_ROOT = ./afl 

# path to libelf and libdwarf
DEPS_ROOT = /usr/local

# where to install
INSTALL_ROOT = /usr/local

CXX = g++
CXXFLAGS = -g -Wall -O3 -std=c++11
LIBFLAGS = -fpic -shared

CC = gcc
CFLAGS = -Wall -pedantic -g -std=gnu99


all: afl-dyninst libAflDyninst.so

afl-dyninst: afl-dyninst.o
	$(CXX) $(CXXFLAGS) -L$(DYNINST_ROOT)/lib \
		-L$(DEPS_ROOT)/lib \
		-o afl-dyninst afl-dyninst.o \
		-lcommon \
		-liberty \
		-lboost_system \
		-ldyninstAPI 

libAflDyninst.so: libAflDyninst.cpp
	$(CXX) $(CXXFLAGS) $(LIBFLAGS) -I$(AFL_ROOT) -I$(DEPS_ROOT)/include libAflDyninst.cpp -o libAflDyninst.so

afl-dyninst.o: afl-dyninst.cpp
	$(CXX) $(CXXFLAGS) -I$(DEPS_ROOT)/include -I$(DYNINST_ROOT)/include  -c afl-dyninst.cpp

clean:
	rm -f afl-dyninst *.so *.o 

install: all
	install -d $(INSTALL_ROOT)/bin
	install -d $(INSTALL_ROOT)/lib
	install afl-dyninst afl-dyninst.sh afl-fuzz-dyninst.sh $(INSTALL_ROOT)/bin
	install libAflDyninst.so $(INSTALL_ROOT)/lib	
