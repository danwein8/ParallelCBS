#ifndef PARALLEL_CBS_SERIALIZATION_H
#define PARALLEL_CBS_SERIALIZATION_H

#include "cbs.h"

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

void serialize_high_level_node(const HighLevelNode *node, SerializedNode *out);
void free_serialized_node(SerializedNode *node);
HighLevelNode *deserialize_high_level_node(const SerializedNode *data);
void send_serialized_node(int dest_rank, int tag, const SerializedNode *node);
void receive_serialized_node(int source_rank, int tag, SerializedNode *out, MPI_Status *status_out);

#endif /* PARALLEL_CBS_SERIALIZATION_H */
