#include "config.h" // do symlink: ln -s ../AFLplusplus/include afl
#include "types.h" // Also needed via symlink for newer AFL versions
#include "dyninstversion.h" // if this include errors, compile and install https://github.com/dyninst/dyninst
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace std;

static u8 dummy[65536];
static u8 *__afl_area_ptr = dummy; // this saves a test + jz instruction
static s32 shm_id;
static int __afl_temp_data;
static pid_t __afl_fork_pid;
static unsigned short int prev_id = 0;
static bool forkserver_installed = false;
#if (__amd64__ || __x86_64__)
#if (DYNINST_MAJOR_VERSION < 10)
static long saved_di;
register long rdi asm("di"); // the warning is fine - we need the warning because of a bug in dyninst9
#endif
#endif

#define PRINT_ERROR(string) (void)(write(2, string, strlen(string)) + 1) // the (...+1) weirdness is so we do not get an ignoring return value warning

void initAflForkServer() {
  if (forkserver_installed == true)
    return;
  forkserver_installed = true;

  // we can not use fprint* stdout/stderr functions here, it fucks up some programs
  char *shm_env_var = getenv(SHM_ENV_VAR);

  if (!shm_env_var) {
    PRINT_ERROR("Error getting shm\n");
    return;
  }
  shm_id = atoi(shm_env_var);
  __afl_area_ptr = (u8 *)shmat(shm_id, NULL, 0);
  if (__afl_area_ptr == (u8 *)-1) {
    PRINT_ERROR("Error: shmat\n");
    return;
  }
  // enter fork() server thyme!
  int n = write(FORKSRV_FD + 1, &__afl_temp_data, 4);

  if (n != 4) {
    PRINT_ERROR("Error writing fork server\n");
    return;
  }
  while (1) {
    n = read(FORKSRV_FD, &__afl_temp_data, 4);
    if (n != 4) {
      PRINT_ERROR("Error reading fork server\n");
      return;
    }

    __afl_fork_pid = fork();
    if (__afl_fork_pid < 0) {
      PRINT_ERROR("Error on fork()\n");
      return;
    }
    if (__afl_fork_pid == 0) {
      close(FORKSRV_FD);
      close(FORKSRV_FD + 1);
      prev_id = 0;
      break;
    } else {
      // parrent stuff
      n = write(FORKSRV_FD + 1, &__afl_fork_pid, 4);
      pid_t temp_pid = waitpid(__afl_fork_pid, &__afl_temp_data, 2);

      if (temp_pid == 0) {
        return;
      }
      n = write(FORKSRV_FD + 1, &__afl_temp_data, 4);
    }
  }
}

// Should be called on basic block entry
void bbCallback(unsigned short id) {
  __afl_area_ptr[prev_id ^ id]++;
  prev_id = id >> 1;
}

void forceCleanExit() { exit(0); }

#if (DYNINST_MAJOR_VERSION < 10)
void save_rdi() {
#if __amd64__ || __x86_64__
  saved_di = rdi;
#endif
}

void restore_rdi() {
#if __amd64__ || __x86_64__
  rdi = saved_di;
#endif
}
#endif

void initOnlyAflForkServer() {
  if (forkserver_installed == true)
    return;
  forkserver_installed = true;

  // enter fork() server thyme!
  int n = write(FORKSRV_FD + 1, &__afl_temp_data, 4);

  if (n != 4) {
    PRINT_ERROR("Error writting fork server\n");
    return;
  }
  while (1) {
    n = read(FORKSRV_FD, &__afl_temp_data, 4);
    if (n != 4) {
      PRINT_ERROR("Error reading fork server\n");
      return;
    }

    __afl_fork_pid = fork();
    if (__afl_fork_pid < 0) {
      PRINT_ERROR("Error on fork()\n");
      return;
    }
    if (__afl_fork_pid == 0) {
      close(FORKSRV_FD);
      close(FORKSRV_FD + 1);
      prev_id = 0;
      break;
    } else {
      // parrent stuff
      n = write(FORKSRV_FD + 1, &__afl_fork_pid, 4);
      pid_t temp_pid = waitpid(__afl_fork_pid, &__afl_temp_data, 2);

      if (temp_pid == 0) {
        return;
      }
      n = write(FORKSRV_FD + 1, &__afl_temp_data, 4);
    }
  }
}

void initAflForkServerVar(u8 *map) {
  // we can not use fprint* stdout/stderr functions here, it fucks up some programs
  if (forkserver_installed == true)
    return;
  forkserver_installed = true;

  u8 **ptr = (u8 **)map;
  char *shm_env_var = getenv(SHM_ENV_VAR);
  if (!shm_env_var) {
    char buf[256];
    PRINT_ERROR("Error getting shm\n");
    snprintf(buf, sizeof(buf), "__afl_area_ptr: %p\n", ptr);
    PRINT_ERROR(buf);
    return;
  }

  shm_id = atoi(shm_env_var);
  *ptr = (u8 *)shmat(shm_id, NULL, 0);
  if ((u8 *)*ptr == (u8 *)-1) {
    PRINT_ERROR("Error: shmat\n");
    return;
  }
  // enter fork() server thyme!
  int n = write(FORKSRV_FD + 1, &__afl_temp_data, 4);

  if (n != 4) {
    PRINT_ERROR("Error writing fork server\n");
    return;
  }
  while (1) {
    n = read(FORKSRV_FD, &__afl_temp_data, 4);
    if (n != 4) {
      PRINT_ERROR("Error reading fork server\n");
      return;
    }

    __afl_fork_pid = fork();
    if (__afl_fork_pid < 0) {
      PRINT_ERROR("Error on fork()\n");
      return;
    }
    if (__afl_fork_pid == 0) {
      close(FORKSRV_FD);
      close(FORKSRV_FD + 1);
      prev_id = 0;
      break;
    } else {
      // parrent stuff
      n = write(FORKSRV_FD + 1, &__afl_fork_pid, 4);
      pid_t temp_pid = waitpid(__afl_fork_pid, &__afl_temp_data, 2);

      if (temp_pid == 0) {
        return;
      }
      n = write(FORKSRV_FD + 1, &__afl_temp_data, 4);
    }
  }
}
