#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include <stdlib.h>
#include <sstream>
#include <climits>
using namespace std;

// Command line parsing
#include <getopt.h>

// DyninstAPI includes
#include "BPatch.h"
#include "BPatch_binaryEdit.h"
#include "BPatch_flowGraph.h"
#include "BPatch_function.h"
#include "BPatch_point.h"

using namespace Dyninst;

//cmd line options
char *originalBinary;
char *instrumentedBinary;
bool verbose = false;

Dyninst::Address entryPoint;
set < string > instrumentLibraries;
set < string > runtimeLibraries;
int bbSkip = 0, dynfix = 0;
unsigned int bbMinSize = 1;
bool skipMainModule = false;

BPatch_function *save_rdi;
BPatch_function *restore_rdi;

const char *instLibrary = "libAflDyninst.so";

static const char *OPT_STR = "fi:o:l:e:vs:dr:m:";
static const char *USAGE = " -i <binary> -o <binary> -l <library> -e <address> -s <number> -m <size>\n \
  -i: Input binary \n \
  -o: Output binary\n \
  -d: Don't instrument the binary, only supplied libraries\n \
  -l: Linked library to instrument (repeat for more than one)\n \
  -r: Runtime library to instrument (path to, repeat for more than one)\n \
  -e: Entry point address to patch (required for stripped binaries)\n \
  -s: Number of basic blocks to skip\n \
  -m: minimum size of a basic bock to instrument (default: 1)\n \
  -f: try to fix crashes\n \
  -v: Verbose output\n";

bool parseOptions(int argc, char **argv) {
  int c;

  while ((c = getopt(argc, argv, OPT_STR)) != -1) {
    switch ((char) c) {
    case 'e':
      entryPoint = strtoul(optarg, NULL, 16);;
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
      dynfix = 1;
      break;
    case 'v':
      verbose = true;
      break;
    default:
      cerr << "Usage: " << argv[0] << USAGE;
      return false;
    }
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
bool insertCallToInit(BPatch_binaryEdit *appBin, BPatch_function *instIncFunc, BPatch_module *module, BPatch_function *funcInit) {
  /* Find the instrumentation points */
  vector < BPatch_point * >points;
  vector < BPatch_point * >*funcEntry = funcInit->findPoint(BPatch_entry);

  if (NULL == funcEntry) {
    cerr << "Failed to find entry for function. " << endl;
    return false;
  }

  cout << "Inserting init callback." << endl;
  BPatch_Vector < BPatch_snippet * >instArgs;   // init has no args
  BPatch_funcCallExpr instIncExpr(*instIncFunc, instArgs);

  /* Insert the snippet at function entry */
  BPatchSnippetHandle *handle = appBin->insertSnippet(instIncExpr, *funcEntry, BPatch_callBefore,
                                                      BPatch_lastSnippet);

  if (!handle) {
    cerr << "Failed to insert init callback." << endl;
    return false;
  }
  return true;
}

// inserts a callback for each basic block assigning it an instrumentation
// time 16bit random ID just as afl
bool insertBBCallback(BPatch_binaryEdit *appBin, BPatch_function *curFunc, char *funcName, BPatch_function *instBBIncFunc, int *bbIndex) {
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
    if (*bbIndex < bbSkip || (*iter)->size() < bbMinSize) {    // skip over first bbSkip bbs
      (*bbIndex)++;
      continue;
    }
    unsigned long address = (*iter)->getStartAddress();

    randID = rand() % USHRT_MAX;
    if (verbose) {
      cout << "Instrumenting Basic Block 0x" << hex << address << " of " << funcName << " of size " << dec << (*iter)->size() << " with random id " << randID << endl;
    }

    BPatch_Vector < BPatch_snippet * >instArgs1;
    BPatch_Vector < BPatch_snippet * >instArgs;
    BPatch_constExpr bbId(randID);

    instArgs.push_back(&bbId);
    BPatch_point *bbEntry = (*iter)->findEntryPoint();

    if (NULL == bbEntry) {
      // warn the user, but continue
      cerr << "Failed to find entry for basic block at 0x" << hex << address << endl;
      (*bbIndex)++;
      continue;
    }

    BPatch_funcCallExpr instIncExpr1(*save_rdi, instArgs1);
    BPatch_funcCallExpr instIncExpr3(*restore_rdi, instArgs1);
    BPatch_funcCallExpr instIncExpr(*instBBIncFunc, instArgs);
    BPatchSnippetHandle *handle;
    if (dynfix)
      handle = appBin->insertSnippet(instIncExpr1, *bbEntry, BPatch_callBefore, BPatch_lastSnippet);
    handle = appBin->insertSnippet(instIncExpr, *bbEntry, BPatch_callBefore, BPatch_lastSnippet);
    if (dynfix)
      handle = appBin->insertSnippet(instIncExpr3, *bbEntry, BPatch_callBefore, BPatch_lastSnippet);

    if (!handle) {
      // warn the user, but continue to next bb
      cerr << "Failed to insert instrumention in basic block at 0x" << hex << address << endl;
      (*bbIndex)++;
      continue;
    }

    (*bbIndex)++;
  }

  return true;
}

int main(int argc, char **argv) {
  if (!parseOptions(argc, argv)) {
    return EXIT_FAILURE;
  }

  BPatch bpatch;
  BPatch_binaryEdit *appBin = bpatch.openBinary(originalBinary, instrumentLibraries.size() != 1);

  if (appBin == NULL) {
    cerr << "Failed to open binary" << endl;
    return EXIT_FAILURE;
  }

  BPatch_image *appImage = appBin->getImage();

  //get and iterate over all modules, instrumenting only the default and manually specified ones
  vector < BPatch_module * >*modules = appImage->getModules();
  vector < BPatch_module * >::iterator moduleIter;
  vector < BPatch_function * >*funcsInModule;
  BPatch_module *defaultModule = NULL;
  string defaultModuleName;

  // look for _init
  if (defaultModuleName.empty()) {
    for (moduleIter = modules->begin(); moduleIter != modules->end(); ++moduleIter) {
      funcsInModule = (*moduleIter)->getProcedures();
      vector < BPatch_function * >::iterator funcsIterator;
      for (funcsIterator = funcsInModule->begin(); funcsIterator != funcsInModule->end(); ++funcsIterator) {
        char funcName[1024];

        (*funcsIterator)->getName(funcName, 1024);
        if (string(funcName) == string("_init")) {
          char moduleName[1024];

          (*moduleIter)->getName(moduleName, 1024);
          defaultModuleName = string(moduleName);
          if (verbose) {
            cout << "Found _init in " << moduleName << endl;
          }
          break;
        }
      }
      if (!defaultModuleName.empty())
        break;
    }
  }
  // last resort, by name of the binary
  if (defaultModuleName.empty())
    defaultModuleName = string(originalBinary).substr(string(originalBinary).find_last_of("\\/") + 1);

  if (!appBin->loadLibrary(instLibrary)) {
    cerr << "Failed to open instrumentation library " << instLibrary << endl;
    cerr << "It needs to be located in the current working directory." << endl;
    return EXIT_FAILURE;
  }

  appImage = appBin->getImage();

  /* Find code coverage functions in the instrumentation library */
  BPatch_function *initAflForkServer = findFuncByName(appImage, (char *) "initAflForkServer");
  save_rdi = findFuncByName(appImage, (char *) "save_rdi");
  restore_rdi = findFuncByName(appImage, (char *) "restore_rdi");
  BPatch_function *bbCallback = findFuncByName(appImage, (char *) "bbCallback");

  if (!initAflForkServer || !bbCallback || !save_rdi || !restore_rdi) {
    cerr << "Instrumentation library lacks callbacks!" << endl;
    return EXIT_FAILURE;
  }

  int bbIndex = 0;

  // instrument all shared libraries:
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
      defaultModule = (*moduleIter);
      if (skipMainModule)
        continue;
    }
    cout << "Instrumenting module: " << moduleName << endl;
    vector < BPatch_function * >*allFunctions = (*moduleIter)->getProcedures();
    vector < BPatch_function * >::iterator funcIter;

    // iterate over all functions in the module
    for (funcIter = allFunctions->begin(); funcIter != allFunctions->end(); ++funcIter) {
      BPatch_function *curFunc = *funcIter;
      char funcName[1024];

      curFunc->getName(funcName, 1024);
      if (string(funcName) == string("_start"))
        continue;               // here's a bug on hlt // XXX: check what happens if removed
      insertBBCallback(appBin, curFunc, funcName, bbCallback, &bbIndex);
    }
  }

  // if entrypoint set´ then find function, else find _init
  BPatch_function *funcToPatch = NULL;

  if (!entryPoint) {
    BPatch_Vector < BPatch_function * >funcs;
    defaultModule->findFunction("_init", funcs);
    if (!funcs.size()) {
      cerr << "Couldn't locate _init, specify entry point manualy. " << endl;
      return EXIT_FAILURE;
    }
    // there should really be only one
    funcToPatch = funcs[0];
  } else {
    funcToPatch = defaultModule->findFunctionByEntry(entryPoint);
  }

  if (!funcToPatch) {
    cerr << "Couldn't locate function at given entry point. " << endl;
    return EXIT_FAILURE;
  }
  if (!insertCallToInit(appBin, initAflForkServer, defaultModule, funcToPatch)) {
    cerr << "Could not insert init callback at given entry point." << endl;
    return EXIT_FAILURE;
  }

  cout << "Saving the instrumented binary to " << instrumentedBinary << " ..." << endl;
  // Output the instrumented binary
  if (!appBin->writeFile(instrumentedBinary)) {
    cerr << "Failed to write output file: " << instrumentedBinary << endl;
    return EXIT_FAILURE;
  }

  if (!runtimeLibraries.empty()) {
    cout << "Instrumenting runtime libraries." << endl;
    set < string >::iterator rtLibIter;
    for (rtLibIter = runtimeLibraries.begin(); rtLibIter != runtimeLibraries.end(); rtLibIter++) {
      BPatch_binaryEdit *libBin = bpatch.openBinary((*rtLibIter).c_str(), false);

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

          curFunc->getName(funcName, 1024);
          if (string(funcName) == string("_start"))
            continue;
          insertBBCallback(libBin, curFunc, funcName, bbCallback, &bbIndex);
        }
      }
      if (!libBin->writeFile((*rtLibIter + ".ins").c_str())) {
        cerr << "Failed to write output file: " << (*rtLibIter + ".ins").c_str() << endl;
        return EXIT_FAILURE;
      } else {
        cout << "Saved the instrumented library to " << (*rtLibIter + ".ins").c_str() << "." << endl;
      }
    }
  }

  cout << "All done! Happy fuzzing!" << endl;
  return EXIT_SUCCESS;
}
