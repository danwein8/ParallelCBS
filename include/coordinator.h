#ifndef PARALLEL_CBS_COORDINATOR_H
#define PARALLEL_CBS_COORDINATOR_H

#include "cbs.h"
#include "low_level.h"

typedef struct
{
    int *ranks;
    int count;
} WorkerSet;

void run_coordinator(const ProblemInstance *instance,
                     const LowLevelContext *ll_ctx,
                     const WorkerSet *workers);

#endif /* PARALLEL_CBS_COORDINATOR_H */
