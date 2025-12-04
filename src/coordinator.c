#include "coordinator.h"

#include "messages.h"
#include "priority_queue.h"
#include "serialization.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
                          int worker_rank,
                          PendingSendPool *pool)
{
    SerializedNode payload;
    serialize_high_level_node(node, &payload);
    payload.aux_value = (int)(incumbent_cost >= (double)INT_MAX ? INT_MAX : (int)ceil(incumbent_cost));
    send_serialized_node_async(worker_rank, TAG_TASK, &payload, pool);
    free_serialized_node(&payload);
}

void run_coordinator(const ProblemInstance *instance,
                     const LowLevelContext *ll_ctx,
                     const WorkerSet *workers,
                     double timeout_seconds,
                     RunStats *stats)
{
    int coord_rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &coord_rank);

    double start_time = MPI_Wtime();
    int timed_out = 0;
    if (stats)
    {
        memset(stats, 0, sizeof(RunStats));
        stats->best_cost = DBL_MAX;
    }

    if (workers->count == 0)
    {
        if (coord_rank == 0)
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
    printf("[Coordinator %d] Root node ready: id=%d cost=%.0f agents=%d\n",
           coord_rank,
           root->id,
           root->cost,
           instance->num_agents);
    fflush(stdout);

    /* Initialize pending send pool for async MPI operations */
    PendingSendPool send_pool;
    pending_send_pool_init(&send_pool);

    double incumbent_cost = DBL_MAX;
    HighLevelNode *incumbent_solution = NULL;
    int next_node_id = 1;
    int rr_index = 0;
    long long nodes_expanded = 0;
    long long nodes_generated = 0;
    long long conflicts_detected = 0;
    long long loop_iterations = 0;
    double last_status_time = start_time;
    int outstanding = 0;  /* Track outstanding workers at function scope for timeout_exit */

    while (open.count > 0)
    {
        loop_iterations++;
        double elapsed = MPI_Wtime() - start_time;
        
        if (timeout_seconds > 0.0 && elapsed > timeout_seconds)
        {
            timed_out = 1;
            printf("[Coordinator %d] TIMEOUT at %.2fs (limit=%.2fs) after %lld iterations\n", 
                   coord_rank, elapsed, timeout_seconds, loop_iterations);
            fflush(stdout);
            break;
        }
        
        // Periodic status update every 5 seconds
        if (elapsed - last_status_time >= 5.0)
        {
            printf("[Coordinator %d] STATUS: elapsed=%.1fs, open=%d, expanded=%lld, generated=%lld, incumbent=%s\n",
                   coord_rank, elapsed, open.count, nodes_expanded, nodes_generated,
                   incumbent_cost < DBL_MAX ? "found" : "none");
            fflush(stdout);
            last_status_time = elapsed;
        }

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
                HighLevelNode **new_plateau = (HighLevelNode **)realloc(plateau, sizeof(HighLevelNode *) * (size_t)plateau_capacity);
                if (!new_plateau)
                {
                    fprintf(stderr, "coordinator: failed to allocate plateau memory (size=%d)\n", plateau_capacity);
                    free(plateau);
                    exit(EXIT_FAILURE);
                }
                plateau = new_plateau;
            }
            plateau[plateau_size++] = equal_node;
        }

        nodes_expanded += plateau_size;
        char incumbent_str[32];
        if (incumbent_cost < DBL_MAX)
        {
            snprintf(incumbent_str, sizeof(incumbent_str), "%.0f", incumbent_cost);
        }
        else
        {
            strcpy(incumbent_str, "INF");
        }
        printf("[Coordinator %d] Dispatching %d node(s) at cost %.0f (incumbent=%s)\n",
               coord_rank,
               plateau_size,
               plateau_cost,
               incumbent_str);
        fflush(stdout);

        outstanding = plateau_size;  /* Assign (not declare) to use function-scope variable */
        for (int i = 0; i < plateau_size; ++i)
        {
            int worker_rank = select_worker(workers, &rr_index);
            printf("[Coordinator %d] -> Worker %d: node id=%d depth=%d cost=%.0f\n",
                   coord_rank,
                   worker_rank,
                   plateau[i]->id,
                   plateau[i]->depth,
                   plateau[i]->cost);
            fflush(stdout);
            dispatch_node(plateau[i], incumbent_cost, worker_rank, &send_pool);
            cbs_node_free(plateau[i]);
        }

        free(plateau);
        printf("[Coordinator %d] Waiting for %d worker response(s)...\n", coord_rank, outstanding);
        fflush(stdout);

        while (outstanding > 0)
        {
            // CHECK TIMEOUT INSIDE THE WAITING LOOP
            double wait_elapsed = MPI_Wtime() - start_time;
            if (timeout_seconds > 0.0 && wait_elapsed > timeout_seconds)
            {
                timed_out = 1;
                printf("[Coordinator %d] TIMEOUT in worker wait loop at %.2fs (outstanding=%d)\n",
                       coord_rank, wait_elapsed, outstanding);
                fflush(stdout);
                goto timeout_exit;
            }
            
            // Use NON-BLOCKING probe so we can check timeout
            MPI_Status status;
            int flag = 0;
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
            
            if (!flag)
            {
                // No message available, sleep briefly and check timeout again
                usleep(1000);  // Sleep 1ms to avoid busy-waiting
                // Progress any pending async sends
                pending_send_pool_progress(&send_pool);
                continue;
            }
            
            printf("[Coordinator %d] [t=%.1fs] Received message (tag=%d) from rank %d\n",
                   coord_rank, wait_elapsed, status.MPI_TAG, status.MPI_SOURCE);
            fflush(stdout);
            
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
                    printf("[Coordinator %d] New incumbent: node id=%d cost=%.0f depth=%d\n",
                           coord_rank,
                           solution_node->id,
                           solution_node->cost,
                           solution_node->depth);
                    fflush(stdout);
                }
                outstanding--;
                printf("[Coordinator %d] Outstanding workers remaining: %d\n", coord_rank, outstanding);
                fflush(stdout);
            }
            else if (status.MPI_TAG == TAG_CHILDREN)
            {
                int source_worker = status.MPI_SOURCE;
                int child_count = 0;
                MPI_Recv(&child_count, 1, MPI_INT, source_worker, TAG_CHILDREN, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                nodes_generated += child_count;
                if (child_count > 0)
                {
                    conflicts_detected++;
                }
                printf("[Coordinator %d] Received %d children from worker %d\n",
                       coord_rank, child_count, source_worker);
                fflush(stdout);
                
                for (int i = 0; i < child_count; ++i)
                {
                    SerializedNode child_data;
                    receive_serialized_node(source_worker, TAG_CHILDREN, &child_data, NULL);
                    int parent_id = child_data.aux_value;
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
                        printf("[Coordinator %d] Received child id=%d (parent=%d) cost=%.0f depth=%d\n",
                               coord_rank,
                               child->id,
                               parent_id,
                               child->cost,
                               child->depth);
                        fflush(stdout);
                    }
                    else
                    {
                        printf("[Coordinator %d] Pruned child (parent=%d) cost=%.0f >= incumbent %.0f\n",
                               coord_rank, parent_id, child->cost, incumbent_cost);
                        fflush(stdout);
                        cbs_node_free(child);
                    }
                }
                outstanding--;
                printf("[Coordinator %d] Outstanding workers remaining: %d\n", coord_rank, outstanding);
                fflush(stdout);
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

timeout_exit:
    /* Drain any remaining results from outstanding workers before terminating */
    printf("[Coordinator %d] Draining remaining results from workers (outstanding=%d)...\n", 
           coord_rank, outstanding);
    fflush(stdout);
    
    /* Keep receiving until we've heard back from all outstanding workers, with a secondary timeout */
    double drain_start = MPI_Wtime();
    double drain_timeout = 5.0;  /* Max 5 seconds to drain */
    
    while (outstanding > 0)
    {
        double drain_elapsed = MPI_Wtime() - drain_start;
        if (drain_elapsed > drain_timeout)
        {
            printf("[Coordinator %d] Drain timeout after %.2fs, %d workers still outstanding\n",
                   coord_rank, drain_elapsed, outstanding);
            fflush(stdout);
            break;
        }
        
        MPI_Status status;
        int flag = 0;
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
        
        if (!flag)
        {
            usleep(1000);  /* 1ms */
            pending_send_pool_progress(&send_pool);
            continue;
        }
        
        if (status.MPI_TAG == TAG_SOLUTION)
        {
            SerializedNode solution_data;
            receive_serialized_node(status.MPI_SOURCE, TAG_SOLUTION, &solution_data, NULL);
            free_serialized_node(&solution_data);
            outstanding--;
            printf("[Coordinator %d] Drained solution from worker %d, outstanding=%d\n",
                   coord_rank, status.MPI_SOURCE, outstanding);
            fflush(stdout);
        }
        else if (status.MPI_TAG == TAG_CHILDREN)
        {
            int source_worker = status.MPI_SOURCE;
            int child_count = 0;
            MPI_Recv(&child_count, 1, MPI_INT, source_worker, TAG_CHILDREN, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            for (int i = 0; i < child_count; ++i)
            {
                SerializedNode child_data;
                receive_serialized_node(source_worker, TAG_CHILDREN, &child_data, NULL);
                free_serialized_node(&child_data);
            }
            outstanding--;
            printf("[Coordinator %d] Drained %d children from worker %d, outstanding=%d\n",
                   coord_rank, child_count, source_worker, outstanding);
            fflush(stdout);
        }
    }
    
    printf("[Coordinator %d] All workers drained, sending termination...\n", coord_rank);
    fflush(stdout);
    
    /* Wait for any pending async sends to complete before terminating workers */
    pending_send_pool_wait_all(&send_pool);

    for (int i = 0; i < workers->count; ++i)
    {
        MPI_Send(NULL, 0, MPI_INT, workers->ranks[i], TAG_TERMINATE, MPI_COMM_WORLD);
    }

    if (incumbent_solution)
    {
        printf("Best solution cost: %.0f\n", incumbent_solution->cost);
        printf("[Coordinator %d] Solution found with node id=%d depth=%d\n",
               coord_rank,
               incumbent_solution->id,
               incumbent_solution->depth);
        fflush(stdout);
        cbs_node_free(incumbent_solution);
    }
    else
    {
        printf("[Coordinator %d] Search finished without finding a solution.\n", coord_rank);
        fflush(stdout);
    }

    while (open.count > 0)
    {
        double dummy = 0.0;
        HighLevelNode *node = (HighLevelNode *)pq_pop(&open, &dummy);
        cbs_node_free(node);
    }
    pq_free(&open);

    if (stats)
    {
        stats->nodes_expanded = nodes_expanded;
        stats->nodes_generated = nodes_generated;
        stats->conflicts_detected = conflicts_detected;
        stats->best_cost = incumbent_cost;
        stats->solution_found = incumbent_solution != NULL;
        stats->timed_out = timed_out;
        stats->runtime_sec = MPI_Wtime() - start_time;
    }
}

// void run_coordinator(const ProblemInstance *instance,
//                      const LowLevelContext *ll_ctx,
//                      const WorkerSet *workers,
//                      double timeout_seconds,
//                      RunStats *stats)
// {
//     int coord_rank = 0;
//     MPI_Comm_rank(MPI_COMM_WORLD, &coord_rank);

//     double start_time = MPI_Wtime();
//     int timed_out = 0;
//     if (stats)
//     {
//         memset(stats, 0, sizeof(RunStats));
//         stats->best_cost = DBL_MAX;
//     }

//     if (workers->count == 0)
//     {
//         if (coord_rank == 0)
//         {
//             fprintf(stderr, "No expansion workers available.\n");
//         }
//         return;
//     }

//     PriorityQueue open;
//     pq_init(&open);

//     HighLevelNode *root = cbs_node_create(instance->num_agents);
//     root->id = 0;
//     root->depth = 0;
//     root->parent_id = -1;

//     if (!initialize_root(instance, ll_ctx, root))
//     {
//         fprintf(stderr, "Failed to compute initial paths.\n");
//         cbs_node_free(root);
//         pq_free(&open);
//         return;
//     }

//     pq_push(&open, root->cost, root);
//     printf("[Coordinator %d] Root node ready: id=%d cost=%.0f agents=%d\n",
//            coord_rank,
//            root->id,
//            root->cost,
//            instance->num_agents);
//     fflush(stdout);

//     double incumbent_cost = DBL_MAX;
//     HighLevelNode *incumbent_solution = NULL;
//     int next_node_id = 1;
//     int rr_index = 0;
//     long long nodes_expanded = 0;
//     long long nodes_generated = 0;
//     long long conflicts_detected = 0;
//     long long loop_iterations = 0;
//     double last_status_time = start_time;

//     while (open.count > 0)
//     {
//         loop_iterations++;
//         double elapsed = MPI_Wtime() - start_time;
//         if (timeout_seconds > 0.0 && elapsed > timeout_seconds)
//         {
//             timed_out = 1;
//             printf("[Coordinator %d] TIMEOUT at %.2fs (limit=%.2fs) after %lld iterations\n", 
//                    coord_rank, elapsed, timeout_seconds, loop_iterations);
//             fflush(stdout);
//             break;
//         }
//         // Periodic status update every 5 seconds
//         if (elapsed - last_status_time >= 5.0)
//         {
//             printf("[Coordinator %d] STATUS: elapsed=%.1fs, open=%d, expanded=%lld, generated=%lld, incumbent=%s\n",
//                    coord_rank, elapsed, open.count, nodes_expanded, nodes_generated,
//                    incumbent_cost < DBL_MAX ? "found" : "none");
//             fflush(stdout);
//             last_status_time = elapsed;
//         }
//         // if (timeout_seconds > 0.0 && MPI_Wtime() - start_time > timeout_seconds)
//         // {
//         //     timed_out = 1;
//         //     printf("[Coordinator %d] Timeout reached (%.2f s). Aborting search.\n", coord_rank, timeout_seconds);
//         //     fflush(stdout);
//         //     break;
//         // }

//         double plateau_cost = 0.0;
//         double key = 0.0;
//         HighLevelNode *front = (HighLevelNode *)pq_pop(&open, &plateau_cost);
//         int plateau_capacity = workers->count > 0 ? workers->count : 1;
//         HighLevelNode **plateau = (HighLevelNode **)malloc(sizeof(HighLevelNode *) * (size_t)plateau_capacity);
//         int plateau_size = 0;
//         plateau[plateau_size++] = front;

//         while (open.count > 0)
//         {
//             HighLevelNode *peek = (HighLevelNode *)pq_peek(&open, &key);
//             if (fabs(peek->cost - plateau_cost) > 1e-6)
//             {
//                 break;
//             }
//             HighLevelNode *equal_node = (HighLevelNode *)pq_pop(&open, &key);
//             if (plateau_size >= plateau_capacity)
//             {
//                 plateau_capacity *= 2;
//                 HighLevelNode **new_plateau = (HighLevelNode **)realloc(plateau, sizeof(HighLevelNode *) * (size_t)plateau_capacity);
//                 if (!new_plateau)
//                 {
//                     fprintf(stderr, "coordinator: failed to allocate plateau memory (size=%d)\n", plateau_capacity);
//                     free(plateau);
//                     exit(EXIT_FAILURE);
//                 }
//                 plateau = new_plateau;
//             }
//             plateau[plateau_size++] = equal_node;
//         }

//         nodes_expanded += plateau_size;
//         char incumbent_str[32];
//         if (incumbent_cost < DBL_MAX)
//         {
//             snprintf(incumbent_str, sizeof(incumbent_str), "%.0f", incumbent_cost);
//         }
//         else
//         {
//             strcpy(incumbent_str, "INF");
//         }
//         printf("[Coordinator %d] Dispatching %d node(s) at cost %.0f (incumbent=%s)\n",
//                coord_rank,
//                plateau_size,
//                plateau_cost,
//                incumbent_str);
//         fflush(stdout);

//         int outstanding = plateau_size;
//         for (int i = 0; i < plateau_size; ++i)
//         {
//             int worker_rank = select_worker(workers, &rr_index);
//             printf("[Coordinator %d] -> Worker %d: node id=%d depth=%d cost=%.0f\n",
//                    coord_rank,
//                    worker_rank,
//                    plateau[i]->id,
//                    plateau[i]->depth,
//                    plateau[i]->cost);
//             fflush(stdout);
//             dispatch_node(plateau[i], incumbent_cost, worker_rank);
//             cbs_node_free(plateau[i]);
//         }

//         free(plateau);
//         printf("[Coordinator %d] Waiting for %d worker response(s)...\n", coord_rank, outstanding);
//         fflush(stdout);

//         while (outstanding > 0)
//         {
//             // CHECK TIMEOUT INSIDE THE WAITING LOOP!
//             double elapsed = MPI_Wtime() - start_time;
//             if (timeout_seconds > 0.0 && elapsed > timeout_seconds)
//             {
//                 timed_out = 1;
//                 printf("[Coordinator %d] TIMEOUT in worker wait loop at %.2fs (outstanding=%d)\n",
//                        coord_rank, elapsed, outstanding);
//                 fflush(stdout);
//                 // Break out of both loops
//                 goto timeout_exit;
//             }
            
//             // MPI_Status status;
//             // MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
//             // double wait_elapsed = MPI_Wtime() - start_time;
//             // printf("[Coordinator %d] [t=%.1fs] Received message (tag=%d) from rank %d\n",
//             //        coord_rank, wait_elapsed, status.MPI_TAG, status.MPI_SOURCE);
//             // fflush(stdout);
//             // if (status.MPI_TAG == TAG_SOLUTION)

//             // Use NON-BLOCKING probe with timeout
//             MPI_Status status;
//             int flag = 0;
//             MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
            
//             if (!flag)
//             {
//                 // No message available, sleep briefly and continue
//                 usleep(1000);  // Sleep 1ms to avoid busy-waiting
//                 continue;
//             }
            
//             double wait_elapsed = MPI_Wtime() - start_time;
//             printf("[Coordinator %d] [t=%.1fs] Received message (tag=%d) from rank %d\n",
//                    coord_rank, wait_elapsed, status.MPI_TAG, status.MPI_SOURCE);
//             fflush(stdout);
//             {
//                 SerializedNode solution_data;
//                 receive_serialized_node(status.MPI_SOURCE, TAG_SOLUTION, &solution_data, NULL);
//                 HighLevelNode *solution_node = deserialize_high_level_node(&solution_data);
//                 free_serialized_node(&solution_data);
//                 if (solution_node)
//                 {
//                     solution_node->id = next_node_id++;
//                     solution_node->cost = cbs_compute_soc(solution_node);
//                     if (incumbent_solution != NULL)
//                     {
//                         cbs_node_free(incumbent_solution);
//                     }
//                     incumbent_solution = solution_node;
//                     incumbent_cost = solution_node->cost;
//                     printf("[Coordinator %d] New incumbent: node id=%d cost=%.0f depth=%d\n",
//                            coord_rank,
//                            solution_node->id,
//                            solution_node->cost,
//                            solution_node->depth);
//                     fflush(stdout);
//                 }
//                 outstanding--;
//                 printf("[Coordinator %d] Outstanding workers remaining: %d\n", coord_rank, outstanding);
//                 fflush(stdout);
//             }
//             else if (status.MPI_TAG == TAG_CHILDREN)
//             {
//                 int child_count = 0;
//                 MPI_Recv(&child_count, 1, MPI_INT, status.MPI_SOURCE, TAG_CHILDREN, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
//                 nodes_generated += child_count;
//                 if (child_count > 0)
//                 {
//                     conflicts_detected++;
//                 }
//                 printf("[Coordinator %d] Received %d children from worker %d\n",
//                        coord_rank, child_count, status.MPI_SOURCE);
//                 fflush(stdout);
                
//                 int source_worker = status.MPI_SOURCE;  // CRITICAL: Fix source
                
//                 for (int i = 0; i < child_count; ++i)
//                 {
//                     SerializedNode child_data;
//                     // CRITICAL: Receive from the SAME source, not ANY_SOURCE!
//                     receive_serialized_node(source_worker, TAG_CHILDREN, &child_data, NULL);
//                     int parent_id = child_data.aux_value;
//                     HighLevelNode *child = deserialize_high_level_node(&child_data);
//                     free_serialized_node(&child_data);
//                     if (!child)
//                     {
//                         continue;
//                     }
//                     child->id = next_node_id++;
//                     child->cost = cbs_compute_soc(child);
//                     if (child->cost < incumbent_cost)
//                     {
//                         pq_push(&open, child->cost, child);
//                         printf("[Coordinator %d] Received child id=%d (parent=%d) cost=%.0f depth=%d\n",
//                                coord_rank,
//                                child->id,
//                                parent_id,
//                                child->cost,
//                                child->depth);
//                         fflush(stdout);
//                     }
//                     else
//                     {
//                         printf("[Coordinator %d] Pruned child (parent=%d) cost=%.0f >= incumbent %.0f\n",
//                                coord_rank, parent_id, child->cost, incumbent_cost);
//                         fflush(stdout);
//                         cbs_node_free(child);
//                     }
//                 }
//                 outstanding--;
//                 printf("[Coordinator %d] Outstanding workers remaining: %d\n", coord_rank, outstanding);
//                 fflush(stdout);
//             }
//         }
//         timeout_exit:  // Jump here if timeout occurs in worker wait loop

//         if (incumbent_solution != NULL)
//         {
//             double peek_cost = 0.0;
//             HighLevelNode *next = (HighLevelNode *)pq_peek(&open, &peek_cost);
//             if (!next || peek_cost >= incumbent_cost - 1e-6)
//             {
//                 break;
//             }
//         }
//     }

//     for (int i = 0; i < workers->count; ++i)
//     {
//         MPI_Send(NULL, 0, MPI_INT, workers->ranks[i], TAG_TERMINATE, MPI_COMM_WORLD);
//     }

//     if (incumbent_solution)
//     {
//         printf("Best solution cost: %.0f\n", incumbent_solution->cost);
//         printf("[Coordinator %d] Solution found with node id=%d depth=%d\n",
//                coord_rank,
//                incumbent_solution->id,
//                incumbent_solution->depth);
//         fflush(stdout);
//         cbs_node_free(incumbent_solution);
//     }
//     else
//     {
//         printf("[Coordinator %d] Search finished without finding a solution.\n", coord_rank);
//         fflush(stdout);
//     }

//     while (open.count > 0)
//     {
//         double dummy = 0.0;
//         HighLevelNode *node = (HighLevelNode *)pq_pop(&open, &dummy);
//         cbs_node_free(node);
//     }
//     pq_free(&open);

//     if (stats)
//     {
//         stats->nodes_expanded = nodes_expanded;
//         stats->nodes_generated = nodes_generated;
//         stats->conflicts_detected = conflicts_detected;
//         stats->best_cost = incumbent_cost;
//         stats->solution_found = incumbent_solution != NULL;
//         stats->timed_out = timed_out;
//         stats->runtime_sec = MPI_Wtime() - start_time;
//     }
// }
