#ifndef PARALLEL_CBS_CONSTRAINTS_H
#define PARALLEL_CBS_CONSTRAINTS_H

#include "common.h"

typedef enum
{
    CONSTRAINT_VERTEX = 0,
    CONSTRAINT_EDGE = 1
} ConstraintType;

typedef struct
{
    int agent_id;
    int time;
    ConstraintType type;
    GridCoord vertex;
    GridCoord edge_to;
} Constraint;

typedef struct
{
    Constraint *items;
    int count;
    int capacity;
} ConstraintSet;

void constraint_set_init(ConstraintSet *set, int capacity);
void constraint_set_free(ConstraintSet *set);
void constraint_set_add(ConstraintSet *set, Constraint constraint);

#endif /* PARALLEL_CBS_CONSTRAINTS_H */
