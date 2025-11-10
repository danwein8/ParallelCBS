#ifndef PARALLEL_CBS_GRID_H
#define PARALLEL_CBS_GRID_H

#include "common.h"

typedef struct
{
    int width;
    int height;
    uint8_t *cells;
} Grid;

static inline bool grid_in_bounds(const Grid *grid, int x, int y)
{
    return x >= 0 && y >= 0 && x < grid->width && y < grid->height;
}

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
