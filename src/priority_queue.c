#include "priority_queue.h"

static void pq_swap(PQEntry *a, PQEntry *b)
{
    PQEntry tmp = *a;
    *a = *b;
    *b = tmp;
}

void pq_init(PriorityQueue *queue)
{
    queue->items = NULL;
    queue->count = 0;
    queue->capacity = 0;
}

void pq_free(PriorityQueue *queue)
{
    free(queue->items);
    queue->items = NULL;
    queue->count = 0;
    queue->capacity = 0;
}

void pq_push(PriorityQueue *queue, double key, void *value)
{
    if (queue->count >= queue->capacity)
    {
        int new_cap = queue->capacity == 0 ? 16 : queue->capacity * 2;
        queue->items = (PQEntry *)realloc(queue->items, (size_t)new_cap * sizeof(PQEntry));
        queue->capacity = new_cap;
    }
    int idx = queue->count++;
    queue->items[idx].key = key;
    queue->items[idx].value = value;

    while (idx > 0)
    {
        int parent = (idx - 1) / 2;
        if (queue->items[parent].key <= queue->items[idx].key)
        {
            break;
        }
        pq_swap(&queue->items[parent], &queue->items[idx]);
        idx = parent;
    }
}

void *pq_pop(PriorityQueue *queue, double *out_key)
{
    if (queue->count == 0)
    {
        return NULL;
    }
    if (out_key)
    {
        *out_key = queue->items[0].key;
    }
    void *result = queue->items[0].value;
    queue->count--;
    queue->items[0] = queue->items[queue->count];

    int idx = 0;
    while (true)
    {
        int left = idx * 2 + 1;
        int right = left + 1;
        int smallest = idx;

        if (left < queue->count && queue->items[left].key < queue->items[smallest].key)
        {
            smallest = left;
        }
        if (right < queue->count && queue->items[right].key < queue->items[smallest].key)
        {
            smallest = right;
        }
        if (smallest == idx)
        {
            break;
        }
        pq_swap(&queue->items[idx], &queue->items[smallest]);
        idx = smallest;
    }

    return result;
}

void *pq_peek(const PriorityQueue *queue, double *out_key)
{
    if (queue->count == 0)
    {
        return NULL;
    }
    if (out_key)
    {
        *out_key = queue->items[0].key;
    }
    return queue->items[0].value;
}
