#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <climits>

// DyninstAPI includes
#include "BPatch.h"
#include "BPatch_binaryEdit.h"
#include "BPatch_flowGraph.h"
#include "BPatch_function.h"
#include "BPatch_point.h"
#include "BPatch_addressSpace.h"
#include "BPatch_process.h"
#include "dyninstversion.h"     // if this include errors, compile and install https://github.com/dyninst/dyninst

using namespace std;
using namespace Dyninst;

//cmd line options
char *originalBinary;
char *instrumentedBinary;
char *entryPointName = NULL;
int verbose = 0;

Dyninst::Address entryPoint;
set < string > todo;
set < string > instrumentLibraries;
set < string > runtimeLibraries;
set < string > skipAddresses;
set < unsigned long > exitAddresses;
unsigned int bbMinSize = 1;
int bbSkip = 0, performance = 0;
bool skipMainModule = false, do_bb = true, dynfix = false;
unsigned long int insertions = 0;
uintptr_t mapaddr = 0;

BPatch_function *save_rdi;
BPatch_function *restore_rdi;

const char *functions[] = { "main", "_main", "_initproc", "_init", "start", "_start", NULL };

const char *instLibrary = "libAflDyninst.so";

static const char *OPT_STR = "fi:o:l:e:E:vs:dr:m:S:Dx";
static const char *USAGE = "-dfvxD -i <binary> -o <binary> -l <library> -e <address> -E <address> -s <number> -S <funcname> -m <size>\n \
  -i: input binary \n \
  -o: output binary\n \
  -d: do not instrument the binary, only supplied libraries\n \
  -l: linked library to instrument (repeat for more than one)\n \
  -r: runtime library to instrument (path to, repeat for more than one)\n \
  -e: entry point address to patch (required for stripped binaries)\n \
  -E: exit point - force exit(0) at this address (repeat for more than one)\n \
  -s: number of initial basic blocks to skip in binary\n \
  -m: minimum size of a basic bock to instrument (default: 1)\n \
  -f: try to fix a dyninst bug that leads to crashes (loss of 20%% performance)\n \
  -S: do not instrument this function (repeat for more than one)\n \
  -D: instrument only a simple fork server and also forced exit functions\n \
  -x: experimental performance modes (can be set up to three times)\n \
        level 1: ~40-50%% improvement\n \
        level 2: ~100%% vs normal, ~40%% vs level 1\n \
        level 3: ~110%% vs normal, ~5%% vs level 2\n \
      level 3 replaces how basic block coverage works and can be tried if\n \
      normal mode or level 1 or 2 lead to crashes randomly.\n \
  -v: verbose output\n";

bool parseOptions(int argc, char **argv) {
  int c;

  while ((c = getopt(argc, argv, OPT_STR)) != -1) {
    switch ((char) c) {
    case 'x':
      performance++;
      if (performance == 3) {
#if ( __amd64__ || __x86_64__ )
        fprintf(stderr, "Warning: performance level 3 is currently totally experimental\n");
#else
        fprintf(stderr, "Warning: maximum performance level for non-intelx64 x86 is 2\n");
        performance = 2;
#endif
      } else if (performance > 3) {
        fprintf(stderr, "Warning: maximum performance level is 3\n");
        performance = 3;
      }
      break;
    case 'S':
      skipAddresses.insert(optarg);
      break;
    case 'e':
      if ((entryPoint = strtoul(optarg, NULL, 16)) < 0x1000)
        entryPointName = optarg;
      break;
    case 'i':
      originalBinary = optarg;
      instrumentLibraries.insert(optarg);
      break;
    case 'o':
      instrumentedBinary = optarg;
      break;
    case 'l':
      instrumentLibraries.insert(optarg);
      break;
    case 'E':
      exitAddresses.insert(strtoul(optarg, NULL, 16));
      break;
    case 'r':
      runtimeLibraries.insert(optarg);
      break;
    case 's':
      bbSkip = atoi(optarg);
      break;
    case 'm':
      bbMinSize = atoi(optarg);
      break;
    case 'd':
      skipMainModule = true;
      break;
    case 'f':
#if (__amd64__ || __x86_64__)
      dynfix = true;
#endif
      break;
    case 'D':
      do_bb = false;
      break;
    case 'v':
      verbose++;
      break;
    default:
      cerr << "Usage: " << argv[0] << USAGE;
      return false;
    }
  }

  if (performance > 0 && do_bb == false) {
    cerr << "Warning: -x performance options only enhance basic block coverage, not forkserver only mode" << endl;
    performance = 0;
  }

  if (originalBinary == NULL) {
    cerr << "Input binary is required!" << endl;
    cerr << "Usage: " << argv[0] << USAGE;
    return false;
  }

  if (instrumentedBinary == NULL) {
    cerr << "Output binary is required!" << endl;
    cerr << "Usage: " << argv[0] << USAGE;
    return false;
  }

  if (skipMainModule && instrumentLibraries.empty()) {
    cerr << "If using option -d , option -l is required." << endl;
    cerr << "Usage: " << argv[0] << USAGE;
    return false;
  }

  return true;
}

BPatch_function *findFuncByName(BPatch_image * appImage, char *funcName) {
  BPatch_Vector < BPatch_function * >funcs;

  if (NULL == appImage->findFunction(funcName, funcs) || !funcs.size()
      || NULL == funcs[0]) {
    cerr << "Failed to find " << funcName << " function." << endl;
    return NULL;
  }

  return funcs[0];
}

// insert callback to initialization function in the instrumentation library
// either at _init or at manualy specified entry point.
bool insertCallToInit(BPatch_addressSpace * appBin, BPatch_function * instIncFunc, BPatch_module * module, BPatch_function * funcInit, bool install_hack) {
  /* Find the instrumentation points */
  vector < BPatch_point * >points;
  vector < BPatch_point * >*funcEntry = funcInit->findPoint(BPatch_entry);
  BPatch_image *appImage = appBin->getImage();
  BPatchSnippetHandle *handle;

  if (NULL == funcEntry) {
    cerr << "Failed to find entry for function. " << endl;
    return false;
  }

  if (performance >= 3 && install_hack == true) {
    cout << "Inserting global variables" << endl;
    // we set up a fake map so we do not have crashes if the the forkserver
    // is not installed in _init but later for speed reasons.
    // we could also check in the bb() code if map == 0 but that would
    // cost precious instructions.
    BPatch_variableExpr *fakemap = appBin->malloc(65536);
    BPatch_constExpr fakemap_ptr(fakemap->getBaseAddr());
    BPatch_variableExpr *map = appBin->malloc(*(appImage->findType("size_t")), "map");
    BPatch_arithExpr initmap(BPatch_assign, *map, fakemap_ptr);

    appBin->insertSnippet(initmap, *funcEntry, BPatch_firstSnippet);
    BPatch_constExpr map_ptr(map->getBaseAddr());
    BPatch_variableExpr *prev_id = appBin->malloc(*(appImage->findType("size_t")), "prev_id");
    BPatch_arithExpr initprevid(BPatch_assign, *prev_id, BPatch_constExpr(0));

    appBin->insertSnippet(initprevid, *funcEntry);
    BPatch_Vector < BPatch_snippet * >instArgs;
    cout << "Inserting init callback." << endl;
    instArgs.push_back(&map_ptr);
    BPatch_funcCallExpr instIncExpr(*instIncFunc, instArgs);

    handle = appBin->insertSnippet(instIncExpr, *funcEntry, BPatch_callBefore, BPatch_lastSnippet);
  } else {
    BPatch_Vector < BPatch_snippet * >instArgs;
    cout << "Inserting init callback." << endl;
    BPatch_funcCallExpr instIncExpr(*instIncFunc, instArgs);

    handle = appBin->insertSnippet(instIncExpr, *funcEntry, BPatch_callBefore, BPatch_lastSnippet);
  }

  if (!handle) {
    cerr << "Failed to insert init callback." << endl;
    return false;
  }
  return true;
}

// inserts a callback for each basic block assigning it an instrumentation
// time 16bit random ID just as afl
bool insertBBCallback(BPatch_addressSpace * appBin, BPatch_function * curFunc, char *funcName, BPatch_function * instBBIncFunc, int *bbIndex) {
  BPatch_image *appImage = appBin->getImage();
  BPatch_flowGraph *appCFG = curFunc->getCFG();
  unsigned short randID;

  if (!appCFG) {
    cerr << "Failed to find CFG for function " << funcName << endl;
    return false;
  }

  BPatch_Set < BPatch_basicBlock * >allBlocks;
  if (!appCFG->getAllBasicBlocks(allBlocks)) {
    cerr << "Failed to find basic blocks for function " << funcName << endl;
    return false;
  } else if (allBlocks.size() == 0) {
    cerr << "No basic blocks for function " << funcName << endl;
    return false;
  }

  BPatch_Set < BPatch_basicBlock * >::iterator iter;
  for (iter = allBlocks.begin(); iter != allBlocks.end(); iter++) {
    if (*bbIndex < bbSkip || (*iter)->size() < bbMinSize) {     // skip over first bbSkip bbs or below minimum size
      (*bbIndex)++;
      continue;
    }

    BPatch_point *bbEntry = (*iter)->findEntryPoint();

    if (performance >= 1) {
      if ((*iter)->isEntryBlock() == false) {
        bool good = false;

        BPatch_Vector < BPatch_basicBlock * >sources;
        (*iter)->getSources(sources);
        for (unsigned int i = 0; i < sources.size() && good == false; i++) {
          BPatch_Vector < BPatch_basicBlock * >targets;
          sources[i]->getTargets(targets);
          if (targets.size() > 1)
            good = true;
        }
        if (good == false)
          continue;
      }
    }

    unsigned long address = (*iter)->getStartAddress();

    randID = rand() % USHRT_MAX;
    if (verbose >= 1) {
      cout << "Instrumenting Basic Block 0x" << hex << address << " of " << funcName << " with size " << dec << (*iter)->size() << " with random id " << randID << "/0x" << hex <<
        randID << endl;
    }

    if (NULL == bbEntry) {
      // warn the user, but continue
      cerr << "Failed to find entry for basic block at 0x" << hex << address << endl;
      (*bbIndex)++;
      continue;
    }

    BPatchSnippetHandle *handle;

    if (performance >= 3) {
      // these are dummy instructions we overwrite later
      BPatch_variableExpr *pid = appImage->findVariable("prev_id");
      BPatch_arithExpr new_prev_id(BPatch_assign, *pid, BPatch_arithExpr(BPatch_divide, BPatch_constExpr(8), BPatch_constExpr(2)));

      handle = appBin->insertSnippet(new_prev_id, *bbEntry, BPatch_lastSnippet);
      BPatch_variableExpr *map = appImage->findVariable("map");
      BPatch_variableExpr *pid2 = appImage->findVariable("prev_id");
      BPatch_arithExpr map_idx(BPatch_arithExpr(BPatch_plus, *map, BPatch_arithExpr(BPatch_divide, *pid2, BPatch_constExpr(2))));

      if (mapaddr == 0) {
        printf("Map for AFL is installed at: %p\n", (void *) map->getBaseAddr());
        mapaddr = (uintptr_t) map->getBaseAddr();
      }
      handle = appBin->insertSnippet(map_idx, *bbEntry, BPatch_firstSnippet);
    } else {
      BPatch_Vector < BPatch_snippet * >instArgs1;
      BPatch_Vector < BPatch_snippet * >instArgs;
      BPatch_constExpr bbId(randID);

      instArgs.push_back(&bbId);
      BPatch_funcCallExpr instIncExpr1(*save_rdi, instArgs1);
      BPatch_funcCallExpr instIncExpr3(*restore_rdi, instArgs1);
      BPatch_funcCallExpr instIncExpr(*instBBIncFunc, instArgs);

      if (dynfix == true)
        handle = appBin->insertSnippet(instIncExpr1, *bbEntry, BPatch_callBefore, BPatch_firstSnippet);
      handle = appBin->insertSnippet(instIncExpr, *bbEntry, BPatch_callBefore);
      if (dynfix == true)
        handle = appBin->insertSnippet(instIncExpr3, *bbEntry, BPatch_callBefore, BPatch_lastSnippet);
    }

    if (!handle) {
      // warn the user, but continue to next bb
      cerr << "Failed to insert instrumention in basic block at 0x" << hex << address << endl;
      (*bbIndex)++;
      continue;
    } else
      insertions++;

    (*bbIndex)++;
  }

  return true;
}

int main(int argc, char **argv) {
  char *func2patch = NULL;
  int loop;

  if (argc < 3 || strncmp(argv[1], "-h", 2) == 0 || strncmp(argv[1], "--h", 3) == 0) {
    cout << "Usage: " << argv[0] << USAGE;
    return false;
  }

  if (!parseOptions(argc, argv)) {
    return EXIT_FAILURE;
  }
#if (__amd64__ || __x86_64__)
  if (do_bb == true) {
    if (DYNINST_MAJOR_VERSION < 9 || (DYNINST_MAJOR_VERSION == 9 && DYNINST_MINOR_VERSION < 3)
        || (DYNINST_MAJOR_VERSION == 9 && DYNINST_MINOR_VERSION == 3 && DYNINST_PATCH_VERSION <= 2)) {
      if (dynfix == false)
        fprintf(stderr, "Warning: your dyninst version does not include a critical fix, you should use the -f option!\n");
    } else {
      if (dynfix == true)
        fprintf(stderr, "Notice: your dyninst version is fixed, the -f option should not be necessary.\n");
    }
  }
#endif

  BPatch bpatch;

  if (performance >= 2) {
    bpatch.setSaveFPR(false);
    bpatch.setTrampRecursive(true);
  }

  BPatch_addressSpace *appBin = bpatch.openBinary(originalBinary, instrumentLibraries.size() != 1);

  if (appBin == NULL) {
    cerr << "Failed to open binary" << endl;
    return EXIT_FAILURE;
  }

  BPatch_image *appImage = appBin->getImage();

  //get and iterate over all modules, instrumenting only the default and manually specified ones
  vector < BPatch_module * >*modules = appImage->getModules();
  vector < BPatch_module * >::iterator moduleIter;
  vector < BPatch_function * >*funcsInModule;
  BPatch_module *defaultModule = NULL, *firstModule = NULL;
  string defaultModuleName;

  // look for _init
  if (defaultModuleName.empty()) {
    for (loop = 0; functions[loop] != NULL && func2patch == NULL; loop++) {
      for (moduleIter = modules->begin(); moduleIter != modules->end(); ++moduleIter) {
        vector < BPatch_function * >::iterator funcsIterator;
        char moduleName[1024];

        if (firstModule == NULL)
          firstModule = (*moduleIter);
        (*moduleIter)->getName(moduleName, 1024);
        funcsInModule = (*moduleIter)->getProcedures();
        if (verbose >= 2)
          cout << "Looking for init function " << functions[loop] << " in " << moduleName << endl;
        for (funcsIterator = funcsInModule->begin(); funcsIterator != funcsInModule->end(); ++funcsIterator) {
          char funcName[1024];

          (*funcsIterator)->getName(funcName, 1024);
          if (verbose >= 3 && loop == 0)
            printf("module: %s function: %s\n", moduleName, funcName);
          if (string(funcName) == string(functions[loop])) {
            func2patch = (char *) functions[loop];
            defaultModuleName = string(moduleName);
            defaultModule = (*moduleIter);
            if (verbose >= 1) {
              cout << "Found " << func2patch << " in " << moduleName << endl;
            }
            break;
          }
        }
        if (!defaultModuleName.empty())
          break;
      }
      if (func2patch != NULL)
        break;
    }
  }
  // last resort, by name of the binary
  if (defaultModuleName.empty())
    defaultModuleName = string(originalBinary).substr(string(originalBinary).find_last_of("\\/") + 1);
  if (defaultModule == NULL)
    defaultModule = firstModule;

  if (!appBin->loadLibrary(instLibrary)) {
    cerr << "Failed to open instrumentation library " << instLibrary << endl;
    cerr << "It needs to be located in the current working directory." << endl;
    return EXIT_FAILURE;
  }

  /* Find code coverage functions in the instrumentation library */
  BPatch_function *initAflForkServer;

  save_rdi = findFuncByName(appImage, (char *) "save_rdi");
  restore_rdi = findFuncByName(appImage, (char *) "restore_rdi");
  BPatch_function *bbCallback = findFuncByName(appImage, (char *) "bbCallback");
  BPatch_function *forceCleanExit = findFuncByName(appImage, (char *) "forceCleanExit");

  if (do_bb == true) {
    if (performance >= 3)
      initAflForkServer = findFuncByName(appImage, (char *) "initAflForkServerVar");
    else
      initAflForkServer = findFuncByName(appImage, (char *) "initAflForkServer");
  } else
    initAflForkServer = findFuncByName(appImage, (char *) "initOnlyAflForkServer");

  if (!initAflForkServer || !bbCallback || !save_rdi || !restore_rdi || !forceCleanExit) {
    cerr << "Instrumentation library lacks callbacks!" << endl;
    return EXIT_FAILURE;
  }

  int bbIndex = 0;

  // if an entrypoint was set then find function, else find _init
  BPatch_function *funcToPatch = NULL;

  if (entryPoint == 0 && entryPointName == NULL) {
    if (func2patch == NULL) {
      cerr << "Couldn't locate _init, specify entry point manually with -e 0xaddr" << endl;
      return EXIT_FAILURE;
    }
    BPatch_Vector < BPatch_function * >funcs;
    defaultModule->findFunction(func2patch, funcs);
    if (!funcs.size()) {
      cerr << "Couldn't locate _init, specify entry point manually with -e 0xaddr" << endl;
      return EXIT_FAILURE;
    }
    // there should really be only one
    funcToPatch = funcs[0];
  } else {
    if (entryPointName != NULL) {
      for (moduleIter = modules->begin(); moduleIter != modules->end() && funcToPatch == 0; ++moduleIter) {
        BPatch_Vector < BPatch_function * >funcs;
        (*moduleIter)->findFunction(entryPointName, funcs);
        if (funcs.size() > 0) {
          char moduleName[1024];

          funcToPatch = funcs[0];
          defaultModule = (*moduleIter);
          defaultModule->getName(moduleName, 1024);
          defaultModuleName = string(moduleName);
          printf("Found entypoint %s in module %s\n", entryPointName, moduleName);
          break;
        }
      }
    }
    if (!funcToPatch) {
      if (verbose > 1)
        printf("Looking for entrypoint %p\n", (char *) entryPoint);
      funcToPatch = defaultModule->findFunctionByEntry(entryPoint);
      if (!funcToPatch && defaultModule != firstModule) {
        funcToPatch = firstModule->findFunctionByEntry(entryPoint);
        if (funcToPatch)
          defaultModule = firstModule;
      }
      if (!funcToPatch) {       // ok lets go hardcore ...
        if (verbose > 1)
          printf("OK we did not find the entrypoint so far, lets dig deeper ...\n");
        for (moduleIter = modules->begin(); moduleIter != modules->end() && funcToPatch != NULL; ++moduleIter) {
          vector < BPatch_function * >::iterator funcsIterator;
          funcToPatch = (*moduleIter)->findFunctionByEntry(entryPoint);
          if (funcToPatch)
            defaultModule = (*moduleIter);
        }
      }
      if (funcToPatch && verbose >= 1) {
        char moduleName[1024];

        defaultModule->getName(moduleName, 1024);
        defaultModuleName = string(moduleName);
        printf("Found entypoint %p in module %s\n", (void *) entryPoint, moduleName);
      }
    }
  }
  if (!funcToPatch) {
    cerr << "Couldn't locate function at given entry point. " << endl;
    cerr << "Try: readelf -ls " << originalBinary << " | egrep 'Entry|FUNC.*GLOBAL.*DEFAULT' | egrep -v '@|UND'" << endl;
    return EXIT_FAILURE;
  }
  if (!insertCallToInit(appBin, initAflForkServer, defaultModule, funcToPatch, true)) {
    cerr << "Could not insert init callback at given entry point." << endl;
    return EXIT_FAILURE;
  }

  for (moduleIter = modules->begin(); moduleIter != modules->end(); ++moduleIter) {
    char moduleName[1024];

    (*moduleIter)->getName(moduleName, 1024);
    if ((*moduleIter)->isSharedLib()) {
      if (instrumentLibraries.find(moduleName) == instrumentLibraries.end()) {
        cout << "Skipping library: " << moduleName << endl;
        continue;
      }
    }

    if (string(moduleName).find(defaultModuleName) != string::npos) {
      if (skipMainModule)
        continue;
    }

    if (do_bb == true) {
      cout << "Instrumenting module: " << moduleName << endl;
      vector < BPatch_function * >*allFunctions = (*moduleIter)->getProcedures();
      vector < BPatch_function * >::iterator funcIter;
      // iterate over all functions in the module
      for (funcIter = allFunctions->begin(); funcIter != allFunctions->end(); ++funcIter) {
        BPatch_function *curFunc = *funcIter;
        char funcName[1024];
        int do_patch = 1;

        curFunc->getName(funcName, 1024);
        if (string(funcName) == string("_init") || string(funcName) == string("__libc_csu_init") || string(funcName) == string("_start")
          )
          continue;             // here's a bug on hlt // XXX: check what happens if removed
        if (!skipAddresses.empty()) {
          set < string >::iterator saiter;
          for (saiter = skipAddresses.begin(); saiter != skipAddresses.end() && do_patch == 1; saiter++)
            if (*saiter == string(funcName))
              do_patch = 0;
          if (do_patch == 0) {
            cout << "Skipping instrumenting function " << funcName << endl;
            continue;
          }
        }
        insertBBCallback(appBin, curFunc, funcName, bbCallback, &bbIndex);
      }
    }
  }

  if (!exitAddresses.empty()) {
    cout << "Instrumenting forced exit addresses." << endl;
    set < unsigned long >::iterator uliter;

    for (uliter = exitAddresses.begin(); uliter != exitAddresses.end(); uliter++) {
      if (*uliter > 0 && (signed long) *uliter != -1) {
        funcToPatch = defaultModule->findFunctionByEntry(*uliter);
        if (!funcToPatch) {
          cerr << "Could not find enty point 0x" << hex << *uliter << " (continuing)" << endl;
        } else {
          if (!insertCallToInit(appBin, forceCleanExit, defaultModule, funcToPatch, false))
            cerr << "Could not insert force clean exit callback at 0x" << hex << *uliter << " (continuing)" << endl;
        }
      }
    }
  }

  cout << "Saving the instrumented binary to " << instrumentedBinary << " ..." << endl;
  // Output the instrumented binary
  BPatch_binaryEdit *appBinr = dynamic_cast < BPatch_binaryEdit * >(appBin);

  if (!appBinr->writeFile(instrumentedBinary)) {
    cerr << "Failed to write output file: " << instrumentedBinary << endl;
    return EXIT_FAILURE;
  }
  todo.insert(instrumentedBinary);
  
  if (!runtimeLibraries.empty()) {
    cout << "Instrumenting runtime libraries." << endl;
    set < string >::iterator rtLibIter;
    for (rtLibIter = runtimeLibraries.begin(); rtLibIter != runtimeLibraries.end(); rtLibIter++) {
      BPatch_addressSpace *libBin = bpatch.openBinary((*rtLibIter).c_str(), false);

      if (libBin == NULL) {
        cerr << "Failed to open binary " << *rtLibIter << endl;
        return EXIT_FAILURE;
      }
      BPatch_image *libImg = libBin->getImage();

      vector < BPatch_module * >*modules = libImg->getModules();
      moduleIter = modules->begin();
      for (; moduleIter != modules->end(); ++moduleIter) {
        char moduleName[1024];

        (*moduleIter)->getName(moduleName, 1024);
        cout << "Instrumenting module: " << moduleName << endl;
        vector < BPatch_function * >*allFunctions = (*moduleIter)->getProcedures();
        vector < BPatch_function * >::iterator funcIter;
        // iterate over all functions in the module
        for (funcIter = allFunctions->begin(); funcIter != allFunctions->end(); ++funcIter) {
          BPatch_function *curFunc = *funcIter;
          char funcName[1024];
          int do_patch = 1;

          curFunc->getName(funcName, 1024);
          if (string(funcName) == string("_init") || string(funcName) == string("__libc_csu_init") || string(funcName) == string("_start"))
            continue;
          if (!skipAddresses.empty()) {
            set < string >::iterator saiter;
            for (saiter = skipAddresses.begin(); saiter != skipAddresses.end() && do_patch == 1; saiter++)
              if (*saiter == string(funcName))
                do_patch = 0;
            if (do_patch == 0) {
              cout << "Skipping instrumenting function " << funcName << endl;
              continue;
            }
          }

          insertBBCallback(libBin, curFunc, funcName, bbCallback, &bbIndex);
        }
      }
      appBinr = dynamic_cast < BPatch_binaryEdit * >(libBin);
      if (!appBinr->writeFile((*rtLibIter + ".ins").c_str())) {
        cerr << "Failed to write output file: " << (*rtLibIter + ".ins").c_str() << endl;
        return EXIT_FAILURE;
      } else {
        cout << "Saved the instrumented library to " << (*rtLibIter + ".ins").c_str() << "." << endl;
        todo.insert(*rtLibIter + ".ins");
      }
    }
  }
  
  printf("Did a total of %lu basic block insertions\n", insertions);
  
  if (performance >= 3) {
    int fd;
    struct stat st;
    uint64_t i, found = 0;
    unsigned char *ptr;

    unsigned char snip1[] = {
      0x00, 0x00, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    unsigned char snip2[] = {
      0x08, 0x00, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    unsigned char fullsnip[] = {
      0x53, 0x50, 0x41, 0x52, 0x48, 0xBB, 0x00, 0x00, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x03, 0x48, 0x85, 0xc0, 0x74, 0x28, 0x49, 0xBA, 0x08, 0x00, 0x71, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x66, 0x41, 0x8b, 0x1a, 0x66, 0x81, 0xf3, 0x99, 0x99, 0x48, 0x0f, 0xb7, 0xdb, 0x80, 0x04, 0x18, 0x01, 0x66, 0x41, 0x8b, 0x1a, 0x66, 0xd1, 0xfb,
      0x66, 0x41, 0x89, 0x1a,
      0x41, 0x5a, 0x58, 0x5b, 0x90, 0x90, 0x90, 0x90
    };
    memcpy(snip1, (char *) &mapaddr, sizeof(mapaddr));
    memcpy(fullsnip + 6, (char *) &mapaddr, sizeof(mapaddr));
    mapaddr += sizeof(mapaddr);
    memcpy(snip2, (char *) &mapaddr, sizeof(mapaddr));
    memcpy(fullsnip + 24, (char *) &mapaddr, sizeof(mapaddr));
    set < string >::iterator fn;
    for (fn = todo.begin(); fn != todo.end(); fn++) {
      cout << "Reinstrumenting " << *fn << " ..." << endl;
      if ((fd = open((const char *) (fn->c_str()), O_RDWR)) == -1 || fstat(fd, &st) != 0) {
        cerr << "Error: file is gone: " << *fn << endl;
        exit(-1);
      }
      if ((size_t) st.st_size < (size_t) sizeof(fullsnip)) {
        cerr << "Error: somethings horrible wrong here with " << *fn << " ..." << endl;
        continue;
      }
      ptr = (unsigned char *) mmap(NULL, st.st_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
      for (i = 2; i < (size_t) st.st_size - (size_t) sizeof(fullsnip); i++) {
        if (memcmp(ptr + i, snip1, sizeof(snip1)) == 0 && memcmp(ptr + i + sizeof(snip1) + 4, snip2, sizeof(snip2)) == 0) {
          found++;
          fullsnip[0x27] = rand() % 256;
          fullsnip[0x28] = rand() % 256;
          memcpy(ptr + i - 2, fullsnip, sizeof(fullsnip));
        }
      }
      //printf("found %lu entries, snipsize %u\n", found, (unsigned int)sizeof(fullsnip));
      munmap((void *) ptr, st.st_size);
      close(fd);
    }
    if (found == insertions) {
      printf("SUCCESS! Performance level 3 succeeded :)\n");
    } else {
      fprintf(stderr, "Error: can not complete performance level 3, could not find all insertions (%lu of %lu).\n", found, insertions);
      exit(-1);
    }
  }

  cout << "All done! Happy fuzzing!" << endl;
  return EXIT_SUCCESS;
}
