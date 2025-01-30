#pragma once

#include <Windows.h> // or <stdint.h>
#include "THREADSLib.h"

#define READY       1
#define RUNNING     2
#define BLOCKED     3
#define TERMINATED  4

#define WAIT_BLOCK  11
#define JOIN_BLOCK  12

// Check if our assignment calls for other states 0..10 internally and adapt as needed.

typedef struct _process
{
    int pid;                                     // unique process ID
    char name[THREADS_MAX_NAME];                 // up to 128 chars
    int priority;                                // 0..5
    int status;                                  // e.g. READY, RUNNING, BLOCKED, TERMINATED
    void* context;                               // CPU context for switching
    int (*entryPoint)(void*);                   // function pointer if you store it
    struct _process* pParent;
    struct _process* pChildren;
    struct _process* nextSiblingProcess;
    struct _process* nextReady;
    
    int signaledFlag;                            // 1 if SIG_TERM was sent, else 0
    int exitCode;                                // exit code to pass back to the parent
    DWORD startTime;                             // optional: track microseconds at spawn time
    int  cpuTime;                                // increments in time_slice
} Process;

