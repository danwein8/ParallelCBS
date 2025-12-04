#ifndef PARALLEL_CBS_SERIALIZATION_H
#define PARALLEL_CBS_SERIALIZATION_H

#include "cbs.h"
#include <mpi.h>

typedef struct
{
    int node_id;
    int parent_id;
    int depth;
    int num_agents;
    int constraint_count;
    int aux_value;
    double cost;
    int path_int_count;
    int constraint_int_count;
    int *path_data;
    int *constraint_data;
} SerializedNode;

/* Pending async send entry - stores MPI requests and buffer pointers */
typedef struct
{
    MPI_Request requests[4]; /* header, cost, path_data, constraint_data */
    int num_requests;
    int *header;             /* dynamically allocated header buffer */
    double *cost;            /* dynamically allocated cost buffer */
    int *path_data;          /* owned copy of path data */
    int *constraint_data;    /* owned copy of constraint data */
} PendingSend;

/* Pool of pending async sends */
#define MAX_PENDING_SENDS 256

typedef struct
{
    PendingSend entries[MAX_PENDING_SENDS];
    int count;
} PendingSendPool;

void serialize_high_level_node(const HighLevelNode *node, SerializedNode *out);
void free_serialized_node(SerializedNode *node);
HighLevelNode *deserialize_high_level_node(const SerializedNode *data);

/* Blocking send (original) */
void send_serialized_node(int dest_rank, int tag, const SerializedNode *node);

/* Non-blocking send - stores requests in pool */
void send_serialized_node_async(int dest_rank, int tag, const SerializedNode *node, PendingSendPool *pool);

/* Initialize the pending send pool */
void pending_send_pool_init(PendingSendPool *pool);

/* Test and complete any finished sends, freeing their buffers */
void pending_send_pool_progress(PendingSendPool *pool);

/* Wait for all pending sends to complete */
void pending_send_pool_wait_all(PendingSendPool *pool);

void receive_serialized_node(int source_rank, int tag, SerializedNode *out, MPI_Status *status_out);

#endif /* PARALLEL_CBS_SERIALIZATION_H */
