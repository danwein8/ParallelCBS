#ifndef PARALLEL_CBS_LOW_LEVEL_H
#define PARALLEL_CBS_LOW_LEVEL_H

#include "cbs.h"
#include "constraints.h"
#include "messages.h"
#include "parallel_a_star.h"

typedef struct
{
    int manager_world_rank;
    MPI_Comm pool_comm;
} LowLevelContext;

void low_level_service_loop(const ProblemInstance *instance, const LowLevelContext *ctx);
bool low_level_request_path(const ProblemInstance *instance,
                            const ConstraintSet *constraints,
                            int agent_id,
                            const LowLevelContext *ctx,
                            AgentPath *out_path);
void low_level_request_shutdown(const LowLevelContext *ctx);

#endif /* PARALLEL_CBS_LOW_LEVEL_H */
