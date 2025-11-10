#include "worker.h"

#include "messages.h"
#include "parallel_a_star.h"
#include "serialization.h"

static HighLevelNode *clone_parent_node(const HighLevelNode *parent)
{
    HighLevelNode *clone = cbs_node_create(parent->num_agents);
    if (!clone)
    {
        return NULL;
    }
    clone->parent_id = parent->id;
    clone->depth = parent->depth + 1;
    clone->cost = parent->cost;
    for (int i = 0; i < parent->constraints.count; ++i)
    {
        constraint_set_add(&clone->constraints, parent->constraints.items[i]);
    }
    for (int i = 0; i < parent->num_agents; ++i)
    {
        path_copy(&clone->paths[i], &parent->paths[i]);
    }
    return clone;
}

static bool append_constraint(HighLevelNode *node, Constraint constraint)
{
    constraint_set_add(&node->constraints, constraint);
    return true;
}

static bool replan_agent_path(const ProblemInstance *instance,
                              HighLevelNode *node,
                              int agent_id,
                              const LowLevelContext *ll_ctx)
{
    AgentPath new_path;
    path_init(&new_path, 0);
    bool ok = low_level_request_path(instance,
                                     &node->constraints,
                                     agent_id,
                                     ll_ctx,
                                     &new_path);
    if (!ok)
    {
        path_free(&new_path);
        return false;
    }
    path_free(&node->paths[agent_id]);
    node->paths[agent_id] = new_path;
    return true;
}

static Constraint make_vertex_constraint(const Conflict *conflict, int agent_id)
{
    Constraint c = {.agent_id = agent_id,
                    .time = conflict->time,
                    .type = CONSTRAINT_VERTEX,
                    .vertex = conflict->position,
                    .edge_to = conflict->position};
    return c;
}

static Constraint make_edge_constraint(const HighLevelNode *node,
                                       const Conflict *conflict,
                                       int agent_id)
{
    Constraint c = {.agent_id = agent_id,
                    .time = conflict->time,
                    .type = CONSTRAINT_EDGE,
                    .vertex = conflict->position,
                    .edge_to = conflict->edge_to};
    if (agent_id == conflict->agent_b)
    {
        GridCoord from = path_step_at(&node->paths[agent_id], conflict->time);
        GridCoord to = path_step_at(&node->paths[agent_id], conflict->time + 1);
        c.vertex = from;
        c.edge_to = to;
    }
    return c;
}

static bool process_node(const ProblemInstance *instance,
                         const LowLevelContext *ll_ctx,
                         HighLevelNode *node,
                         int incumbent_cost,
                         int coordinator_rank)
{
    node->cost = cbs_compute_soc(node);
    Conflict conflict;
    if (!cbs_detect_conflict(node, &conflict))
    {
        SerializedNode payload;
        serialize_high_level_node(node, &payload);
        send_serialized_node(coordinator_rank, TAG_SOLUTION, &payload);
        free_serialized_node(&payload);
        return true;
    }

    HighLevelNode *children[2] = {NULL, NULL};
    int child_agents[2] = {conflict.agent_a, conflict.agent_b};
    int produced = 0;

    for (int idx = 0; idx < 2; ++idx)
    {
        HighLevelNode *child = clone_parent_node(node);
        if (!child)
        {
            continue;
        }

        Constraint c = conflict.is_vertex_conflict
                           ? make_vertex_constraint(&conflict, child_agents[idx])
                           : make_edge_constraint(node, &conflict, child_agents[idx]);
        append_constraint(child, c);

        if (!replan_agent_path(instance, child, child_agents[idx], ll_ctx))
        {
            cbs_node_free(child);
            continue;
        }

        child->cost = cbs_compute_soc(child);
        if (incumbent_cost > 0 && child->cost >= (double)incumbent_cost)
        {
            cbs_node_free(child);
            continue;
        }

        children[produced++] = child;
    }

    MPI_Send(&produced, 1, MPI_INT, coordinator_rank, TAG_CHILDREN, MPI_COMM_WORLD);

    for (int i = 0; i < produced; ++i)
    {
        HighLevelNode *child = children[i];
        child->id = -1;
        SerializedNode payload;
        serialize_high_level_node(child, &payload);
        payload.aux_value = node->id;
        send_serialized_node(coordinator_rank, TAG_CHILDREN, &payload);
        free_serialized_node(&payload);
        cbs_node_free(child);
    }

    return false;
}

void run_worker(const ProblemInstance *instance,
                const LowLevelContext *ll_ctx,
                int coordinator_rank)
{
    int active = 1;
    while (active)
    {
        MPI_Status status;
        MPI_Probe(coordinator_rank, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        if (status.MPI_TAG == TAG_TERMINATE)
        {
            MPI_Recv(NULL, 0, MPI_INT, coordinator_rank, TAG_TERMINATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            active = 0;
            break;
        }
        else if (status.MPI_TAG == TAG_TASK)
        {
            SerializedNode received;
            receive_serialized_node(coordinator_rank, TAG_TASK, &received, NULL);
            int incumbent_cost = received.aux_value;
            HighLevelNode *node = deserialize_high_level_node(&received);
            free_serialized_node(&received);
            if (!node)
            {
                continue;
            }

            process_node(instance, ll_ctx, node, incumbent_cost, coordinator_rank);
            cbs_node_free(node);
        }
    }
}
