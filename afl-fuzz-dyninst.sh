#!/bin/bash
test -z "$1" -o "$1" = "-h" && { echo Syntax: $0 afl-fuzz-options ; echo sets the afl-dyninst environment variables ; exit 1 ; }
sysctl -w kernel.core_pattern="core" > /dev/null
sysctl -w kernel.randomize_va_space=0 > /dev/null
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null
export AFL_SKIP_BIN_CHECK=1
export DYNINSTAPI_RT_LIB=/usr/local/lib/libdyninstAPI_RT.so
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:.
export AFL_EXIT_WHEN_DONE=1
#export AFL_TMPDIR=/run/$$
#export AFL_PRELOAD=./desock.so:./libdislocator/libdislocator.so
afl-fuzz $*
