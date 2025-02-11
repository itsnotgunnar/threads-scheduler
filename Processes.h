#pragma once

#define STATUS_EMPTY            0
#define STATUS_READY            1
#define STATUS_RUNNING          2
#define STATUS_BLOCKED_WAIT     3
#define STATUS_EXITED           4
#define STATUS_BLOCKED_JOIN     5
#define STATUS_BLOCKED          6

typedef struct _process
{
    /* Next pointers for linked lists */
    struct _process* nextReadyProcess;     // for the ready queue
    struct _process* nextSiblingProcess;   // for siblings in parent's child list or exiting list

    /* Parent-child relationships */
    struct _process* pParent;             // parent process
    struct _process* pChildren;           // head of linked list of active children
    struct _process* pExitingChildren;    // head of linked list of children who exited but not collected

    char  name[MAXNAME];                  // Process name
    char  startArgs[MAXARG];              // Process arguments
    void* context;                        // Context pointer

    short pid;                            // Process ID
    int   priority;                       // Priority level
    int (*entryPoint)(void*);            // The code to run
    char* stack;                          // Stack pointer (if you’re explicitly allocating)
    unsigned int stacksize;               // Stack size

    int   status;                         // Current state
    int   exitCode;                       // Child's exit code if exited

    int   childCount;                     // Number of active children

    unsigned int cpuTime;                 // CPU usage for read_time()
    int   blockReason;                    // Used if you want to store reason in block()

} Process;
