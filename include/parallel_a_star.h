#ifndef PARALLEL_CBS_PARALLEL_A_STAR_H
#define PARALLEL_CBS_PARALLEL_A_STAR_H

#include "common.h"
#include "constraints.h"
#include "grid.h"

typedef struct
{
    GridCoord position;
    int g_cost;
    int f_cost;
    int parent_index;
    int time;
} AStarNode;

typedef struct
{
    AStarNode *nodes;
    int count;
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
