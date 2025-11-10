#ifndef PARALLEL_CBS_PRIORITY_QUEUE_H
#define PARALLEL_CBS_PRIORITY_QUEUE_H

#include "common.h"

typedef struct
{
    double key;
    void *value;
} PQEntry;

typedef struct
{
    PQEntry *items;
    int count;
    int capacity;
} PriorityQueue;

void pq_init(PriorityQueue *queue);
void pq_free(PriorityQueue *queue);
void pq_push(PriorityQueue *queue, double key, void *value);
void *pq_pop(PriorityQueue *queue, double *out_key);
void *pq_peek(const PriorityQueue *queue, double *out_key);

#endif /* PARALLEL_CBS_PRIORITY_QUEUE_H */
