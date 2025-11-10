#ifndef PARALLEL_CBS_CBS_H
#define PARALLEL_CBS_CBS_H

#include "common.h"
#include "constraints.h"
#include "grid.h"

typedef struct
{
    int agent_a;
    int agent_b;
    int time;
    GridCoord position;
    bool is_vertex_conflict;
    GridCoord edge_to;
} Conflict;

typedef struct
{
    int id;
    int parent_id;
    int depth;
    double cost;
    ConstraintSet constraints;
    AgentPath *paths;
    int num_agents;
} HighLevelNode;

typedef struct
{
    Grid map;
    GridCoord *starts;
    GridCoord *goals;
    int num_agents;
} ProblemInstance;

void problem_instance_init(ProblemInstance *instance, int num_agents);
void problem_instance_free(ProblemInstance *instance);

HighLevelNode *cbs_node_create(int num_agents);
void cbs_node_free(HighLevelNode *node);
double cbs_compute_soc(const HighLevelNode *node);
bool cbs_detect_conflict(const HighLevelNode *node, Conflict *conflict);

#endif /* PARALLEL_CBS_CBS_H */
