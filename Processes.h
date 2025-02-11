#pragma once

#define STATUS_EMPTY            0
#define STATUS_READY            1
#define STATUS_RUNNING          2
#define STATUS_BLOCKED_WAIT     3
#define STATUS_EXITED           4
#define STATUS_BLOCKED_JOIN     5
#define STATUS_BLOCKED          6

#define MAXNAME           256
#define MAXARG            256
#define MAXPROC           50

typedef struct _process {
    /* Pointers for list management */
    struct _process* nextReadyProcess;    // For ready queue
    struct _process* nextSiblingProcess;  // For parent's active children list
    struct _process* pExitingNext;        // For parent's exited children list

    /* Parent/child relationships */
    struct _process* pParent;    // Pointer to parent process
    struct _process* pChildren;  // Head of linked list of active children

    char name[MAXNAME];          // Process name
    char startArgs[MAXARG];      // Process arguments
    void* context;               // Process's context pointer
    short pid;                   // Process id
    int priority;                // Process priority
    int (*entryPoint)(void*);    // Entry point function
    char* stack;                 // Stack pointer (if allocated)
    unsigned int stacksize;      // Stack size

    int joined;  // NEW: 0 means not yet joined, 1 means joined.

    int status;                  // Process status
    int exitCode;                // Exit code when process finishes
    int childCount;              // Number of active children
    unsigned int cpuTime;        // CPU time (for read_time())
    int blockReason;             // For block() calls
} Process;
