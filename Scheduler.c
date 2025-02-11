#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "THREADSLib.h"
#include "Scheduler.h"
#include "Processes.h"

Process processTable[MAX_PROCESSES];
Process* runningProcess = NULL;

Process* readyList[HIGHEST_PRIORITY + 1]; // Still using your simple one-pointer-per-priority model
int nextPid = 1;
int debugFlag = 0;

static int watchdog(char*);
static inline void disableInterrupts();
void dispatcher();
static int launch(void*);
static void check_deadlock();
static void DebugConsole(char* format, ...);

static int GetNextPid();
void AddToReadyList(Process* pProcess);

/* --- New helper function declarations --- */
static void AddToExitingChildren(Process* parent, Process* child);
static void RemoveChildFromList(Process* parent, Process* child);

static int processCount;
int booting = 1;

/* DO NOT REMOVE */
extern int SchedulerEntryPoint(void* pArgs);
int check_io_scheduler();
check_io_function check_io;

/*************************************************************************
   bootstrap()
*************************************************************************/
int bootstrap(void *pArgs)
{
    int result;

    check_io = check_io_scheduler;

    /* Initialize the process table */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        memset(&processTable[i], 0, sizeof(Process));
        processTable[i].status = STATUS_EMPTY;
    }
    processCount = 0;

    /* Initialize the ready list */
    for (int i = 0; i <= HIGHEST_PRIORITY; i++) {
        readyList[i] = NULL;
    }

    /* Initialize clock interrupt handler, etc. */

    /* Spawn watchdog process */
    result = k_spawn("watchdog", watchdog, NULL, THREADS_MIN_STACK_SIZE, LOWEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for watchdog returned an error (%d), stopping...\n", result);
        stop(1);
    }

    booting = 0;

    /* Spawn the test process */
    result = k_spawn("Scheduler", SchedulerEntryPoint, NULL, 2 * THREADS_MIN_STACK_SIZE, HIGHEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag,"Scheduler(): spawn for SchedulerEntryPoint returned an error (%d), stopping...\n", result);
        stop(1);
    }

    stop(-3);
    return 0;
}

/*************************************************************************
   check_kernel_mode()
*************************************************************************/
static inline void check_kernel_mode() {
    if ((get_psr() & PSR_KERNEL_MODE) == 0) {
        console_output(FALSE, "Kernel mode expected, but function called in user mode.\n");
        stop(1);
    }
}

/*************************************************************************
   k_spawn()
*************************************************************************/
int k_spawn(char* name, int (*entryPoint)(void*), void* arg, int stacksize, int priority)
{
    check_kernel_mode();

    if (priority < LOWEST_PRIORITY || priority > HIGHEST_PRIORITY) {
        console_output(FALSE, "spawn(): Priority out of range.\n");
        return -1;
    }
    if (stacksize < THREADS_MIN_STACK_SIZE) {
        console_output(FALSE, "spawn(): Stack size is too small\n");
        return -2;
    }

    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processTable[i].status == STATUS_EMPTY) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        console_output(FALSE, "spawn(): No free PCB slots, returning -1\n");
        return -1;
    }

    Process* pNewProc = &processTable[slot];
    memset(pNewProc, 0, sizeof(Process));
    strcpy(pNewProc->name, name);
    if (arg)
        strncpy(pNewProc->startArgs, (char*)arg, MAXARG - 1);
    pNewProc->pid = GetNextPid();
    pNewProc->priority = priority;
    pNewProc->entryPoint = entryPoint;
    pNewProc->status = STATUS_READY;
    pNewProc->exitCode = 0;

    /* Link to parent's child list and update childCount */
    pNewProc->pParent = runningProcess;
    if (runningProcess) {
       pNewProc->nextSiblingProcess = runningProcess->pChildren;
       runningProcess->pChildren = pNewProc;
       runningProcess->childCount++;
    }

    pNewProc->context = context_initialize(launch, stacksize, arg);

    /* Add to ready list using existing function */
    AddToReadyList(pNewProc);

    if (!booting)
        dispatcher();

    return pNewProc->pid;
}

/*************************************************************************
   launch()
*************************************************************************/
static int launch(void* args)
{
    int resultCode;
    DebugConsole("launch(): started: %s\n", runningProcess->name);

    /* TODO: Enable interrupts */

    resultCode = runningProcess->entryPoint(runningProcess->startArgs);

    DebugConsole("Process %d returned to launch\n", runningProcess->pid);

    k_exit(resultCode);

    return 0;
}

/*************************************************************************
   k_exit()
*************************************************************************/
void k_exit(int code)
{
    check_kernel_mode();

    Process* pParent = runningProcess->pParent;
    runningProcess->exitCode = code;
    runningProcess->status = STATUS_EXITED;

    if (pParent != NULL)
    {
        /* Decrement parent's active child count */
        if (pParent->childCount > 0)
            pParent->childCount--;

        /* Remove self from parent's active children list */
        RemoveChildFromList(pParent, runningProcess);

        /* Append self to parent's exiting children list */
        AddToExitingChildren(pParent, runningProcess);

        /* Unblock parent if it is waiting */
        if (pParent->status == STATUS_BLOCKED_WAIT)
        {
            AddToReadyList(pParent);
        }
    }
    else
    {
        memset(runningProcess, 0, sizeof(Process));
    }

    dispatcher();
}

/*************************************************************************
   k_wait()
*************************************************************************/
int k_wait(int* code)
{
    check_kernel_mode();

    /* If no active children remain, return error code (-4) */
    if (runningProcess->childCount == 0)
        return -4;

    /* Wait until at least one child is in the exiting list */
    while (runningProcess->pExitingChildren == NULL) {
        runningProcess->status = STATUS_BLOCKED_WAIT;
        dispatcher();
    }

    Process* child = runningProcess->pExitingChildren;
    runningProcess->pExitingChildren = child->nextSiblingProcess;
    child->nextSiblingProcess = NULL;

    *code = child->exitCode;
    int childPid = child->pid;

    /* Free the child's PCB by zeroing it out */
    memset(child, 0, sizeof(Process));

    return childPid;
}

/*************************************************************************
   k_kill()
*************************************************************************/
int k_kill(int pid, int signal)
{
    check_kernel_mode();
    // Stub: for now, just return 0.
    return 0;
}

/*************************************************************************
   k_getpid()
*************************************************************************/
int k_getpid()
{
    check_kernel_mode();
    return runningProcess ? runningProcess->pid : 0;
}

/*************************************************************************
   k_join()
*************************************************************************/
int k_join(int pid, int* pChildExitCode)
{
    check_kernel_mode();
    // Stub
    return 0;
}

/*************************************************************************
   unblock()
*************************************************************************/
int unblock(int pid)
{
    // Stub
    return 0;
}

/*************************************************************************
   block()
*************************************************************************/
int block(int newStatus)
{
    // Stub
    return 0;
}

/*************************************************************************
   signaled()
*************************************************************************/
int signaled()
{
    return 0;
}

/*************************************************************************
   read_time()
*************************************************************************/
int read_time()
{
    return 0;
}

/*************************************************************************
   read_clock()
*************************************************************************/
DWORD read_clock()
{
    return system_clock();
}

/*************************************************************************
   display_process_table()
*************************************************************************/
void display_process_table()
{
    // (This function should print the process table in the format expected.)
    console_output(FALSE, "PID     Parent   Priority  Status        # Kids   CPUtime  Name    \n");
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processTable[i].status != STATUS_EMPTY)
        {
            int parentPid = processTable[i].pParent ? processTable[i].pParent->pid : -1;
            console_output(FALSE, "%d    %d     %d     %d     %d    %u   %s\n",
                processTable[i].pid,
                parentPid,
                processTable[i].priority,
                processTable[i].status,
                processTable[i].childCount,
                processTable[i].cpuTime,
                processTable[i].name);
        }
    }
}

/*************************************************************************
   GetNextPid()
*************************************************************************/
int GetNextPid()
{
    int newPid = nextPid++;
    return newPid;
}

/*************************************************************************
   AddToReadyList()
*************************************************************************/
void AddToReadyList(Process* pProcess)
{
    pProcess->status = STATUS_READY;
    int priority = pProcess->priority;
    readyList[priority] = pProcess; // Simple: one process per priority (for now)
}

/*************************************************************************
   GetNextReadyProc()
*************************************************************************/
Process* GetNextReadyProc()
{
    int higherThanPriority = LOWEST_PRIORITY;
    Process* nextProcess = NULL;

    if (runningProcess != NULL && runningProcess->status == STATUS_RUNNING)
        higherThanPriority = runningProcess->priority;

    for (int i = HIGHEST_PRIORITY; i >= higherThanPriority; i--)
    {
        if (readyList[i] != NULL)
        {
            nextProcess = readyList[i];
            readyList[i] = NULL;
            break;
        }
    }
    return nextProcess;
}

/*************************************************************************
   dispatcher()
*************************************************************************/
void dispatcher()
{
    Process* nextProcess = GetNextReadyProc();
    if (nextProcess != NULL)
    {
        runningProcess = nextProcess;
        runningProcess->status = STATUS_RUNNING;
        context_switch(runningProcess->context);
    }
}

/*************************************************************************
   watchdog()
*************************************************************************/
static int watchdog(char* dummy)
{
    DebugConsole("watchdog(): called\n");
    while (1)
    {
        check_deadlock();
    }
    return 0;
}

/*************************************************************************
   check_deadlock()
*************************************************************************/
static void check_deadlock()
{
    console_output(FALSE, "All processes completed.\n");
    stop(0);
}

/*************************************************************************
   disableInterrupts()
*************************************************************************/
static inline void disableInterrupts()
{
    int psr = get_psr();
    psr &= ~PSR_INTERRUPTS;
    set_psr(psr);
}

/*************************************************************************
   DebugConsole()
*************************************************************************/
static void DebugConsole(char* format, ...)
{
    char buffer[2048];
    va_list argptr;
    if (debugFlag)
    {
        va_start(argptr, format);
        vsprintf(buffer, format, argptr);
        console_output(TRUE, buffer);
        va_end(argptr);
    }
}

/*************************************************************************
   check_io_scheduler()
*************************************************************************/
int check_io_scheduler()
{
    return false;
}

/*============================================================
   Helper: AddToExitingChildren()
   Appends the exiting child to parent's pExitingChildren list.
============================================================*/
static void AddToExitingChildren(Process* parent, Process* child)
{
    child->nextSiblingProcess = NULL;
    if (parent->pExitingChildren == NULL)
        parent->pExitingChildren = child;
    else {
        Process* p = parent->pExitingChildren;
        while (p->nextSiblingProcess != NULL)
            p = p->nextSiblingProcess;
        p->nextSiblingProcess = child;
    }
}

/*============================================================
   Helper: RemoveChildFromList()
   Removes the child from parent's pChildren list.
============================================================*/
static void RemoveChildFromList(Process* parent, Process* child)
{
    if (!parent)
        return;
    Process* prev = NULL;
    Process* cur = parent->pChildren;
    while (cur)
    {
        if (cur == child)
        {
            if (prev)
                prev->nextSiblingProcess = cur->nextSiblingProcess;
            else
                parent->pChildren = cur->nextSiblingProcess;
            cur->nextSiblingProcess = NULL;
            return;
        }
        prev = cur;
        cur = cur->nextSiblingProcess;
    }
}
