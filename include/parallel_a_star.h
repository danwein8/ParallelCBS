#ifndef PARALLEL_CBS_PARALLEL_A_STAR_H
#define PARALLEL_CBS_PARALLEL_A_STAR_H

#include "common.h"
#include "constraints.h"
#include "grid.h"

/*
AStarNode represents a node in the A* search algorithm
*/
typedef struct
{
    /** Position of the node in the grid */
    GridCoord position;
    /** Cost from start to this node */
    int g_cost;
    /** Estimated (heuristic) cost from this node to the goal */
    int f_cost;
    /** Index of the parent node in the buffer */
    int parent_index;
    /** Time step of the node */
    int time;
} AStarNode;

/* 
Buffer for storing AStarNodes dynamically
stores the paths explored during A* search
*/
typedef struct
{
    /** Dynamic array of AStarNodes */
    AStarNode *nodes;
    /** Current number of nodes in the buffer */
    int count;
    /** Maximum capacity of the buffer */
    int capacity;
} AStarNodeBuffer;

void a_star_buffer_init(AStarNodeBuffer *buffer);
void a_star_buffer_free(AStarNodeBuffer *buffer);
int a_star_buffer_add(AStarNodeBuffer *buffer, AStarNode node);

bool parallel_a_star(const Grid *grid,
                     const ConstraintSet *constraints,
                     GridCoord start,
                     GridCoord goal,
                     int agent_id,
                     MPI_Comm comm,
                     AgentPath *out_path);
bool sequential_a_star(const Grid *grid,
                       const ConstraintSet *constraints,
                       GridCoord start,
                       GridCoord goal,
                       int agent_id,
                       AgentPath *out_path);

#endif /* PARALLEL_CBS_PARALLEL_A_STAR_H */
