
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include "THREADSLib.h"
#include "Scheduler.h"
#include "Processes.h"

Process processTable[MAX_PROCESSES];
Process *runningProcess = NULL;

//Process* readyList[HIGHEST_PRIORITY + 1]; // One list per priority
List readyList;
int nextPid = 1;
int debugFlag = 0;

static int watchdog(char*);
static inline void disableInterrupts();
void dispatcher();
static int launch(void *);
static void check_deadlock();
static void DebugConsole(char* format, ...);

static int GetNextPid();
Process* GetNextReadyProc();
static int processCount;
void timer_interrupt_handler(char deviceId[32], uint8_t command, uint32_t status);

static void ListInitialize(List* pList);
static void ListAddNode(List* pList, Process* pProcToAdd);
//void AddToReadyList(Process* pProcess);

int booting = 1;

/* DO NOT REMOVE */
extern int SchedulerEntryPoint(void* pArgs);
int check_io_scheduler();
check_io_function check_io;


/*************************************************************************
   bootstrap()

   Purpose - This is the first function called by THREADS on startup.

             The function must setup the OS scheduler and primitive
             functionality and then spawn the first two processes.  
             
             The first two process are the watchdog process 
             and the startup process SchedulerEntryPoint.  
             
             The statup process is used to initialize additional layers
             of the OS.  It is also used for testing the scheduler 
             functions.

   Parameters - Arguments *pArgs - these arguments are unused at this time.

   Returns - The function does not return!

   Side Effects - The effects of this function is the launching of the kernel.

 *************************************************************************/
int bootstrap(void *pArgs)
{
    int result; /* value returned by call to spawn() */

    /* set this to the scheduler version of this function.*/
    check_io = check_io_scheduler;

    /* Initialize the process table. */

    /* Initialize the Ready list, etc. */
    ListInitialize(&readyList);

    /* Initialize the clock interrupt handler */
    //intVector = get_interrupt_handlers();
    //intVector[THREADS_TIMER_INTERRUPT] = timer_interrupt_handler;

    /* startup a watchdog process */
    result = k_spawn("watchdog", watchdog, NULL, THREADS_MIN_STACK_SIZE, LOWEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for watchdog returned an error (%d), stopping...\n", result);
        stop(1);
    }

    booting = 0;

    /* start the test process, which is the main for each test program.  */
    result = k_spawn("Scheduler", SchedulerEntryPoint, NULL, 2 * THREADS_MIN_STACK_SIZE, HIGHEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag,"Scheduler(): spawn for SchedulerEntryPoint returned an error (%d), stopping...\n", result);
        stop(1);
    }

    /* Initialized and ready to go!! */

    /* This should never return since we are not a real process. */

    stop(-3);
    return 0;

}

/*************************************************************************
   k_spawn()

   Purpose - spawns a new process.
   
             Finds an empty entry in the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.

   Parameters - the process's entry point function, the stack size, and
                the process's priority.

   Returns - The Process ID (pid) of the new child process 
             The function must return if the process cannot be created.

************************************************************************ */
int k_spawn(char* name, int (*entryPoint)(void *), void* arg, int stacksize, int priority)
{
    int proc_slot;
    int myPid;
    struct _process* pNewProc;

    DebugConsole("spawn(): creating process %s\n", name);

    disableInterrupts();

    /* Validate all of the parameters, starting with the name. */
    if (name == NULL)
    {
        console_output(debugFlag, "spawn(): Name value is NULL.\n");
        return -1;
    }
    if (strlen(name) >= (MAXNAME - 1))
    {
        console_output(debugFlag, "spawn(): Process name is too long.  Halting...\n");
        stop( 1);
    }


    /* Find an empty slot in the process table */
    int i;
    for (i = nextPid; processTable[i].status != 0; ++i);

    myPid = GetNextPid();
    //myPid = 2; // Changing this to 3 causes access violation inside dispatcher
    
    proc_slot = myPid % MAX_PROCESSES;
    //proc_slot = 1;  // just use 1 for now!
    pNewProc = &processTable[proc_slot];

    /* Setup the entry in the process table. */
    strcpy(pNewProc->name, name);

    if (arg != NULL)
    {
        strcpy(pNewProc->startArgs, (char*)arg);
    }

    pNewProc->pid = myPid;
    pNewProc->priority = priority;
    pNewProc->pParent = runningProcess;
    pNewProc->status = STATUS_READY;
    pNewProc->entryPoint = entryPoint;

    /* If there is a parent process,add this to the list of children. */
    if (runningProcess != NULL)
    {
    }

    /* Add the process to the ready list. */
    //AddToReadyList(pNewProc);
    ListAddNode(&readyList, pNewProc);

    /* Initialize context for this process, but use launch function pointer for
     * the initial value of the process's program counter (PC)
    */
    pNewProc->context = context_initialize(launch, stacksize, arg);

    if (!booting)
        dispatcher();

    return pNewProc->pid;


} /* spawn */

/**************************************************************************
   Name - launch

   Purpose - Utility function that makes sure the environment is ready,
             such as enabling interrupts, for the new process.  

   Parameters - none

   Returns - nothing
*************************************************************************/
static int launch(void *args)
{
    int resultCode;
    DebugConsole("launch(): started: %s\n", runningProcess->name);

    /* TODO: Enable interrupts */


    /* Call the function passed to spawn and capture its return value */
    resultCode = runningProcess->entryPoint(runningProcess->startArgs);

    DebugConsole("Process %d returned to launch\n", runningProcess->pid);

    /* Stop the process gracefully */
    k_exit(resultCode);

    return 0;
} 

/**************************************************************************
   Name - k_wait

   Purpose - Wait for a child process to quit.  Return right away if
             a child has already quit.

   Parameters - Output parameter for the child's exit code. 

   Returns - the pid of the quitting child, or
        -4 if the process has no children
        -5 if the process was signaled in the join

************************************************************************ */
int k_wait(int* code)
{
    int result = 0;
    Process* pExitingChild;

    // Change my state to BLOCKED
    runningProcess->status = STATUS_BLOCKED_WAIT;

    dispatcher();

    // Interrupts are enabled here

    // Get the exit code of the child
    // TODO: Pop first child off of exiting child list
    pExitingChild = runningProcess->pExitingChildren;

    if (pExitingChild != NULL)
    {
        *code = pExitingChild->exitCode;
        result = pExitingChild->pid;
    }

    // Clean up after the child
    memset(pExitingChild, 0, sizeof(Process));

    return result;

} 

/**************************************************************************
   Name - k_exit

   Purpose - Exits a process and coordinates with the parent for cleanup 
             and return of the exit code.

   Parameters - the code to return to the grieving parent

   Returns - nothing
   
*************************************************************************/
void k_exit(int code)
{
    Process* pParent;

    // TODO: Disable interrupts

    pParent = runningProcess->pParent;

    // Wake up the parent process only if they're in k_wait
    if (pParent != NULL)
    {
        if (pParent->status == STATUS_BLOCKED_WAIT)
        {
            //AddToReadyList(pParent);
            ListAddNode(&readyList, pParent);
        }

        // Put parent process on the ready list
        runningProcess->exitCode = code;
        runningProcess->status = STATUS_EXITED;

        // Add myself to the quit children list of the parent
        // TODO: Make this a list of children
        pParent->pExitingChildren = runningProcess;
    }
    else
    {
        // Reset the main entry in the process table
        // ClearPCB();
        memset(runningProcess, 0, sizeof(Process));
    }

    dispatcher();

}

/**************************************************************************
   Name - k_kill

   Purpose - Signals a process with the specified signal

   Parameters - Signal to send

   Returns -
*************************************************************************/
int k_kill(int pid, int signal)
{
    int result = 0;
    return 0;
}

/**************************************************************************
   Name - k_getpid
*************************************************************************/
int k_getpid()
{
    return 0;
}

/**************************************************************************
   Name - k_join
***************************************************************************/
int k_join(int pid, int* pChildExitCode)
{
    return 0;
}

/**************************************************************************
   Name - unblock
*************************************************************************/
int unblock(int pid)
{
    return 0;
}

/*************************************************************************
   Name - block
*************************************************************************/
int block(int newStatus)
{
    //checkKernelMode(__func__);

    int result = 0;

    if (newStatus <= 10)
    {
        console_output(false, "block: function called with a reserved status value.\n");
        stop(1);
    }

    disableInterrupts();
    runningProcess->status = newStatus;

    dispatcher(FALSE);

    disableInterrupts();
    if (signaled())
    {
        DebugConsole("block(): Process signaled while blocked()\n");
        result = -5;
    }
    //enableInterrupts();

    return 0;
}

/*************************************************************************
   Name - signaled
*************************************************************************/
int signaled()
{
    return 0;
}
/*************************************************************************
   Name - readtime
*************************************************************************/
int read_time()
{
    return 0;
}

/*************************************************************************
   Name - readClock
*************************************************************************/
DWORD read_clock()
{
    return system_clock();
}

void display_process_table()
{

}

/**************************************************************************
   Name - GetNextPid

   Purpose - 

   Parameters - none

   Returns - 

*************************************************************************/
int GetNextPid()
{
    int newPid = -1;
    int procSlot = nextPid % MAXPROC;

    if (processCount < MAXPROC)
    {
        while (processCount < MAXPROC && processTable[procSlot].status != STATUS_EMPTY)
        {
            nextPid++;
            procSlot = nextPid % MAXPROC;
        }
        newPid = nextPid++;
    }
    return newPid;
}

/* ---------------------------------------------------------------
    ListInitialize

    Purpose - Initialize a List type
    Parameters - nextPrevOffset - offset from beginning of
                structure to the next and previous pointers
                with the structure that makes up the nodes
    Returns - None
    Side Effects -
--------------------------------------------------------------- */
static void ListInitialize(List* pList)
{
    pList->pHead = pList->pTail = NULL;
    pList->count = 0;
}

/* ---------------------------------------------------------------
    ListAddNode

    Purpose - Adds a node to the end of the list
    Parameters - List *pList - pointer to the list
                TestStructure *pStructToAdd - pointer
                to the structure to add
    Returns - None
    Side Effects -
--------------------------------------------------------------- */
static void ListAddNode(List* pList, Process* pProcToAdd)
{
    int listOffset;

    pProcToAdd->nextReadyProcess = NULL;

    if (pList->pHead == NULL)
    {
        // Set both the head and the tail pointer to the new node
        pList->pHead = pList->pTail = pProcToAdd;
    }
    else
    {
        // Move the tail after pointing to it with current tail's pNext
        pList->pTail->nextReadyProcess = pProcToAdd;
        pList->pTail = pProcToAdd;
    }
    pList->count++;
}

/* ---------------------------------------------------------------
    ListPopNodeEvens

    Purpose - Removes the first node from the list and returns
                a pointer to it
    Parameters - List *pList - pointer to the list
    Returns - A pointer to the removed node
    Side Effects -
--------------------------------------------------------------- */
static Process* ListPopNode(List* pList)
{
    Process* pNode = NULL;

    if (pList->count > 0)
    {
        pNode = pList->pHead;
        pList->pHead = pNode->nextReadyProcess;
        pList->count--;

        // Clear prev and next
        pNode->nextReadyProcess = NULL;

        // Clear the tail pointer if the list is now empty
        if (pList->count == 0)
        {
            pList->pHead = pList->pTail = NULL;
        }
    }
    return pNode;
}

/**************************************************************************
   Name - AddToReadyList

   Purpose -

   Parameters - none

   Returns - nothing

*************************************************************************/
void AddToReadyList(Process* pProcess)
{
    int priority;

    pProcess->status = STATUS_READY;

    priority = pProcess->priority;

    // Add to the ready list based on priority
    //readyList[priority] = pProcess; // Add to tail of list
    ListAddNode(&readyList, &pProcess);
}

/**************************************************************************
   Name - GetNextReadyProc

   Purpose - 

   Parameters - none

   Returns - nothing

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
        if (readyList.count > 0)
        {
            //nextProcess = readyList[i]; // Pop from ready list
            //readyList[i] = NULL; // Pop simulating
            nextProcess = ListPopNode(&readyList);
            break;
        }
    }

    return nextProcess;
}

/**************************************************************************
   Name - time_slice

   Purpose -

   Parameters - none

   Returns - nothing

*************************************************************************/
void  time_slice(void)
{
    // If the current process has been running for 80ms or more, do something

}

/**************************************************************************
   Name - timer_interrupt_handler

   Purpose -

   Parameters - none

   Returns - nothing

*************************************************************************/
void timer_interrupt_handler(char deviceId[32], uint8_t command, uint32_t status)
{
    time_slice();
}

/**************************************************************************
   Name - dispatcher

   Purpose - This is where context changes to the next process to run.

   Parameters - none

   Returns - nothing

*************************************************************************/
void dispatcher()
{
    Process *nextProcess = NULL;

    nextProcess = GetNextReadyProc();
    //nextProcess = &processTable[2];

    // Next process is null if the current process should remain running
    if (nextProcess != NULL)
    {
        /* IMPORTANT: context switch enables interrupts. */
        runningProcess = nextProcess;

        // Set the status of the next process to running
        runningProcess->status = STATUS_RUNNING;

        context_switch(runningProcess->context);
    }

} 

/**************************************************************************
   Name - watchdog

   Purpose - The watchdoog keeps the system going when all other
         processes are blocked.  It can be used to detect when the system
         is shutting down as well as when a deadlock condition arises.

   Parameters - none

   Returns - nothing
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

/* check to determine if deadlock has occurred... */
static void check_deadlock()
{
    // TODO: If there are no other processes in the system, then stop
    console_output(false, "All processes completed.");
    stop(0);
}

/*
 * Disables the interrupts.
 */
static inline void disableInterrupts()
{

    /* We ARE in kernel mode */


    int psr = get_psr();

    psr = psr & ~PSR_INTERRUPTS;

    set_psr( psr);

} /* disableInterrupts */

/**************************************************************************
   Name - DebugConsole
   Purpose - Prints  the message to the console_output if in debug mode
   Parameters - format string and va args
   Returns - nothing
   Side Effects -
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

/**************************************************************************
   Name - CheckKernelMode

   Purpose - Checks the PSR for kernel mode
   and halts if in user mode

   Parameters -

   Returns -

*************************************************************************/
static inline void CheckKernelMode(const char *functionName)
{

}


/* there is no I/O yet, so return false. */
int check_io_scheduler()
{
    return false;
}