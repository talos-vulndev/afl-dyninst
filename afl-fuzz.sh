#!/bin/bash
test -z "$1" -o "$1" = "-h" && { echo Syntax: $0 afl-fuzz-options ; echo sets the afl-dyninst environment variables ; exit 1 ; }
export AFL_SKIP_BIN_CHECK=1
export DYNINSTAPI_RT_LIB=/usr/local/lib/libdyninstAPI_RT.so
#export AFL_PRELOAD=./desock.so
afl-fuzz $*
