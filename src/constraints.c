#include "constraints.h"

#include <string.h>

void constraint_set_init(ConstraintSet *set, int capacity)
{
    set->items = capacity > 0 ? (Constraint *)malloc(sizeof(Constraint) * (size_t)capacity) : NULL;
    set->count = 0;
    set->capacity = capacity;
}

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

void constraint_set_add(ConstraintSet *set, Constraint constraint)
{
    if (set->count >= set->capacity)
    {
        int new_cap = set->capacity == 0 ? 8 : set->capacity * 2;
        set->items = (Constraint *)realloc(set->items, sizeof(Constraint) * (size_t)new_cap);
        set->capacity = new_cap;
    }
    set->items[set->count++] = constraint;
}
