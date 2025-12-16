#include "constraints.h"

#include <stdio.h>
#include <string.h>

/*
Initialize constraint set with given capacity 

@param set Pointer to the ConstraintSet to initialize
@param capacity Initial capacity of the set
*/
void constraint_set_init(ConstraintSet *set, int capacity)
{
    set->items = capacity > 0 ? (Constraint *)malloc(sizeof(Constraint) * (size_t)capacity) : NULL;
    set->count = 0;
    set->capacity = capacity;
}

/*
Free memory used by constraint set

@param set Pointer to the ConstraintSet to free
*/
void constraint_set_free(ConstraintSet *set)
{
    if (set->items != NULL)
    {
        free(set->items);
    }
    set->items = NULL;
    set->count = 0;
    set->capacity = 0;
}

/* Add a constraint to the set
@param set Pointer to the ConstraintSet
@param constraint Constraint to add
*/
void constraint_set_add(ConstraintSet *set, Constraint constraint)
{
    if (set->count >= set->capacity)
    {
        int new_cap = set->capacity == 0 ? 8 : set->capacity * 2;
        Constraint *new_items = (Constraint *)realloc(set->items, sizeof(Constraint) * (size_t)new_cap);
        if (!new_items)
        {
            fprintf(stderr, "constraint_set_add: failed to allocate memory for ConstraintSet (size=%d)\n", new_cap);
            exit(EXIT_FAILURE);
        }
        set->items = new_items;
        set->capacity = new_cap;
    }
    set->items[set->count++] = constraint;
}
