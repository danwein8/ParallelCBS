#include "serialization.h"

#include <mpi.h>
#include <string.h>

static int compute_path_int_count(const HighLevelNode *node)
{
    int total = 0;
    for (int i = 0; i < node->num_agents; ++i)
    {
        total += 1 + node->paths[i].length * 2;
    }
    return total;
}

static void fill_path_data(const HighLevelNode *node, int *buffer)
{
    int cursor = 0;
    for (int i = 0; i < node->num_agents; ++i)
    {
        buffer[cursor++] = node->paths[i].length;
        for (int j = 0; j < node->paths[i].length; ++j)
        {
            buffer[cursor++] = node->paths[i].steps[j].x;
            buffer[cursor++] = node->paths[i].steps[j].y;
        }
    }
}

static void apply_path_data(HighLevelNode *node, const int *buffer)
{
    int cursor = 0;
    for (int i = 0; i < node->num_agents; ++i)
    {
        int length = buffer[cursor++];
        path_reserve(&node->paths[i], length);
        node->paths[i].length = length;
        for (int j = 0; j < length; ++j)
        {
            node->paths[i].steps[j].x = buffer[cursor++];
            node->paths[i].steps[j].y = buffer[cursor++];
        }
    }
}

void serialize_high_level_node(const HighLevelNode *node, SerializedNode *out)
{
    out->node_id = node->id;
    out->parent_id = node->parent_id;
    out->depth = node->depth;
    out->num_agents = node->num_agents;
    out->constraint_count = node->constraints.count;
    out->aux_value = 0;
    out->cost = node->cost;

    out->path_int_count = compute_path_int_count(node);
    out->path_data = NULL;
    if (out->path_int_count > 0)
    {
        out->path_data = (int *)malloc(sizeof(int) * (size_t)out->path_int_count);
        fill_path_data(node, out->path_data);
    }

    out->constraint_int_count = node->constraints.count * 7;
    out->constraint_data = NULL;
    if (out->constraint_int_count > 0)
    {
        out->constraint_data = (int *)malloc(sizeof(int) * (size_t)out->constraint_int_count);
        int cursor = 0;
        for (int i = 0; i < node->constraints.count; ++i)
        {
            const Constraint *c = &node->constraints.items[i];
            out->constraint_data[cursor++] = c->agent_id;
            out->constraint_data[cursor++] = c->time;
            out->constraint_data[cursor++] = (int)c->type;
            out->constraint_data[cursor++] = c->vertex.x;
            out->constraint_data[cursor++] = c->vertex.y;
            out->constraint_data[cursor++] = c->edge_to.x;
            out->constraint_data[cursor++] = c->edge_to.y;
        }
    }
}

void free_serialized_node(SerializedNode *node)
{
    free(node->path_data);
    free(node->constraint_data);
    node->path_data = NULL;
    node->constraint_data = NULL;
    node->path_int_count = 0;
    node->constraint_int_count = 0;
}

HighLevelNode *deserialize_high_level_node(const SerializedNode *data)
{
    HighLevelNode *node = cbs_node_create(data->num_agents);
    if (!node)
    {
        return NULL;
    }
    node->id = data->node_id;
    node->parent_id = data->parent_id;
    node->depth = data->depth;
    node->cost = data->cost;

    if (data->path_int_count > 0 && data->path_data != NULL)
    {
        apply_path_data(node, data->path_data);
    }

    if (data->constraint_int_count > 0 && data->constraint_data != NULL)
    {
        int cursor = 0;
        for (int i = 0; i < data->constraint_count; ++i)
        {
            Constraint c = {
                .agent_id = data->constraint_data[cursor++],
                .time = data->constraint_data[cursor++],
                .type = (ConstraintType)data->constraint_data[cursor++],
                .vertex = {.x = data->constraint_data[cursor++], .y = data->constraint_data[cursor++]},
                .edge_to = {.x = data->constraint_data[cursor++], .y = data->constraint_data[cursor++]}};
            constraint_set_add(&node->constraints, c);
        }
    }

    return node;
}

void send_serialized_node(int dest_rank, int tag, const SerializedNode *node)
{
    int header[8] = {
        node->node_id,
        node->parent_id,
        node->depth,
        node->num_agents,
        node->constraint_count,
        node->path_int_count,
        node->constraint_int_count,
        node->aux_value};

    MPI_Send(header, 8, MPI_INT, dest_rank, tag, MPI_COMM_WORLD);
    MPI_Send(&node->cost, 1, MPI_DOUBLE, dest_rank, tag, MPI_COMM_WORLD);
    if (node->path_int_count > 0 && node->path_data != NULL)
    {
        MPI_Send(node->path_data, node->path_int_count, MPI_INT, dest_rank, tag, MPI_COMM_WORLD);
    }
    if (node->constraint_int_count > 0 && node->constraint_data != NULL)
    {
        MPI_Send(node->constraint_data, node->constraint_int_count, MPI_INT, dest_rank, tag, MPI_COMM_WORLD);
    }
}

void receive_serialized_node(int source_rank, int tag, SerializedNode *out, MPI_Status *status_out)
{
    int header[8];
    MPI_Status status;
    MPI_Recv(header, 8, MPI_INT, source_rank, tag, MPI_COMM_WORLD, &status);
    if (status_out != NULL)
    {
        *status_out = status;
    }
    out->node_id = header[0];
    out->parent_id = header[1];
    out->depth = header[2];
    out->num_agents = header[3];
    out->constraint_count = header[4];
    out->path_int_count = header[5];
    out->constraint_int_count = header[6];
    out->aux_value = header[7];

    MPI_Recv(&out->cost, 1, MPI_DOUBLE, source_rank, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    out->path_data = NULL;
    if (out->path_int_count > 0)
    {
        out->path_data = (int *)malloc(sizeof(int) * (size_t)out->path_int_count);
        MPI_Recv(out->path_data, out->path_int_count, MPI_INT, source_rank, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    out->constraint_data = NULL;
    if (out->constraint_int_count > 0)
    {
        out->constraint_data = (int *)malloc(sizeof(int) * (size_t)out->constraint_int_count);
        MPI_Recv(out->constraint_data, out->constraint_int_count, MPI_INT, source_rank, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
}
