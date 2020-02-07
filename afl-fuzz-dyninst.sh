#!/bin/bash
test -z "$1" -o "$1" = "-h" && { echo Syntax: $0 afl-fuzz-options ; echo sets the afl-dyninst environment variables ; exit 1 ; }
#
# remember to run afl-system-config !
#
export AFL_SKIP_BIN_CHECK=1
export DYNINSTAPI_RT_LIB=/usr/local/lib/libdyninstAPI_RT.so
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib:.
echo " $*" | grep -q " -m" && { echo "Warning: no -m memory option specified!" ; sleep 1 ; }
#export AFL_EXIT_WHEN_DONE=1
#export AFL_TMPDIR=/run/$$
#export AFL_PRELOAD=./desock.so:./libdislocator/libdislocator.so
afl-fuzz $*
