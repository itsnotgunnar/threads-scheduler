#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <string.h>
#include "THREADSLib.h"
#include "Scheduler.h"
#include "Processes.h"

Process processTable[MAX_PROCESSES];
Process* runningProcess = NULL;

/* Single pointer for each priority, as before. */
Process* readyList[HIGHEST_PRIORITY + 1];

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

static int processCount;
int booting = 1;

/* DO NOT REMOVE */
extern int SchedulerEntryPoint(void* pArgs);

/* If you need a "check_io" function pointer, keep as is: */
int check_io_scheduler();
check_io_function check_io;

/*************************************************************************
 * bootstrap()
 *************************************************************************/
int bootstrap(void* pArgs)
{
    int result;

    /* set this to the scheduler version of this function. */
    check_io = check_io_scheduler;

    /* Initialize the process table, etc. */
    // e.g., set them all to STATUS_EMPTY
    for (int i = 0; i < MAX_PROCESSES; i++) {
        memset(&processTable[i], 0, sizeof(Process));
        processTable[i].status = STATUS_EMPTY;
    }
    processCount = 0;

    /* Initialize the Ready list, etc. */
    for (int i = 0; i <= HIGHEST_PRIORITY; i++) {
        readyList[i] = NULL;
    }

    /* Initialize the clock interrupt handler if needed. */

    /* Start a watchdog process */
    result = k_spawn("watchdog", watchdog, NULL, THREADS_MIN_STACK_SIZE, LOWEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag,
            "Scheduler(): spawn for watchdog returned an error (%d), stopping...\n",
            result);
        stop(1);
    }

    booting = 0;

    /* Start the test process (SchedulerEntryPoint) */
    result = k_spawn("Scheduler", SchedulerEntryPoint, NULL, 2 * THREADS_MIN_STACK_SIZE, HIGHEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag,
            "Scheduler(): spawn for SchedulerEntryPoint returned an error (%d), stopping...\n",
            result);
        stop(1);
    }

    /* Should never return */
    stop(-3);
    return 0;
}

static inline void check_kernel_mode()
{
    if ((get_psr() & PSR_KERNEL_MODE) == 0)
    {
        console_output(FALSE, "Kernel mode expected, but function called in user mode.\n");
        stop(1);
    }
}

/*************************************************************************
 * k_spawn()
 *************************************************************************/
int k_spawn(char* name, int (*entryPoint)(void*), void* arg, int stacksize, int priority)
{
    check_kernel_mode();

    // Validate priority & stacksize
    if (priority < LOWEST_PRIORITY || priority > HIGHEST_PRIORITY)
    {
        console_output(FALSE, "spawn(): Priority out of range.\n");
        return -1;
    }
    if (stacksize < THREADS_MIN_STACK_SIZE)
    {
        console_output(FALSE, "spawn(): Stack size is too small\n");
        return -2;
    }

    // Find a free slot in the process table
    int slot = -1;
    for (int i = 0; i < MAXPROC; i++)
    {
        if (processTable[i].status == STATUS_EMPTY)
        {
            slot = i;
            break;
        }
    }
    if (slot == -1)
    {
        console_output(FALSE, "spawn(): No free PCB slots, returning -1\n");
        return -1;
    }

    // Initialize the PCB
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

    /* Link to parent's child list if we have a running process. */
    pNewProc->pParent = runningProcess;
    if (runningProcess)
    {
        pNewProc->nextSiblingProcess = runningProcess->pChildren;
        runningProcess->pChildren = pNewProc;
    }

    // context init
    pNewProc->context = context_initialize(launch, stacksize, arg);

    /* Add to ready list. */
    AddToReadyList(pNewProc);

    /* If not booting, dispatch. */
    if (!booting)
        dispatcher();

    return pNewProc->pid;
}

/*************************************************************************
 * launch()
 *************************************************************************/
static int launch(void* args)
{
    int resultCode;
    DebugConsole("launch(): started: %s\n", runningProcess->name);

    // TODO: Enable interrupts

    // call the entry point
    resultCode = runningProcess->entryPoint(runningProcess->startArgs);

    DebugConsole("Process %d returned to launch\n", runningProcess->pid);
    k_exit(resultCode);

    return 0;
}

/*************************************************************************
 * k_wait()
 *************************************************************************/
int k_wait(int* code)
{
    check_kernel_mode();

    int result = 0;
    Process* pExitingChild;

    // block self
    runningProcess->status = STATUS_BLOCKED_WAIT;

    dispatcher();

    // once unblocked, presumably the parent has a pExitingChild
    pExitingChild = runningProcess->pExitingChildren;

    if (pExitingChild != NULL)
    {
        *code = pExitingChild->exitCode;
        result = pExitingChild->pid;

        // clean up
        memset(pExitingChild, 0, sizeof(Process));
    }

    return result;
}

/*************************************************************************
 * k_exit()
 *************************************************************************/
void k_exit(int code)
{
    check_kernel_mode();

    Process* pParent = runningProcess->pParent;
    runningProcess->exitCode = code;
    runningProcess->status = STATUS_EXITED;

    if (pParent != NULL)
    {
        if (pParent->status == STATUS_BLOCKED_WAIT)
        {
            // "unblock" parent by placing it back on ready list
            AddToReadyList(pParent);
        }

        // Add this exiting child to parent's pExitingChildren
        pParent->pExitingChildren = runningProcess;
    }
    else
    {
        // no parent, free up
        memset(runningProcess, 0, sizeof(Process));
    }

    dispatcher();
}

/*************************************************************************
 * k_kill()
 *************************************************************************/
int k_kill(int pid, int signal)
{
    check_kernel_mode();

    // stub
    return 0;
}

/*************************************************************************
 * k_getpid()
 *************************************************************************/
int k_getpid()
{
    check_kernel_mode();
    // for now, return 0 or the actual runningProcess->pid
    return runningProcess ? runningProcess->pid : 0;
}

/*************************************************************************
 * k_join()
 *************************************************************************/
int k_join(int pid, int* pChildExitCode)
{
    check_kernel_mode();

    // stub
    return 0;
}

/*************************************************************************
 * unblock()
 *************************************************************************/
int unblock(int pid)
{
    // stub
    return 0;
}

/*************************************************************************
 * block()
 *************************************************************************/
int block(int newStatus)
{
    // stub
    return 0;
}

/*************************************************************************
 * signaled()
 *************************************************************************/
int signaled()
{
    return 0;
}

/*************************************************************************
 * read_time()
 *************************************************************************/
int read_time()
{
    return 0;
}

/*************************************************************************
 * read_clock()
 *************************************************************************/
DWORD read_clock()
{
    return system_clock();
}

/*************************************************************************
 * display_process_table()
 *************************************************************************/
void display_process_table()
{
    // stub
}

/*************************************************************************
 * GetNextPid()
 *************************************************************************/
int GetNextPid()
{
    int newPid = -1;
    int procSlot = nextPid % MAXPROC;

    if (processCount < MAXPROC)
    {
        while (processCount < MAXPROC &&
            processTable[procSlot].status != STATUS_EMPTY)
        {
            nextPid++;
            procSlot = nextPid % MAXPROC;
        }
        newPid = nextPid++;
        processCount++;
    }
    return newPid;
}

/*************************************************************************
 * AddToReadyList()
 *
 * Currently just storing a single process pointer per priority.
 * If you need multiple, you'd implement a queue.
 *************************************************************************/
void AddToReadyList(Process* pProcess)
{
    pProcess->status = STATUS_READY;
    int priority = pProcess->priority;
    readyList[priority] = pProcess;
}

/*************************************************************************
 * GetNextReadyProc()
 *************************************************************************/
Process* GetNextReadyProc()
{
    int higherThanPriority = LOWEST_PRIORITY;
    Process* nextProcess = NULL;

    if (runningProcess != NULL && runningProcess->status == STATUS_RUNNING)
    {
        higherThanPriority = runningProcess->priority;
    }

    for (int i = HIGHEST_PRIORITY; i >= higherThanPriority; --i)
    {
        if (readyList[i] != NULL)
        {
            nextProcess = readyList[i];
            readyList[i] = NULL; // pop
            break;
        }
    }

    return nextProcess;
}

/*************************************************************************
 * dispatcher()
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
 * watchdog()
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
 * check_deadlock()
 *************************************************************************/
static void check_deadlock()
{
    console_output(false, "All processes completed.");
    stop(0);
}

/*************************************************************************
 * disableInterrupts()
 *************************************************************************/
static inline void disableInterrupts()
{
    int psr = get_psr();
    psr = psr & ~PSR_INTERRUPTS;
    set_psr(psr);
}

/*************************************************************************
 * DebugConsole()
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
 * check_io_scheduler()
 *************************************************************************/
int check_io_scheduler()
{
    return false;
}
