
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <string.h>    // For memset, strncpy, etc.
#include <stdarg.h>    // For va_list, vsprintf in DebugConsole

#include "THREADSLib.h"
#include "Scheduler.h"
#include "Processes.h"

/*****************************************************************************
 *                            GLOBAL DATA & DEFINES
 *****************************************************************************/

Process processTable[MAX_PROCESSES];
Process* runningProcess = NULL;
int nextPid = 1;
int debugFlag = 1;

// The standard time-slice quantum in milliseconds (80ms).
#define TIME_SLICE_QUANTUM 80

// Block statuses
#define WAIT_BLOCK  11
#define JOIN_BLOCK  12

/*****************************************************************************
 *                        FORWARD DECLARATIONS (HELPERS)
 *****************************************************************************/

 // Because we don't define set_interrupt_handler in THREADSLib.h
static void set_interrupt_handler(int interruptNumber, void (*handler)(void));

static void set_interrupt_handler(int interruptNumber, void (*handler)(void))
{
    interrupt_handler_t* vect = get_interrupt_handlers();
    vect[interruptNumber] = (interrupt_handler_t)handler;
}
// The ready queue functions
void ready_queue_init(void);
void add_to_ready_queue(Process* proc);
Process* get_highest_priority_ready_process(void);

// Finds an unused slot in processTable. Returns the index if found, else -1.
static int findFreeProcessSlot(void);

// Returns a pointer to the process with matching pid, or NULL if not found.
static Process* findProcessByPid(int pid);

// Removes 'child' from its parent's linked list of children.
static void removeChildFromParentList(Process* child);

// Returns a pointer to any terminated child of 'parent' (status == TERMINATED).
// If none is found, returns NULL.
static Process* findTerminatedChild(Process* parent);

// Counts how many children a process has.
static int countChildren(Process* parent);

// Logs debug messages to the console if debugFlag is set.
static void DebugConsole(char* format, ...);

// Disables all interrupts by manipulating the PSR register. (we're in kernel mode)
static inline void disableInterrupts();

// Re-enables interrupts (inverse of disableInterrupts()).
static inline void enableInterrupts();

// This is the function that a newly spawned process actually starts in. A trampoline that enables interrupts and then calls the real entry point.
static int launch(void* args);

// Helper that checks for deadlock in the system (watchdog usage). Currently empty, but can implement in the future as needed.
static void check_deadlock();

// This is the watchdog process's main function. Runs forever, possibly checking for deadlock or system issues.
static int watchdog(char* dummy);
void dispatcher();

/* DO NOT REMOVE */
extern int SchedulerEntryPoint(void* pArgs);
int check_io_scheduler();
check_io_function check_io;


/*************************************************************************
                           SCHEDULER ENTRYPOINT

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
int bootstrap(void* pArgs)
{
    int result; /* value returned by call to spawn() */

    // Let the "Scheduler" version of check_io be set up. We might only need the timer interrupt, but we point to a dummy function here
    check_io = check_io_scheduler;

    // Clear out the global process table, marking them as unused (status=0).
    memset(processTable, 0, sizeof(processTable));

    // Initialize your ready queues, if you have multiple or a single queue.
    ready_queue_init();

    // Install the timer interrupt handler to time_slice() only.
    // The other interrupts (IO, exception, syscalls) can remain NULL or be set in the future.
    set_interrupt_handler(THREADS_TIMER_INTERRUPT, time_slice);
    set_interrupt_handler(THREADS_IO_INTERRUPT, NULL);
    set_interrupt_handler(THREADS_EXCEPTION_INTERRUPT, NULL);
    set_interrupt_handler(THREADS_SYS_CALL_INTERRUPT, NULL);

    // Spawn the watchdog process at the lowest priority (0).
    result = k_spawn("watchdog", watchdog, NULL, THREADS_MIN_STACK_SIZE, LOWEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for watchdog returned an error (%d), stopping...\n", result);
        stop(1);
    }

    // Spawn the main scheduler test harness (SchedulerEntryPoint) at the highest priority (5).
    // The "test" process that exercises your kernel's system calls.
    result = k_spawn("Scheduler", SchedulerEntryPoint, NULL, 2 * THREADS_MIN_STACK_SIZE, HIGHEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for SchedulerEntryPoint returned an error (%d), stopping...\n", result);
        stop(1);
    }

    // Once the bootstrap is done, we have at least two processes (watchdog + test).
    // There's no reason to continue here. The CPU will start running one of the
    // new processes (once we do a context switch).

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
int k_spawn(char* name, int (*entryPoint)(void*), void* arg, int stack_size, int priority)
{

    int proc_slot;

    DebugConsole("spawn(): creating process %s\n", name);

    disableInterrupts();

    /* Validate all of the parameters, starting with the name. */

    // Check if process name is NULL
    // If name is NULL, strncpy(pNewProc->name, name, MAXNAME); would cause a segmentation fault.
    if (name == NULL)
    {
        console_output(debugFlag, "spawn(): Name value is NULL.\n");
        enableInterrupts();
        return -1;
    }

    // Check if entryPoint is NULL
    // If entryPoint is NULL, the process would have no valid function to execute, leading to undefined behavior when it tries to run.
    if (entryPoint == NULL)
    {
        console_output(debugFlag, "spawn(): entryPoint value is NULL.\n");
        return -1;
    }

    // Ensure name is not too large, 128
    if (strlen(name) >= (THREADS_MAX_NAME))
    {
        console_output(debugFlag, "spawn(): Process name is too long.  Halting...\n");
        return -1;
    }

    // Check 'arg' if not NULL, ensure it is also < THREADS_MAX_NAME in length
    if (arg != NULL) {
        if (strlen((char*)arg) >= THREADS_MAX_NAME) {
            // also must halt kernel per specs
            stop(1); // never returns
        }
    }

    // Ensure priority is within the allowed range (0 - 5)
    if (priority < 0 || priority > 5)
    {
        console_output(debugFlag, "spawn(): Priority out of range.\n");
        return -3;
    }

    // The assignment says -2 if out of range. 
    // We want at least THREADS_MIN_STACK_SIZE
    if (stack_size < THREADS_MIN_STACK_SIZE) {
        console_output(debugFlag, "spawn(): Minimum stack size not met.\n");
        enableInterrupts();
        return -2;
    }

    // Find a free slot in the process table
    int slot = findFreeProcessSlot();
    if (slot < 0) {
        console_output(debugFlag, "spawn(): No space in stack.\n");
        enableInterrupts();
        return -4;
    }

    // Initialize the new process
    Process* newProc = &processTable[slot];
    memset(newProc, 0, sizeof(Process)); // clear it out

    // The assignment states the new PID is returned upon success.
    // We can use nextPid or the slot index, up to us 
    newProc->pid = nextPid++;
    strncpy(newProc->name, name, THREADS_MAX_NAME);
    newProc->priority = priority;
    newProc->status = READY;  // newly created => READY
    newProc->context = NULL;   // set later with context_initialize
    newProc->entryPoint = entryPoint; // sometimes optional to store
    newProc->signaledFlag = 0;
    newProc->exitCode = 0;
    newProc->startTime = read_clock(); // capture a start timestamp
    newProc->cpuTime = 0;  // no CPU usage yet

    // The new process's parent is the current runningProcess (except for watchers from bootstrap).
    newProc->pParent = runningProcess;
    newProc->pChildren = NULL;
    newProc->nextSiblingProcess = NULL; // will link below

    // Initialize the process's context. 
    // We pass 'launch' as the function the CPU will jump to first, 
    // and 'arg' as the parameter. Inside 'launch', we actually call entryPoint.
    newProc->context = context_initialize(launch, stack_size, arg);
    if (!newProc->context) {
        // Some error in context creation (unlikely if THREADS library is correct).
        // Return -1 or do stop(1), depending on our design. We can do -1 for now.
        enableInterrupts();
        return -1;
    }

    // Link it into the parent's child list (if we have a running parent).
    if (runningProcess != NULL) {
        newProc->nextSiblingProcess = runningProcess->pChildren;
        runningProcess->pChildren = newProc;
    }

    // Put the new process on the ready queue so dispatcher can schedule it.
    add_to_ready_queue(newProc);

    enableInterrupts();

    // Return the new child's pid on success
    return newProc->pid;


} /* spawn */

/**************************************************************************
   Name - launch

   Purpose - Utility function that makes sure the environment is ready,
             such as enabling interrupts, for the new process.

   Parameters - none

   Returns - nothing
*************************************************************************/
static int launch(void* args)
{

    // By the time we get here, 'runningProcess' should be set to
    // the process that is starting. Let's confirm in debug.
    DebugConsole("launch(): process %s (PID %d) is starting.\n", runningProcess->name, runningProcess->pid);

    // So the process can be preempted if needed.
    enableInterrupts();

    // Call the real entry point function of this process.
    // The 'args' pointer is what we passed to context_initialize.
    // Here, we'll retrieve the function pointer from the process table,
    // or you could have passed it directly. 
    int (*actualEntry)(void*) = runningProcess->entryPoint;
    int returnCode = 0;
    if (actualEntry) {
        returnCode = actualEntry(args);
        // We shouldn't expect user processes to return from main
        //  but if it does we'll call k_exit with that code.
    }

    DebugConsole("launch(): process %s returned with code %d\n", runningProcess->name, returnCode);

    // If a process returns from its main entry, we consider that an exit.
    k_exit(returnCode);

    // Should never reach here
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
int k_wait(int* p_child_exit_code)
{
    disableInterrupts();

    // If the calling process has no children return -4
    if (runningProcess->pChildren == NULL) {
        enableInterrupts();
        return -4;
    }

    // See if any child is already TERMINATED, if it is, return that child's info
    Process* deadKid = findTerminatedChild(runningProcess);
    if (deadKid != NULL) {
        // We found one child that is done. 
        int childPid = deadKid->pid;
        *p_child_exit_code = deadKid->exitCode;

        // Unlink that deadKid from parent's child list 
        removeChildFromParentList(deadKid);

        enableInterrupts();
        return childPid;
    }

    // Otherwise, we must block until a child terminates. 
    // We'll block with status=WAIT_BLOCK (which is > 10).
    enableInterrupts();
    // block(...) can re-enable or call dispatcher, so let's separate the calls.

    int blockResult = block(WAIT_BLOCK);
    // block() will call dispatcher() => we'll eventually resume here after 'unblock' or something.

    // When we resume here, we check if we were signaled while blocked.
    if (blockResult == -5) {
        // Means signaled => no child info
        return -5;
    }

    // If not signaled, presumably a child is now terminated. 
    // Let's find which child terminated:
    disableInterrupts();
    deadKid = findTerminatedChild(runningProcess);
    if (!deadKid) {
        // It's possible no child is found => some inconsistent state, or 
        // the child might have been reaped by a prior call? 
        // We'll just return -1 here. 
        enableInterrupts();
        return -1;
    }

    int cpid = deadKid->pid;
    *p_child_exit_code = deadKid->exitCode;
    removeChildFromParentList(deadKid);

    enableInterrupts();
    return cpid;
}

/**************************************************************************
   Name - k_exit

   Purpose - Exits a process and coordinates with the parent for cleanup
             and return of the exit code.

   Parameters - the code to return to the grieving parent

   Returns - nothing

*************************************************************************/
void k_exit(int exit_code)
{
    disableInterrupts();

    // If we still have children, the spec says we cannot exit => halt(1).
    if (runningProcess->pChildren != NULL) {
        stop(1); // never returns
    }

    // Mark ourselves as terminated
    runningProcess->status = TERMINATED;

    // If the process was signaled, override exit code to -5
    if (runningProcess->signaledFlag) {
        runningProcess->exitCode = -5;
    }
    else {
        runningProcess->exitCode = exit_code;
    }

    // Possibly unblock the parent if it's waiting or joining
    if (runningProcess->pParent)
    {
        // If parent is blocked (WAIT_BLOCK or JOIN_BLOCK), set it READY
        if (runningProcess->pParent->status == WAIT_BLOCK ||
            runningProcess->pParent->status == JOIN_BLOCK)
        {
            runningProcess->pParent->status = READY;
            add_to_ready_queue(runningProcess->pParent);
        }
    }

    enableInterrupts();

    // Call dispatcher to switch away. k_exit does not return.
    dispatcher();

    // Should never reach here, but just to be safe:
    while (1) { /* spin forever */ }
}

/**************************************************************************
   Name - k_kill

   Purpose - Signals a process with the specified signal

   Parameters - Signal to send

   Returns 0 if it's a success
*************************************************************************/
int k_kill(int pid, int signal)
{
    disableInterrupts();

    // Only SIG_TERM is supported
    if (signal != SIG_TERM) {
        stop(1); // never returns
    }

    // Find the target process
    Process* target = findProcessByPid(pid);
    if (!target) {
        // Non-existing process => spec says halt with code 1
        stop(1); // never returns
    }

    // Mark the process as signaled
    target->signaledFlag = 1;

    enableInterrupts();
    return 0; // success
}

/*****************************************************************************
 *                        k_getpid(void)
 *
 * Returns the PID of the calling process.
 *****************************************************************************/
int k_getpid()
{
    // If for some reason runningProcess is NULL (like at system startup)
    // we might want to handle that. Shouldn't be after scheduling begins.
    if (runningProcess == NULL) return -1;
    return runningProcess->pid;
}

/*****************************************************************************
 *                        k_join(int pid, int* p_child_exit_code)
 *
 * Similar to k_wait, but waits for a SPECIFIC child identified by 'pid'.
 *
 * Returns:
 *   0  => success, child found and reaped
 *  -5  => signaled while waiting
 *
 *  Also note the specification for error conditions:
 *   - If the caller tries to join itself => stop(1).
 *   - If the caller tries to join its parent => stop(2).
 *   - If the specified pid does not exist or is not a child => stop(1).
 *****************************************************************************/
int k_join(int pid, int* p_child_exit_code)
{
    disableInterrupts();

    // Disallow joining self
    if (pid == runningProcess->pid) {
        stop(1); // never returns
    }

    // Disallow joining parent 
    if (runningProcess->pParent && pid == runningProcess->pParent->pid) {
        stop(2); // never returns
    }

    // Verify the target child actually exists and is a direct child of this process
    Process* child = findProcessByPid(pid);
    if (!child || child->pParent != runningProcess) {
        // Non-existing or not a child => stop(1)
        stop(1);
    }

    // If the child is already terminated, return immediately with exit code
    if (child->status == TERMINATED) {
        *p_child_exit_code = child->exitCode;
        removeChildFromParentList(child);
        enableInterrupts();
        return 0;
    }

    // Otherwise, we block until that specific child terminates
    // We'll block with status=JOIN_BLOCK for instance
    enableInterrupts();
    int blockResult = block(JOIN_BLOCK);

    // If we were signaled, return -5
    if (blockResult == -5) {
        return -5;
    }

    // Now unblocked => child presumably terminated
    disableInterrupts();
    if (child->status != TERMINATED) {
        // Theoretically the child might have been killed or something else,
        // but typically we expect it to be TERMINATED by now. If not, 
        // we can handle gracefully.
        enableInterrupts();
        // Return success or some other code. We'll just do 0 here.
        return 0;
    }

    // The child is definitely done, so gather exit code
    *p_child_exit_code = child->exitCode;
    removeChildFromParentList(child);

    enableInterrupts();
    return 0; // success
}

/*****************************************************************************
 *                        unblock(int pid)
 *
 * Unblocks a process with pid 'pid' returning it to READY state and
 * placing it on the ready queue.
 *
 * Returns
 *   0 => success
 *  -1 => the process is invalid or is not actually blocked
 *****************************************************************************/
int unblock(int pid)
{
    disableInterrupts();

    Process* p = findProcessByPid(pid);
    if (!p) {
        enableInterrupts();
        return -1; // invalid pid
    }

    // If p->status <= 10 => it's not in a user-blocked state. Possibly READY or RUNNING
    if (p->status <= 10) {
        enableInterrupts();
        return -1; // not blocked
    }

    // Ok, move it to READY
    p->status = READY;
    add_to_ready_queue(p);

    enableInterrupts();
    return 0;
}

/*****************************************************************************
 *                        block(int block_status)
 *
 * Blocks the calling process with a given block_status, which must be > 10.
 * Then calls dispatcher() to switch away. We eventually resume here (return)
 * when unblocked.
 *
 * Returns:
 *   0 => unblocked normally
 *  -5 => unblocked because the process was signaled
 *
 * If block_status <= 10 => stop(1) per the specification.
 *****************************************************************************/
int block(int block_status)
{
    disableInterrupts();

    // The specification: "The value of block_status must be > 10. 
    // If <= 10 => stop(1)."
    if (block_status <= 10) {
        stop(1); // never returns
    }

    // Mark our status to the block code
    runningProcess->status = block_status;

    // Perform a context switch to another ready process
    dispatcher();

    // When we come back here, the process is unblocked. 
    // We re-enable interrupts and see if we were signaled.
    enableInterrupts();

    // If signaled, return -5, else 0
    if (runningProcess->signaledFlag) {
        return -5;
    }
    return 0;
}

/*****************************************************************************
 *                        get_start_time(void)
 *
 * Returns the start time of the calling process in microseconds.
 *****************************************************************************/
int get_start_time(void)
{
    if (!runningProcess) return 0;
    return (int)(runningProcess->startTime);
}

/*****************************************************************************
 *                        cpu_time(void)
 *
 * Returns the CPU time (in milliseconds) used by the calling process so far.
 *****************************************************************************/
int cpu_time(void)
{
    if (!runningProcess) return 0;
    return (int)(runningProcess->cpuTime);
}

/*****************************************************************************
 *                        signaled(void)
 *
 * Returns 1 if the calling process has been signaled, else 0.
 *****************************************************************************/
int signaled()
{
    return (runningProcess && runningProcess->signaledFlag) ? 1 : 0;
}
/*************************************************************************
   Name - readtime
*************************************************************************/
int read_time()
{
    return 0;
}

/*****************************************************************************
 *                        read_clock()
 *
 * Returns the current value of the system clock (THREADS system_clock()).
 *****************************************************************************/
DWORD read_clock(void)
{
    return system_clock();
}

/*****************************************************************************
 *                        display_process_table()
 *
 * Displays all non-empty (non-zero status) processes in the process table,
 * including PID, Parent PID, Priority, Status, # of Kids, CPU time, and Name.
 *****************************************************************************/
void display_process_table(void)
{
    console_output(TRUE, "PID Parent Pri Status #Kids CPUtime Name\n");
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        // If status == 0, that means empty slot (unused).
        if (processTable[i].status != 0)
        {
            Process* p = &processTable[i];
            int parentId = (p->pParent) ? p->pParent->pid : -1;
            int kids = countChildren(p);
            console_output(TRUE, "%2d  %4d   %2d  %5d  %5d  %7d  %s\n",
                p->pid, parentId, p->priority,
                p->status, kids, p->cpuTime, p->name);
        }
    }
}

/**************************************************************************
   Name - dispatcher

   Purpose - This is where context changes to the next process to run.

   Parameters - none

   Returns - nothing

*************************************************************************/
void dispatcher(void)
{
    disableInterrupts();

    // Picks the highest-priority process that is READY
    Process* nextProc = get_highest_priority_ready_process();
    if (nextProc == NULL)
    {
        // No ready processes? Possibly idle or watchdog takes over.
        enableInterrupts();
        return;
    }

    // If the chosen process is the same as our currently running one do nothing
    if (nextProc == runningProcess)
    {
        enableInterrupts();
        return;
    }

    // We have an old process (runningProcess) and a new process (nextProc).
    Process* oldProc = runningProcess;
    runningProcess = nextProc;
    runningProcess->status = RUNNING;  // Mark it as running now

    // If oldProc was running, you might want to set it to READY (unless it just blocked)
    if (oldProc && oldProc->status == RUNNING) {
        oldProc->status = READY;
        add_to_ready_queue(oldProc);
    }


    // If there is an old process, we call context_stop to save its CPU state
    // into oldProc->context. Then we switch to the newProc’s context.
    if (oldProc != NULL)
    {
        // Save oldProc's current CPU registers into oldProc->context
        context_stop(oldProc->context);
    }

    // Finally switch to the newProc's context (load CPU registers).
    context_switch(runningProcess->context);

    enableInterrupts();
}

/**************************************************************************
   Name - time_slice

   Purpose - This is where we handle timer interrupts.

   Parameters - none

   Returns - nothing

*************************************************************************/
void time_slice()
{
    // Typically, we might add something like:
    // runningProcess->cpuTime += someIncrement
    // depending on how frequently the timer interrupt fires.
    // For simplicity, assume we increment by 1 every tick, or do a real measure
    runningProcess->cpuTime += 1; // e.g., each timer = 1ms

    if (runningProcess->cpuTime >= TIME_SLICE_QUANTUM) {
        // time to switch
        runningProcess->cpuTime = 0;  // reset CPU usage for next timeslice
        dispatcher();
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
    DebugConsole("watchdog(): started.\n");

    while (1)
    {
        check_deadlock();
        // We could yield or do a small sleep, or just spin.
        // If all processes are blocked, eventually the scheduler
        // might run the watchdog so it can detect a deadlock.
    }
    return 0; // never actually reached
}

/* check to determine if deadlock has occurred... */
static void check_deadlock()
{
}

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

/*****************************************************************************
 *            SMALL HELPER FUNCTIONS
 *****************************************************************************/

 /**
  * Finds a free entry in processTable (status==0) and returns its index,
  * or -1 if none are free.
  */
static int findFreeProcessSlot(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processTable[i].status == 0) { // 0 means "unused" or "EMPTY"
            return i;
        }
    }
    return -1;
}

/**
 * findProcessByPid: returns a pointer to the process with 'pid', or NULL if not found.
 */
static Process* findProcessByPid(int pid)
{
    if (pid <= 0) return NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processTable[i].status != 0 &&  // means it's in use
            processTable[i].pid == pid)
        {
            return &processTable[i];
        }
    }
    return NULL;
}

/**
 * Removes 'child' from child->pParent->pChildren singly linked list.
 * Important after a child terminates or is otherwise re-parented.
 */
static void removeChildFromParentList(Process* child)
{
    if (!child || !child->pParent) return;

    Process* parent = child->pParent;
    Process* prev = NULL;
    Process* curr = parent->pChildren;

    while (curr != NULL) {
        if (curr == child) {
            // Found it. Unlink
            if (prev == NULL) {
                // child is first in parent's children list
                parent->pChildren = curr->nextSiblingProcess;
            }
            else {
                // child is in middle or end
                prev->nextSiblingProcess = curr->nextSiblingProcess;
            }
            // Null out child's parent to reflect it's no longer in that list
            child->pParent = NULL;
            child->nextSiblingProcess = NULL;
            return;
        }
        prev = curr;
        curr = curr->nextSiblingProcess;
    }
}

/**
 * findTerminatedChild: returns a pointer to any child of 'parent' that
 * is in TERMINATED status. If none, returns NULL.
 */
static Process* findTerminatedChild(Process* parent)
{
    Process* c = parent->pChildren;
    while (c != NULL) {
        if (c->status == TERMINATED) {
            return c;
        }
        c = c->nextSiblingProcess;
    }
    return NULL;
}

/**
 * countChildren: returns how many child processes 'parent' has (including
 * ready, blocked, terminated, etc.).
 */
static int countChildren(Process* parent)
{
    int count = 0;
    Process* c = parent->pChildren;
    while (c) {
        count++;
        c = c->nextSiblingProcess;
    }
    return count;
}

/**
 * disableInterrupts() and enableInterrupts()
 * manipulate the processor status register (PSR).int kids = countChildren(p);
 */
static inline void disableInterrupts()
{
    // Clear the PSR_INTERRUPTS bit
    uint32_t psr = get_psr();
    psr &= ~PSR_INTERRUPTS;
    set_psr(psr);
}

static inline void enableInterrupts()
{
    // Set the PSR_INTERRUPTS bit
    uint32_t psr = get_psr();
    psr |= PSR_INTERRUPTS;
    set_psr(psr);
}

/* there is no I/O yet, so return false. */
int check_io_scheduler()
{
    return false;
}

/*****************************************************************************
 *           READY QUEUE IMPLEMENTATION
 *****************************************************************************/

 /*
  * An array of lists (one list per priority level).
  *   - Index 0 => priority 0
  *   - Index 1 => priority 1
  *   ...
  *   - Index HIGHEST_PRIORITY => priority 5
  */
static Process* readyQueue[HIGHEST_PRIORITY + 1] = { NULL };

/**
 * ready_queue_init():
 *   Initializes (clears) all ready-queue lists.
 */
void ready_queue_init(void)
{
    // Set each priority list head to NULL
    for (int i = LOWEST_PRIORITY; i <= HIGHEST_PRIORITY; i++)
    {
        readyQueue[i] = NULL;
    }
}

/**
 * add_to_ready_queue(Process* proc):
 *   Adds 'proc' at the HEAD of the ready queue for proc->priority.
 *
 *   For example, if proc->priority == 3, we insert it at the front
 *   of the linked list for priority 3.
 */
void add_to_ready_queue(Process* proc)
{
    // Insert at the head of the list. 
    // Make sure your Process struct has a pointer, e.g. proc->nextReady, for the queue link.
    proc->nextReady = readyQueue[proc->priority];
    readyQueue[proc->priority] = proc;
}

/**
 * get_highest_priority_ready_process():
 *   Finds the highest (numerically greatest) priority queue that is NOT empty
 *   and removes the front element from that queue, returning it.
 *
 *   If all queues are empty, returns NULL.
 */
Process* get_highest_priority_ready_process(void)
{
    // Start at HIGHEST_PRIORITY (5) and go down to LOWEST_PRIORITY (0).
    for (int pri = HIGHEST_PRIORITY; pri >= LOWEST_PRIORITY; pri--)
    {
        if (readyQueue[pri] != NULL)
        {
            // We found a non-empty queue at 'pri'.
            Process* front = readyQueue[pri];
            // Remove 'front' from the list
            readyQueue[pri] = front->nextReady;

            // Clear out front->nextReady so it doesn't keep stale pointer
            front->nextReady = NULL;
            return front;
        }
    }
    // No ready processes at any priority
    return NULL;
}
