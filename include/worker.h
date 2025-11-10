#ifndef PARALLEL_CBS_WORKER_H
#define PARALLEL_CBS_WORKER_H

#include "cbs.h"
#include "low_level.h"

void run_worker(const ProblemInstance *instance,
                const LowLevelContext *ll_ctx,
                int coordinator_rank);

#endif /* PARALLEL_CBS_WORKER_H */
