#ifndef PARALLEL_CBS_COORDINATOR_H
#define PARALLEL_CBS_COORDINATOR_H

#include "cbs.h"
#include "low_level.h"

typedef struct
{
    int *ranks;
    int count;
} WorkerSet;

typedef struct
{
    long long nodes_expanded;
    long long nodes_generated;
    long long conflicts_detected;
    double best_cost;
    int solution_found;
    int timed_out;
    double runtime_sec;
    double comm_time_sec;    /* Time spent in MPI communication */
    double compute_time_sec; /* Time spent in CBS computation */
} RunStats;

void run_coordinator(const ProblemInstance *instance,
                     const LowLevelContext *ll_ctx,
                     const WorkerSet *workers,
                     double timeout_seconds,
                     RunStats *stats);

#endif /* PARALLEL_CBS_COORDINATOR_H */
