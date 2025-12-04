#ifndef PARALLEL_CBS_COMMON_H
#define PARALLEL_CBS_COMMON_H

#include <mpi.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_AGENTS 40
#define MAX_PATH_LENGTH 4096
#define MAX_CONSTRAINTS 4096

/*
Basic Grid Coordinate structure
*/
typedef struct
{
    /**X coordinate*/
    int x;
    /**Y coordinate*/
    int y;
} GridCoord;

/*
Agent Path structure
*/
typedef struct
{
    /**Array of GridCoord steps*/
    GridCoord *steps;
    /**Current length of the path*/
    int length;
    /**Current capacity of the steps array*/
    int capacity;
} AgentPath;

/*
Initialize AgentPath with given capacity
@param path Pointer to the AgentPath to initialize
@param capacity Initial capacity of the path
*/
static inline void path_init(AgentPath *path, int capacity)
{
    path->steps = capacity > 0 ? (GridCoord *)malloc(sizeof(GridCoord) * (size_t)capacity) : NULL;
    path->length = 0;
    path->capacity = capacity;
}

/*
Free memory used by AgentPath
@param path Pointer to the AgentPath to free
*/
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

/* Reserve capacity in AgentPath
@param path Pointer to the AgentPath
@param capacity New capacity to reserve
*/
static inline void path_reserve(AgentPath *path, int capacity)
{
    // no expansion needed
    if (capacity <= path->capacity)
    {
        return;
    }
    GridCoord *new_steps = (GridCoord *)realloc(path->steps, sizeof(GridCoord) * (size_t)capacity);
    if (!new_steps)
    {
        fprintf(stderr, "path_reserve: failed to allocate memory for path steps (size=%d)\n", capacity);
        exit(EXIT_FAILURE);
    }
    // reallocate existing array otherwise
    path->steps = new_steps;
    path->capacity = capacity;
}

/* Push new step to AgentPath
@param path Pointer to the AgentPath
@param coord GridCoord step to add
*/
static inline void path_push_step(AgentPath *path, GridCoord coord)
{
    if (path->length >= path->capacity)
    {
        int new_cap = path->capacity == 0 ? 8 : path->capacity * 2;
        path_reserve(path, new_cap);
    }
    path->steps[path->length++] = coord;
}

/* Copy AgentPath from src to dst
@param dst Pointer to destination AgentPath
@param src Pointer to source AgentPath
*/
static inline void path_copy(AgentPath *dst, const AgentPath *src)
{
    path_reserve(dst, src->length);
    dst->length = src->length;
    for (int i = 0; i < src->length; ++i)
    {
        dst->steps[i] = src->steps[i];
    }
}

/*
@param path Pointer to the AgentPath
@param time_index Time index to get the position for
@return GridCoord position of the agent at the given time index
*/
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
