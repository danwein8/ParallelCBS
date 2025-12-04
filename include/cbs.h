#ifndef PARALLEL_CBS_CBS_H
#define PARALLEL_CBS_CBS_H

#include "common.h"
#include "constraints.h"
#include "grid.h"

/*
Conflict structure representing a conflict between two agents
*/
typedef struct
{
    /** First agent involved in the conflict */
    int agent_a;
    /** Second agent involved in the conflict */
    int agent_b;
    /** Time step of the conflict */
    int time;
    /** Position of the conflict */
    GridCoord position;
    /** Whether the conflict is a vertex conflict */
    bool is_vertex_conflict;
    /** Position of the edge to which the conflict occurs (for edge conflicts) */
    GridCoord edge_to;
} Conflict;

/* 
Single HighLevelNode structue, element of constraint tree
*/
typedef struct
{
    /** Unique identifier for the node */
    int id;
    /** Identifier of the parent node */
    int parent_id;
    /** Depth of the node in the constraint tree */
    int depth;
    /** Cost associated with the node */
    double cost;
    /** Set of constraints associated with the node */
    ConstraintSet constraints;
    /** Paths for all agents in the node */
    AgentPath *paths;
    /** Number of agents */
    int num_agents;
} HighLevelNode;

/* 
Full MAPF problem instance
*/
typedef struct
{
    /** Grid representing the map */
    Grid map;
    /** Starting positions of the agents */
    GridCoord *starts;
    /** Goal positions of the agents */
    GridCoord *goals;
    /** Number of agents */
    int num_agents;
} ProblemInstance;

void problem_instance_init(ProblemInstance *instance, int num_agents);
void problem_instance_free(ProblemInstance *instance);

HighLevelNode *cbs_node_create(int num_agents);
void cbs_node_free(HighLevelNode *node);
double cbs_compute_soc(const HighLevelNode *node);
bool cbs_detect_conflict(const HighLevelNode *node, Conflict *conflict);

#endif /* PARALLEL_CBS_CBS_H */
