#include "coordinator.h"

#include "messages.h"
#include "priority_queue.h"
#include "serialization.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

static int select_worker(const WorkerSet *workers, int *rr_index)
{
    if (workers->count == 0)
    {
        return MPI_PROC_NULL;
    }
    int idx = *rr_index % workers->count;
    int rank = workers->ranks[idx];
    *rr_index = (*rr_index + 1) % workers->count;
    return rank;
}

static bool initialize_root(const ProblemInstance *instance,
                            const LowLevelContext *ll_ctx,
                            HighLevelNode *root)
{
    for (int agent = 0; agent < instance->num_agents; ++agent)
    {
        if (!low_level_request_path(instance, &root->constraints, agent, ll_ctx, &root->paths[agent]))
        {
            return false;
        }
    }
    root->cost = cbs_compute_soc(root);
    return true;
}

static void dispatch_node(const HighLevelNode *node,
                          double incumbent_cost,
                          int worker_rank)
{
    SerializedNode payload;
    serialize_high_level_node(node, &payload);
    payload.aux_value = (int)(incumbent_cost >= (double)INT_MAX ? INT_MAX : (int)ceil(incumbent_cost));
    send_serialized_node(worker_rank, TAG_TASK, &payload);
    free_serialized_node(&payload);
}

void run_coordinator(const ProblemInstance *instance,
                     const LowLevelContext *ll_ctx,
                     const WorkerSet *workers)
{
    if (workers->count == 0)
    {
        if (MPI_Comm_rank(MPI_COMM_WORLD, &(int){0}) == 0)
        {
            fprintf(stderr, "No expansion workers available.\n");
        }
        return;
    }

    PriorityQueue open;
    pq_init(&open);

    HighLevelNode *root = cbs_node_create(instance->num_agents);
    root->id = 0;
    root->depth = 0;
    root->parent_id = -1;

    if (!initialize_root(instance, ll_ctx, root))
    {
        fprintf(stderr, "Failed to compute initial paths.\n");
        cbs_node_free(root);
        pq_free(&open);
        return;
    }

    pq_push(&open, root->cost, root);

    double incumbent_cost = DBL_MAX;
    HighLevelNode *incumbent_solution = NULL;
    int next_node_id = 1;
    int rr_index = 0;

    while (open.count > 0)
    {
        double plateau_cost = 0.0;
        double key = 0.0;
        HighLevelNode *front = (HighLevelNode *)pq_pop(&open, &plateau_cost);
        int plateau_capacity = workers->count > 0 ? workers->count : 1;
        HighLevelNode **plateau = (HighLevelNode **)malloc(sizeof(HighLevelNode *) * (size_t)plateau_capacity);
        int plateau_size = 0;
        plateau[plateau_size++] = front;

        while (open.count > 0)
        {
            HighLevelNode *peek = (HighLevelNode *)pq_peek(&open, &key);
            if (fabs(peek->cost - plateau_cost) > 1e-6)
            {
                break;
            }
            HighLevelNode *equal_node = (HighLevelNode *)pq_pop(&open, &key);
            if (plateau_size >= plateau_capacity)
            {
                plateau_capacity *= 2;
                plateau = (HighLevelNode **)realloc(plateau, sizeof(HighLevelNode *) * (size_t)plateau_capacity);
            }
            plateau[plateau_size++] = equal_node;
        }

        int outstanding = plateau_size;
        for (int i = 0; i < plateau_size; ++i)
        {
            int worker_rank = select_worker(workers, &rr_index);
            dispatch_node(plateau[i], incumbent_cost, worker_rank);
            cbs_node_free(plateau[i]);
        }

        free(plateau);

        while (outstanding > 0)
        {
            MPI_Status status;
            MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            if (status.MPI_TAG == TAG_SOLUTION)
            {
                SerializedNode solution_data;
                receive_serialized_node(status.MPI_SOURCE, TAG_SOLUTION, &solution_data, NULL);
                HighLevelNode *solution_node = deserialize_high_level_node(&solution_data);
                free_serialized_node(&solution_data);
                if (solution_node)
                {
                    solution_node->id = next_node_id++;
                    solution_node->cost = cbs_compute_soc(solution_node);
                    if (incumbent_solution != NULL)
                    {
                        cbs_node_free(incumbent_solution);
                    }
                    incumbent_solution = solution_node;
                    incumbent_cost = solution_node->cost;
                }
                outstanding--;
            }
            else if (status.MPI_TAG == TAG_CHILDREN)
            {
                int child_count = 0;
                MPI_Recv(&child_count, 1, MPI_INT, status.MPI_SOURCE, TAG_CHILDREN, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < child_count; ++i)
                {
                    SerializedNode child_data;
                    receive_serialized_node(status.MPI_SOURCE, TAG_CHILDREN, &child_data, NULL);
                    HighLevelNode *child = deserialize_high_level_node(&child_data);
                    free_serialized_node(&child_data);
                    if (!child)
                    {
                        continue;
                    }
                    child->id = next_node_id++;
                    child->cost = cbs_compute_soc(child);
                    if (child->cost < incumbent_cost)
                    {
                        pq_push(&open, child->cost, child);
                    }
                    else
                    {
                        cbs_node_free(child);
                    }
                }
                outstanding--;
            }
        }

        if (incumbent_solution != NULL)
        {
            double peek_cost = 0.0;
            HighLevelNode *next = (HighLevelNode *)pq_peek(&open, &peek_cost);
            if (!next || peek_cost >= incumbent_cost - 1e-6)
            {
                break;
            }
        }
    }

    for (int i = 0; i < workers->count; ++i)
    {
        MPI_Send(NULL, 0, MPI_INT, workers->ranks[i], TAG_TERMINATE, MPI_COMM_WORLD);
    }

    if (incumbent_solution)
    {
        printf("Best solution cost: %.0f\n", incumbent_solution->cost);
        cbs_node_free(incumbent_solution);
    }

    while (open.count > 0)
    {
        double dummy = 0.0;
        HighLevelNode *node = (HighLevelNode *)pq_pop(&open, &dummy);
        cbs_node_free(node);
    }
    pq_free(&open);
}
