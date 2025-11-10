#ifndef PARALLEL_CBS_COMMON_H
#define PARALLEL_CBS_COMMON_H

#include <mpi.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_AGENTS 16
#define MAX_PATH_LENGTH 256
#define MAX_CONSTRAINTS 512

typedef struct
{
    int x;
    int y;
} GridCoord;

typedef struct
{
    GridCoord *steps;
    int length;
    int capacity;
} AgentPath;

static inline void path_init(AgentPath *path, int capacity)
{
    path->steps = capacity > 0 ? (GridCoord *)malloc(sizeof(GridCoord) * (size_t)capacity) : NULL;
    path->length = 0;
    path->capacity = capacity;
}

static inline void path_free(AgentPath *path)
{
    if (path->steps != NULL)
    {
        free(path->steps);
    }
    path->steps = NULL;
    path->length = 0;
    path->capacity = 0;
}

static inline void path_reserve(AgentPath *path, int capacity)
{
    if (capacity <= path->capacity)
    {
        return;
    }
    path->steps = (GridCoord *)realloc(path->steps, sizeof(GridCoord) * (size_t)capacity);
    path->capacity = capacity;
}

static inline void path_push_step(AgentPath *path, GridCoord coord)
{
    if (path->length >= path->capacity)
    {
        int new_cap = path->capacity == 0 ? 8 : path->capacity * 2;
        path_reserve(path, new_cap);
    }
    path->steps[path->length++] = coord;
}

static inline void path_copy(AgentPath *dst, const AgentPath *src)
{
    path_reserve(dst, src->length);
    dst->length = src->length;
    for (int i = 0; i < src->length; ++i)
    {
        dst->steps[i] = src->steps[i];
    }
}

static inline GridCoord path_step_at(const AgentPath *path, int time_index)
{
    if (path->length == 0)
    {
        return (GridCoord){0, 0};
    }
    if (time_index < path->length)
    {
        return path->steps[time_index];
    }
    return path->steps[path->length - 1];
}

#endif /* PARALLEL_CBS_COMMON_H */
