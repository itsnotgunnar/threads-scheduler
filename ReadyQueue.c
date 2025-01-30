// ReadyQueue.c
#include "Scheduler.h"
#include "Processes.h"

static Process* readyQueue[HIGHEST_PRIORITY+1]; 
// We could also do an array of lists or a single list

void ready_queue_init(void)
{
    // Initialize our data structures
    for (int i = 0; i <= HIGHEST_PRIORITY; i++)
        readyQueue[i] = NULL;
}

void add_to_ready_queue(Process* proc)
{
    // Insert 'proc' into the queue of ready processes at index proc->priority.
    // For example, you might do:
    //   proc->nextReady = readyQueue[proc->priority];
    //   readyQueue[proc->priority] = proc;
    // Exactly how you link them depends on your design.
}

Process* get_highest_priority_ready_process(void)
{
    // Scan from HIGHEST_PRIORITY down to LOWEST_PRIORITY
    // If you find a non-empty queue, remove the front and return it.
    // If all are empty, return NULL.
    for (int pri = HIGHEST_PRIORITY; pri >= LOWEST_PRIORITY; pri--)
    {
        if (readyQueue[pri] != NULL)
        {
            // remove one from this priority
            Process* p = readyQueue[pri];
            // e.g. readyQueue[pri] = p->nextReady;
            // p->nextReady = NULL;
            return p;
        }
    }
    return NULL; 
}
