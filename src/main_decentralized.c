#include "coordinator.h"
#include "instance_io.h"
#include "low_level.h"
#include "messages.h"
#include "priority_queue.h"
#include "serialization.h"

#include <float.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void broadcast_instance(ProblemInstance *instance, int root, MPI_Comm comm)
{
    int rank = 0;
    MPI_Comm_rank(comm, &rank);

    int header[3] = {0, 0, 0};
    if (rank == root)
    {
        header[0] = instance->map.width;
        header[1] = instance->map.height;
        header[2] = instance->num_agents;
    }
    MPI_Bcast(header, 3, MPI_INT, root, comm);

    int width = header[0];
    int height = header[1];
    int agents = header[2];

    if (rank != root)
    {
        problem_instance_init(instance, agents);
        grid_init(&instance->map, width, height);
    }

    size_t cell_count = (size_t)width * (size_t)height;
    if (cell_count > 0)
    {
        MPI_Bcast(instance->map.cells, (int)cell_count, MPI_UNSIGNED_CHAR, root, comm);
    }

    if (agents > 0)
    {
        int *buffer = (int *)malloc(sizeof(int) * (size_t)agents * 2);
        if (rank == root)
        {
            for (int i = 0; i < agents; ++i)
            {
                buffer[i * 2] = instance->starts[i].x;
                buffer[i * 2 + 1] = instance->starts[i].y;
            }
        }
        MPI_Bcast(buffer, agents * 2, MPI_INT, root, comm);
        if (rank != root)
        {
            for (int i = 0; i < agents; ++i)
            {
                instance->starts[i].x = buffer[i * 2];
                instance->starts[i].y = buffer[i * 2 + 1];
            }
        }

        if (rank == root)
        {
            for (int i = 0; i < agents; ++i)
            {
                buffer[i * 2] = instance->goals[i].x;
                buffer[i * 2 + 1] = instance->goals[i].y;
            }
        }
        MPI_Bcast(buffer, agents * 2, MPI_INT, root, comm);
        if (rank != root)
        {
            for (int i = 0; i < agents; ++i)
            {
                instance->goals[i].x = buffer[i * 2];
                instance->goals[i].y = buffer[i * 2 + 1];
            }
        }
        free(buffer);
    }
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

static bool replan_agent_path(const ProblemInstance *instance,
                              HighLevelNode *node,
                              int agent_id,
                              LowLevelContext *ll_ctx)
{
    AgentPath new_path;
    path_init(&new_path, 0);
    
    int world_rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    printf("[Decentral %d] replan_agent_path: calling low_level for agent %d\n", world_rank, agent_id);
    fflush(stdout);
    
    bool ok = low_level_request_path(instance,
                                     &node->constraints,
                                     agent_id,
                                     ll_ctx,
                                     &new_path);
    
    printf("[Decentral %d] replan_agent_path: low_level returned %s for agent %d\n", 
           world_rank, ok ? "SUCCESS" : "FAIL", agent_id);
    fflush(stdout);
    
    if (!ok)
    {
        path_free(&new_path);
        return false;
    }
    path_free(&node->paths[agent_id]);
    node->paths[agent_id] = new_path;
    return true;
}

// static bool replan_agent_path(const ProblemInstance *instance,
//                               HighLevelNode *node,
//                               int agent_id,
//                               LowLevelContext *ll_ctx)
// {
//     AgentPath new_path;
//     path_init(&new_path, 0);
//     bool ok = low_level_request_path(instance,
//                                      &node->constraints,
//                                      agent_id,
//                                      ll_ctx,
//                                      &new_path);
//     if (!ok)
//     {
//         path_free(&new_path);
//         return false;
//     }
//     path_free(&node->paths[agent_id]);
//     node->paths[agent_id] = new_path;
//     return true;
// }

static void push_child(const HighLevelNode *child, int dest_rank, PendingSendPool *pool)
{
    SerializedNode payload;
    serialize_high_level_node(child, &payload);
    
    printf("[Decentral push] Sending node to rank %d (path_ints=%d, constraint_ints=%d)\n",
           dest_rank, payload.path_int_count, payload.constraint_int_count);
    fflush(stdout);
    
    /* Use async send to avoid blocking */
    send_serialized_node_async(dest_rank, TAG_DP_NODE, &payload, pool);
    
    printf("[Decentral push] Send initiated to rank %d\n", dest_rank);
    fflush(stdout);
    
    free_serialized_node(&payload);
}

// static void push_child(const HighLevelNode *child, int dest_rank)
// {
//     SerializedNode payload;
//     serialize_high_level_node(child, &payload);
//     send_serialized_node(dest_rank, TAG_DP_NODE, &payload);
//     free_serialized_node(&payload);
// }

static void receive_buffered_nodes(PriorityQueue *open, int self_rank, double *comm_time_acc)
{
    int flag = 0;
    MPI_Status status;
    while (1)
    {
        MPI_Iprobe(MPI_ANY_SOURCE, TAG_DP_NODE, MPI_COMM_WORLD, &flag, &status);
        if (!flag)
        {
            break;
        }
        SerializedNode payload;
        double recv_start = MPI_Wtime();
        receive_serialized_node(status.MPI_SOURCE, TAG_DP_NODE, &payload, NULL);
        if (comm_time_acc) *comm_time_acc += MPI_Wtime() - recv_start;
        HighLevelNode *node = deserialize_high_level_node(&payload);
        free_serialized_node(&payload);
        if (node)
        {
            node->cost = cbs_compute_soc(node);
            pq_push(open, node->cost, node);
            printf("[Decentral %d] Received node cost=%.0f depth=%d from %d\n",
                   self_rank,
                   node->cost,
                   node->depth,
                   status.MPI_SOURCE);
            fflush(stdout);
        }
    }
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int world_rank = 0;
    int world_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    const char *map_path = NULL;
    const char *agents_path = NULL;
    double timeout_seconds = 0.0;
    const char *csv_path = "results_decentral.csv";
    double suboptimality = 1.0;

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
        else if (strcmp(argv[i], "--w") == 0 && i + 1 < argc)
        {
            suboptimality = atof(argv[++i]);
            if (suboptimality < 1.0)
            {
                suboptimality = 1.0;
            }
        }
    }

    int config_ok = 1;
    if (world_rank == 0)
    {
        if (!map_path || !agents_path)
        {
            fprintf(stderr, "Usage: mpirun -n <procs> decentralized_cbs --map map.txt --agents agents.txt [--timeout SEC] [--csv path] [--w bound]\n");
            config_ok = 0;
        }
        if (world_size < 1)
        {
            fprintf(stderr, "At least one MPI rank is required.\n");
            config_ok = 0;
        }
    }
    MPI_Bcast(&config_ok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!config_ok)
    {
        MPI_Finalize();
        return 1;
    }

    ProblemInstance instance;
    memset(&instance, 0, sizeof(ProblemInstance));
    int load_success = 1;
    if (world_rank == 0)
    {
        if (!load_problem_instance(map_path, agents_path, &instance))
        {
            fprintf(stderr, "Failed to load problem instance.\n");
            load_success = 0;
        }
    }
    MPI_Bcast(&load_success, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!load_success)
    {
        MPI_Finalize();
        return 1;
    }

    broadcast_instance(&instance, 0, MPI_COMM_WORLD);

    LowLevelContext ll_ctx = {.manager_world_rank = -1, .pool_comm = MPI_COMM_NULL};

    HighLevelNode *root = cbs_node_create(instance.num_agents);
    root->id = 0;
    root->depth = 0;
    root->parent_id = -1;
    int root_ok = 1;
    for (int agent = 0; agent < instance.num_agents; ++agent)
    {
        if (!low_level_request_path(&instance, &root->constraints, agent, &ll_ctx, &root->paths[agent]))
        {
            root_ok = 0;
            break;
        }
    }
    root->cost = cbs_compute_soc(root);
    printf("[Decentral %d] Root ready cost=%.0f agents=%d\n", world_rank, root->cost, instance.num_agents);
    fflush(stdout);

    int all_root_ok = 0;
    MPI_Allreduce(&root_ok, &all_root_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    if (!all_root_ok)
    {
        if (world_rank == 0)
        {
            fprintf(stderr, "Failed to compute initial paths.\n");
        }
        cbs_node_free(root);
        problem_instance_free(&instance);
        MPI_Finalize();
        return 1;
    }

    PriorityQueue open;
    pq_init(&open);
    pq_push(&open, root->cost, root);

    /* Initialize pending send pool for async MPI operations */
    PendingSendPool send_pool;
    pending_send_pool_init(&send_pool);

    double start_time = MPI_Wtime();
    long long nodes_expanded = 0;
    long long nodes_generated = 0;
    // long long max_nodes_expanded = 20000;
    long long conflicts_detected = 0;
    int timed_out = 0;
    double local_solution_cost = DBL_MAX;
    double local_comm_time = 0.0;  /* Track MPI communication time */

    int rr_dest = (world_rank + 1) % world_size;

    int active = 1;
    while (active)
    {
        /* Check local timeout and broadcast to all processes so they exit together */
        double elapsed = MPI_Wtime() - start_time;
        int local_timeout = (timeout_seconds > 0.0 && elapsed > timeout_seconds) ? 1 : 0;
        int any_timeout = 0;
        double comm_start = MPI_Wtime();
        MPI_Allreduce(&local_timeout, &any_timeout, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
        local_comm_time += MPI_Wtime() - comm_start;
        
        if (any_timeout)
        {
            timed_out = 1;
            printf("[Decentral %d] TIMEOUT at %.2fs (coordinated exit)\n", world_rank, elapsed);
            fflush(stdout);
            break;
        }
        
        receive_buffered_nodes(&open, world_rank, &local_comm_time);

        double local_lb = DBL_MAX;
        if (open.count > 0)
        {
            double key = 0.0;
            pq_peek(&open, &key);
            local_lb = key;
        }

        double global_lb = DBL_MAX;
        comm_start = MPI_Wtime();
        MPI_Allreduce(&local_lb, &global_lb, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
        local_comm_time += MPI_Wtime() - comm_start;

        double global_sol = DBL_MAX;
        comm_start = MPI_Wtime();
        MPI_Allreduce(&local_solution_cost, &global_sol, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
        local_comm_time += MPI_Wtime() - comm_start;
        if (global_sol < DBL_MAX / 2.0)
        {
            printf("[Decentral %d] Global solution found: %.0f\n", world_rank, global_sol);
            fflush(stdout);
            break;
        }

        if (global_lb >= DBL_MAX / 2.0)
        {
            /* All queues empty and no solution */
            printf("[Decentral %d] All queues empty, no solution\n", world_rank);
            fflush(stdout);
            break;
        }

        double bound = suboptimality * global_lb;

        if (open.count == 0)
        {
            /* Wait for more work */
            printf("[Decentral %d] Queue empty, waiting for work (lb=%.0f)\n", world_rank, global_lb);
            fflush(stdout);
            continue;
        }

        double key = 0.0;
        HighLevelNode *node = (HighLevelNode *)pq_pop(&open, &key);
        if (node->cost > bound + 1e-6)
        {
            /* Not eligible yet; reinsert and wait for bound to catch up */
            pq_push(&open, node->cost, node);
            continue;
        }

        nodes_expanded++;
        printf("[Decentral %d] Expanding node id=%d depth=%d cost=%.0f bound=%.0f lb=%.0f\n",
               world_rank,
               node->id,
               node->depth,
               node->cost,
               bound,
               global_lb);
        fflush(stdout);

        Conflict conflict;
        if (!cbs_detect_conflict(node, &conflict))
        {
            local_solution_cost = node->cost;
            printf("[Decentral %d] Found solution cost=%.0f depth=%d\n",
                   world_rank,
                   node->cost,
                   node->depth);
            fflush(stdout);
            cbs_node_free(node);
            continue;
        }

        conflicts_detected++;
        printf("[Decentral %d] Conflict agents=(%d,%d) time=%d, generating children\n",
               world_rank, conflict.agent_a, conflict.agent_b, conflict.time);
        fflush(stdout);
        
        int child_agents[2] = {conflict.agent_a, conflict.agent_b};
        for (int idx = 0; idx < 2; ++idx)
        {
            printf("[Decentral %d] Processing child %d for agent %d\n", world_rank, idx, child_agents[idx]);
            fflush(stdout);
            
            // CRITICAL: Drain incoming messages to prevent send deadlock
            receive_buffered_nodes(&open, world_rank, &local_comm_time);
            
            HighLevelNode *child = clone_parent_node(node);
            if (!child)
            {
                printf("[Decentral %d] Failed to clone parent node\n", world_rank);
                fflush(stdout);
                continue;
            }
            Constraint c = conflict.is_vertex_conflict
                               ? make_vertex_constraint(&conflict, child_agents[idx])
                               : make_edge_constraint(node, &conflict, child_agents[idx]);
            constraint_set_add(&child->constraints, c);

            printf("[Decentral %d] Calling replan for agent %d\n", world_rank, child_agents[idx]);
            fflush(stdout);

            if (!replan_agent_path(&instance, child, child_agents[idx], &ll_ctx))
            {
                printf("[Decentral %d] Replan FAILED for agent %d, discarding child\n", world_rank, child_agents[idx]);
                fflush(stdout);
                cbs_node_free(child);
                continue;
            }

            child->cost = cbs_compute_soc(child);
            int dest = rr_dest;
            rr_dest = (rr_dest + 1) % world_size;
            
            printf("[Decentral %d] Child ready cost=%.0f, dest=%d (self=%d)\n",
                   world_rank, child->cost, dest, world_rank);
            fflush(stdout);
            
            if (dest == world_rank)
            {
                pq_push(&open, child->cost, child);
                printf("[Decentral %d] Pushed child to local queue\n", world_rank);
                fflush(stdout);
            }
            else
            {
                printf("[Decentral %d] About to push_child to rank %d\n", world_rank, dest);
                fflush(stdout);
                push_child(child, dest, &send_pool);
                printf("[Decentral %d] push_child completed to rank %d\n", world_rank, dest);
                fflush(stdout);
                cbs_node_free(child);
            }
            nodes_generated++;
            
            /* Progress sends and receive any incoming nodes */
            pending_send_pool_progress(&send_pool);
            receive_buffered_nodes(&open, world_rank, &local_comm_time);
        }
        
        printf("[Decentral %d] Finished generating children, freeing parent node\n", world_rank);
        fflush(stdout);

        cbs_node_free(node);
    }

    /* Wait for any pending async sends to complete before cleanup */
    pending_send_pool_wait_all(&send_pool);

    /* Cleanup remaining queued nodes */
    while (open.count > 0)
    {
        double dummy = 0.0;
        HighLevelNode *node = (HighLevelNode *)pq_pop(&open, &dummy);
        cbs_node_free(node);
    }
    pq_free(&open);

    double runtime = MPI_Wtime() - start_time;

    long long total_expanded = 0;
    long long total_generated = 0;
    long long total_conflicts = 0;
    double total_comm_time = 0.0;
    int any_timeout = 0;
    MPI_Reduce(&nodes_expanded, &total_expanded, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&nodes_generated, &total_generated, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&conflicts_detected, &total_conflicts, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_comm_time, &total_comm_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Allreduce(&timed_out, &any_timeout, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

    double global_solution = DBL_MAX;
    MPI_Allreduce(&local_solution_cost, &global_solution, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);

    if (world_rank == 0)
    {
        const char *map_name = map_path ? strrchr(map_path, '/') : NULL;
        map_name = map_name ? map_name + 1 : map_path ? map_path : "unknown";
        int need_header = access(csv_path, F_OK) != 0;
        FILE *fp = fopen(csv_path, "a");
        /* For decentralized: avg comm time per process, compute time = runtime - avg_comm */
        double avg_comm_time = total_comm_time / world_size;
        double compute_time = runtime - avg_comm_time;
        if (fp)
        {
            if (need_header)
            {
                fprintf(fp, "map,agents,width,height,nodes_expanded,nodes_generated,conflicts,cost,runtime_sec,comm_time_sec,compute_time_sec,timeout_sec,status\n");
            }
            const char *status = (global_solution < DBL_MAX / 2.0) ? "success" : (any_timeout ? "timeout" : "failure");
            double cost_out = (global_solution < DBL_MAX / 2.0) ? global_solution : -1.0;
            fprintf(fp,
                    "%s,%d,%d,%d,%lld,%lld,%lld,%.0f,%.6f,%.6f,%.6f,%.2f,%s\n",
                    map_name,
                    instance.num_agents,
                    instance.map.width,
                    instance.map.height,
                    total_expanded,
                    total_generated,
                    total_conflicts,
                    cost_out,
                    runtime,
                    avg_comm_time,
                    compute_time,
                    timeout_seconds,
                    status);
            fclose(fp);
        }
        else
        {
            fprintf(stderr, "Warning: could not open CSV file %s for writing.\n", csv_path);
        }

        if (global_solution < DBL_MAX / 2.0)
        {
            printf("[Decentral] Found solution cost=%.0f (expanded=%lld, comm=%.3fs, compute=%.3fs)\n", 
                   global_solution, total_expanded, avg_comm_time, compute_time);
        }
        else
        {
            printf("[Decentral] No solution found (expanded=%lld, status=%s)\n",
                   total_expanded,
                   any_timeout ? "timeout" : "failure");
        }
    }

    problem_instance_free(&instance);
    MPI_Finalize();
    return 0;
}
