#ifndef PARALLEL_CBS_GRID_H
#define PARALLEL_CBS_GRID_H

#include "common.h"

/* Grid structure representing the map */
typedef struct
{
    /** Width of the grid */
    int width;
    /** Height of the grid */
    int height;
    /** Array of cells representing the grid */
    uint8_t *cells;
} Grid;

/* Check if location is inside map boundaries

@param grid Pointer to the Grid
@param x X coordinate
@param y Y coordinate
*/
static inline bool grid_in_bounds(const Grid *grid, int x, int y)
{
    return x >= 0 && y >= 0 && x < grid->width && y < grid->height;
}

/*
@param grid Pointer to the Grid
@param x X coordinate
@param y Y coordinate
*/
static inline bool grid_is_obstacle(const Grid *grid, int x, int y)
{
    if (!grid_in_bounds(grid, x, y))
    {
        return true;
    }
    return grid->cells[y * grid->width + x] != 0;
}

void grid_init(Grid *grid, int width, int height);
void grid_free(Grid *grid);
bool grid_load_from_file(Grid *grid, const char *path);

#endif /* PARALLEL_CBS_GRID_H */
