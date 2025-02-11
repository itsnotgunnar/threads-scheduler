#pragma once

#define LOWEST_PRIORITY   0
#define HIGHEST_PRIORITY  5

#define MAXNAME           256
#define MAXARG            256
#define MAXPROC           50

/* Kill signals */
#define SIG_TERM			15

int bootstrap(void* pArgs);

typedef int (*check_io_function) ();
extern check_io_function check_io;

/* Functions that will become system calls. */
int  k_spawn(char* name, int(*entryPoint)(void*), void* arg, int stacksize, int priority);

#ifdef BUILD_DLL
__declspec(dllexport) void SchedulerSetEntryPoint(int(*entryPoint)(void*));
#endif

int   k_wait(int* pChildExitCode);
int   k_join(int pid, int* pChildExitCode);
int   k_kill(int pid, int signal);
void  k_exit(int exitCode);
int	  k_getpid(void);

/* Additional kernel-only functions. */
int	  signaled(void);
void  display_process_table(void);
int   block(int block_status);
int   unblock(int pid);
int   get_start_time(void);
void  time_slice(void);
int	  read_time(void);
DWORD read_clock(void);

/* Our dispatcher and queue-management prototypes: */
void  dispatcher(void);
void  k_addToReadyQueue(struct _process* p);
struct _process* k_getNextReadyProc(void);
