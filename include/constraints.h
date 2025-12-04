#ifndef PARALLEL_CBS_CONSTRAINTS_H
#define PARALLEL_CBS_CONSTRAINTS_H

#include "common.h"

/*Constraint types*/
typedef enum
{
    /** Vertex constraint */
    CONSTRAINT_VERTEX = 0,
    /** Edge constraint */
    CONSTRAINT_EDGE = 1
} ConstraintType;

/* Single constraint structure, two of these exist per conflict */
typedef struct
{
    /** ID of the agent this constraint applies to */
    int agent_id;
    /** Time step at which the constraint applies */
    int time;
    /** Type of the constraint (vertex or edge) */
    ConstraintType type;
    /** Vertex involved in the constraint */
    GridCoord vertex;
    /** Destination vertex for edge constraints */
    GridCoord edge_to;
} Constraint;

/* Set of constraints */
typedef struct
{
    /** Array of constraints */
    Constraint *items;
    /** Number of constraints */
    int count;
    /** Capacity of the constraints array */
    int capacity;
} ConstraintSet;

void constraint_set_init(ConstraintSet *set, int capacity);
void constraint_set_free(ConstraintSet *set);
void constraint_set_add(ConstraintSet *set, Constraint constraint);

#endif /* PARALLEL_CBS_CONSTRAINTS_H */
