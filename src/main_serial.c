#include "coordinator.h"
#include "instance_io.h"
#include "low_level.h"
#include "priority_queue.h"

#include <float.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct
{
    const ProblemInstance *instance;
    LowLevelContext *ll_ctx;
} SerialContext;

static double wall_time_seconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

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

static bool replan_agent_path(const SerialContext *ctx,
                              HighLevelNode *node,
                              int agent_id)
{
    AgentPath new_path;
    path_init(&new_path, 0);
    bool ok = low_level_request_path(ctx->instance,
                                     &node->constraints,
                                     agent_id,
                                     ctx->ll_ctx,
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

static void run_serial_cbs(const ProblemInstance *instance,
                           double timeout_seconds,
                           RunStats *stats)
{
    double start = wall_time_seconds();
    LowLevelContext ll_ctx = {.manager_world_rank = -1, .pool_comm = MPI_COMM_NULL};
    SerialContext sctx = {.instance = instance, .ll_ctx = &ll_ctx};

    HighLevelNode *root = cbs_node_create(instance->num_agents);
    root->id = 0;
    root->depth = 0;
    root->parent_id = -1;

    for (int agent = 0; agent < instance->num_agents; ++agent)
    {
        if (!low_level_request_path(instance, &root->constraints, agent, &ll_ctx, &root->paths[agent]))
        {
            fprintf(stderr, "Failed to compute initial path for agent %d.\n", agent);
            cbs_node_free(root);
            return;
        }
    }
    root->cost = cbs_compute_soc(root);

    PriorityQueue open;
    pq_init(&open);
    pq_push(&open, root->cost, root);

    long long nodes_expanded = 0;
    long long nodes_generated = 0;
    long long conflicts_detected = 0;
    long long max_nodes_expanded = 20000;
    HighLevelNode *incumbent = NULL;
    int timed_out = 0;

    while (open.count > 0)
    {
        if (nodes_expanded >= max_nodes_expanded)
        {
            timed_out = 1;
            break;
        }
        if (timeout_seconds > 0.0 && wall_time_seconds() - start > timeout_seconds)
        {
            timed_out = 1;
            break;
        }

        double key = 0.0;
        HighLevelNode *node = (HighLevelNode *)pq_pop(&open, &key);
        nodes_expanded++;

        Conflict conflict;
        if (!cbs_detect_conflict(node, &conflict))
        {
            if (incumbent)
            {
                cbs_node_free(incumbent);
            }
            incumbent = node;
            break;
        }

        conflicts_detected++;
        int child_agents[2] = {conflict.agent_a, conflict.agent_b};
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
            constraint_set_add(&child->constraints, c);

            if (!replan_agent_path(&sctx, child, child_agents[idx]))
            {
                cbs_node_free(child);
                continue;
            }

            child->cost = cbs_compute_soc(child);
            pq_push(&open, child->cost, child);
            nodes_generated++;
        }

        cbs_node_free(node);
    }

    while (open.count > 0)
    {
        double dummy = 0.0;
        HighLevelNode *n = (HighLevelNode *)pq_pop(&open, &dummy);
        if (n != incumbent)
        {
            cbs_node_free(n);
        }
    }
    pq_free(&open);

    if (stats)
    {
        stats->nodes_expanded = nodes_expanded;
        stats->nodes_generated = nodes_generated;
        stats->conflicts_detected = conflicts_detected;
        stats->solution_found = incumbent != NULL;
        stats->timed_out = timed_out;
        stats->best_cost = incumbent ? cbs_compute_soc(incumbent) : DBL_MAX;
        stats->runtime_sec = wall_time_seconds() - start;
    }

    if (incumbent)
    {
        printf("[Serial] Solution cost: %.0f (nodes expanded=%lld)\n", incumbent->cost, nodes_expanded);
        cbs_node_free(incumbent);
    }
    else
    {
        printf("[Serial] No solution found.\n");
    }
}

int main(int argc, char **argv)
{
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    int did_mpi_init = 0;
    if (!mpi_initialized)
    {
        MPI_Init(&argc, &argv);
        did_mpi_init = 1;
    }

    const char *map_path = NULL;
    const char *agents_path = NULL;
    double timeout_seconds = 0.0;
    const char *csv_path = "results_serial.csv";

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--map") == 0 && i + 1 < argc)
        {
            map_path = argv[++i];
        }
        else if (strcmp(argv[i], "--agents") == 0 && i + 1 < argc)
        {
            agents_path = argv[++i];
        }
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc)
        {
            timeout_seconds = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc)
        {
            csv_path = argv[++i];
        }
    }

    if (!map_path || !agents_path)
    {
        fprintf(stderr, "Usage: serial_cbs --map map.txt --agents agents.txt [--timeout SEC] [--csv path]\n");
        return 1;
    }

    ProblemInstance instance;
    memset(&instance, 0, sizeof(instance));
    if (!load_problem_instance(map_path, agents_path, &instance))
    {
        fprintf(stderr, "Failed to load problem instance.\n");
        return 1;
    }

    RunStats stats;
    memset(&stats, 0, sizeof(RunStats));
    run_serial_cbs(&instance, timeout_seconds, &stats);

    const char *map_name = strrchr(map_path, '/');
    map_name = map_name ? map_name + 1 : map_path;
    int need_header = access(csv_path, F_OK) != 0;
    FILE *fp = fopen(csv_path, "a");
    if (fp)
    {
        if (need_header)
        {
            fprintf(fp, "map,agents,width,height,nodes_expanded,nodes_generated,conflicts,cost,runtime_sec,timeout_sec,status\n");
        }
        const char *status = stats.solution_found ? "success" : (stats.timed_out ? "timeout" : "failure");
        double cost_out = stats.solution_found ? stats.best_cost : -1.0;
        fprintf(fp,
                "%s,%d,%d,%d,%lld,%lld,%lld,%.0f,%.6f,%.2f,%s\n",
                map_name,
                instance.num_agents,
                instance.map.width,
                instance.map.height,
                stats.nodes_expanded,
                stats.nodes_generated,
                stats.conflicts_detected,
                cost_out,
                stats.runtime_sec,
                timeout_seconds,
                status);
        fclose(fp);
    }
    else
    {
        fprintf(stderr, "Warning: could not open CSV file %s for writing.\n", csv_path);
    }

    problem_instance_free(&instance);
    if (did_mpi_init)
    {
        MPI_Finalize();
    }
    return 0;
}
