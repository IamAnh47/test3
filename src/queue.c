#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{

        /* TODO: put a new process to queue[q] */
    if (q == NULL || proc == NULL || q->size >= MAX_QUEUE_SIZE) {
        // Queue is full or invalid input
        return;
    }
    q->proc[q->size++] = proc; 
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /* 
         * TODO: return a pcb whose priority is the highest
         * in the queue [q] and remember to remove it from q
         */
        // return NULL;
    if (empty(q)) {
        // Queue is empty
        return NULL;
    }

    // Assume the first process has the highest priority initially
    int highest_priority_index = 0;
    for (int i = 1; i < q->size; i++) {
        if (q->proc[i]->prio < q->proc[highest_priority_index]->prio) {
            highest_priority_index = i;
        }
    }

    // Extract the highest priority process
    struct pcb_t * highest_priority_proc = q->proc[highest_priority_index];

    // Shift elements to fill the gap
    for (int i = highest_priority_index; i < q->size - 1; i++) {
        q->proc[i] = q->proc[i + 1];
    }

    // Decrease size of the queue
    q->size--;

    return highest_priority_proc;
}


