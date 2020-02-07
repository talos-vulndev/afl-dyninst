# American Fuzzy Lop + Dyninst == AFL Fuzzing blackbox binaries

The tool has two parts. The instrumentation tool and the instrumentation 
library. Instrumentation library has an initialization callback and basic 
block callback functions which are designed to emulate what AFL is doing
with afl-gcc/afl-g++/afl-as. 

Instrumentation tool (afl-dyninst) instruments the supplied binary by
inserting callbacks for each basic block and an initialization 
callback either at _init or at specified entry point.


## Building / Compiling

### docker

simply run
```
docker build -t afl-dyninst .
```
which will take ~25 minutes.
Afterwards you have a containers with afl-dyninst and afl++:
```
docker run -ti afl-dyninst
```

### on your own

1. Clone, compile and install dyninst: https://github.com/dyninst/dyninst/

Note that you could also use dyninst 9.3.2, but has less platform support and
quite a few bugs. For using dyninst 9.x you have to edit the Makefile
Using at least 10.1.0 is highly recommended!

2. Download and install afl++ from https://github.com/vanhauser-thc/AFLplusplus
It's an up to date and enhanced version to the original afl with better
performance, new features and bugfixes.

3. Edit the Makefile and set DYNINST_ROOT and AFL_ROOT to appropriate paths. 

4. make

5. sudo make install


### Building dyninst 10

Building dyninst10 can be a pain.
If you are not on debian-testing or kali-rolling, I recommend the following steps:
1. remove elfutils if installed as a distribution package
2. install libboost-all-dev for your distribution
3. execute (depending where your libboost is installed, for me its /usr/lib/x86_64-linux-gnu):
```shell
cd /usr/lib/x86_64-linux-gnu && for i in libboost*.so libboost*.a; do
  n=`echo $i|sed 's/\./-mt./'`
  ln -s $i $n 2> /dev/null
done
```
4. git clone https://github.com/dyninst/dyninst ; mkdir build ; cd build ; cmake .. ; make ; make install
If dyninst complains about any missing packages - install them.
Depending on the age of your Linux OS you can try to use packages from your distro, and install from source otherwise.


## Commandline options
```
Usage: afl-dyninst -dfvxD -i binary -o  binary -l library -e address -E address -s number -S funcname -I funcname -m size
   -i: input binary program
   -o: output binary program
   -d: do not instrument the binary, only supplied libraries
   -l: linked library to instrument (repeat for more than one)
   -r: runtime library to instrument (path to, repeat for more than one)
   -e: entry point address to patch (required for stripped binaries)
   -E: exit point - force exit(0) at this address (repeat for more than one)
   -s: number of initial basic blocks to skip in binary
   -m: minimum size of a basic bock to instrument (default: 10)
   -f: try to fix a dyninst bug that leads to crashes (loss of 20%% performance)
   -I: only instrument this function and nothing else (repeat for more than one)
   -S: do not instrument this function (repeat for more than one)
   -D: instrument only a simple fork server and also forced exit functions
   -x: experimental performance modes (can be set up to two times)
         -x (level 1):  ~40-50%% improvement
         -xx (level 2): ~100%% vs normal, ~40%% vs level 1
   -v: verbose output
```

Switch -l is used to supply the names of the libraries that should 
be instrumented along the binary. Instrumented libraries will be copied
to the current working directory. This option can be repeated as many times
as needed. Depending on the environment, the LD_LIBRARY_PATH should be set 
to point to instrumented libraries while fuzzing. 

Switch -e is used to manualy specify the entry point where initialization
callback is to be inserted. For unstipped binaries, afl-dyninst defaults 
to using _init of the binary as an entry point. In case of stripped binaries
this option is required and is best set to the address of main which 
can easily be determined by disassembling the binary and looking for an 
argument to __libc_start_main. 

Switch -E is used to specify addresses that should force a clean exit
when reached. This can speed up the fuzzing tremendously.

Switch -s instructs afl-dyninst to skip the first NUMBER of basic blocks. 
Currently, it is used to work around a bug in Dyninst but doubles as an
optimization option, as skipping the basic blocks of the initialization
routines makes things run faster.  If the instrumented binary is crashing by
itself, try skiping a number of blocks.

Switch -r allows you to specify a path to a library that is loaded
via dlopen() at runtime. Instrumented runtime libraries will be 
written to the same location with a ".ins" suffix as not to overwrite
the original ones. Make sure to backup the originals and then rename the
instrumented ones to original name. 

Switch -m allows you to only instrument basic blocks of a minimum size - the
default minimum size is 1

Switch -f fixes a dyninst bug that lead to bugs in the instrumented program:
our basic block instrumentation function loaded into the instrumentd binaries
uses the edi/rdi. However dyninst does not always saves and restores it when
instrumenting that function leading to crashes and changed program behaviour
when the register is used for function parameters.

Switch -S allows you to not instrument specific functions.
This options is mainly to hunt down bugs in dyninst.

Switch -D installs the afl fork server and forced exit functions but no
basic block instrumentation. That would serve no purpose - unless there is
another interesting tool coming up: afl-pin (already available at
https://github.com/vanhauser-thc/afl-pin) and afl-dynamorio (wip)

Switch -x enables performance modes, -x is level 1 and -xx is level 2.
level 1 (-x) is highly recommended (+50%).
level 2 (-xx) gives an additonal 40% but removes (usually unnecessary) precautions


## Example of instrumenting a target binary

Dyninst requires DYNINSTAPI_RT_LIB environment variable to point to the location
of libdyninstAPI_RT.so.

```
$ export DYNINSTAPI_RT_LIB=/usr/local/lib/libdyninstAPI_RT.so
$ ./afl-dyninst -i ./unrar -o ./rar_ins -e 0x4034c0 -s 10
Skipping library: libAflDyninst.so
Instrumenting module: DEFAULT_MODULE
Inserting init callback.
Saving the instrumented binary to ./unrar_ins...
All done! Happy fuzzing!
```

Here we are instrumenting the rar binary with entrypoint at 0x4034c0
(manually found address of main), skipping the first 10 basic blocks 
and outputing to unrar_ins

You can also use the afl-dyninst.sh helper script which sets the required
environment variables for you:
```
$ ./afl-dyninst.sh -i ./unrar -o ./rar_ins -e 0x4034c0 -s 10
```


## Running AFL on the instrumented binary

NOTE: The instrumentation library "libDyninst.so" must be available in the current working
directory or LD_LIBRARY_PATH as that is where the instrumented binary will be looking for it.

Since AFL checks if the binary has been instrumented by afl-gcc, the
AFL_SKIP_BIN_CHECK environment variable needs to be set.
No modifications to AFL itself is needed. 
```
$ export AFL_SKIP_BIN_CHECK=1
```
Then, AFL can be run as usual:
```
$ afl-fuzz -i testcases/archives/common/gzip/ -o test_gzip -- ./gzip_ins -d -c 
```

You can also use the afl-fuzz-dyninst.sh helper script which sets the required
environment variables for you.
```
$ afl-fuzz-dyninst.sh -i testcases/archives/common/gzip/ -o test_gzip -- ./gzip_ins -d -c 
```

## Problems

After instrumenting the target binary always check if it works.
Dyninst is making big changes to the code, and hence more often than not
things are not working anymore.

Problem 1: The binary does not work (crashes or hangs)

Solution: increase the -m parameter. -m 8 is the minimum recommended, on some
          targets -m 16 is required etc.
          You can also try to remove -x performance enhancers


Problem 2: Basically every fuzzing test case is reported as crash although it
           does not when running it from the command line

Solution: This happens if the target is using throw/catch, and dyninst's
          modification result in that the cought exception is not resetted and
          hence abort() is triggered.
          No solution to this issue is known yet.
          Binary editing the target binary to perform _exit(0) would help though.

More problems? Create an issue at https://github.com/vanhauser-thc/afl-dyninst
