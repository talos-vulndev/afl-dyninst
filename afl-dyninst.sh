#!/bin/sh
export DYNINSTAPI_RT_LIB=/usr/local/lib/libdyninstAPI_RT.so
afl-dyninst $*
