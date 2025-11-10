#ifndef PARALLEL_CBS_INSTANCE_IO_H
#define PARALLEL_CBS_INSTANCE_IO_H

#include "cbs.h"

bool load_problem_instance(const char *map_path,
                           const char *agents_path,
                           ProblemInstance *instance);

#endif /* PARALLEL_CBS_INSTANCE_IO_H */
