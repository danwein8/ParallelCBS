#include "grid.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void grid_init(Grid *grid, int width, int height)
{
    grid->width = width;
    grid->height = height;
    size_t total = (size_t)width * (size_t)height;
    grid->cells = (uint8_t *)malloc(total);
    if (grid->cells)
    {
        memset(grid->cells, 0, total);
    }
}

void grid_free(Grid *grid)
{
    if (grid->cells != NULL)
    {
        free(grid->cells);
        grid->cells = NULL;
    }
    grid->width = 0;
    grid->height = 0;
}

bool grid_load_from_file(Grid *grid, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        return false;
    }

    int width = 0;
    int height = 0;
    // Read width and height
    if (fscanf(fp, "%d %d\n", &width, &height) != 2)
    {
        fclose(fp);
        return false;
    }

    // Initialize grid
    grid_init(grid, width, height);
    if (grid->cells == NULL)
    {
        fclose(fp);
        return false;
    }

    // Read cell data one character at a time, skipping whitespace
    size_t cell_count = (size_t)width * (size_t)height;
    size_t cell_index = 0;
    while (cell_index < cell_count)
    {
        int ch = fgetc(fp);
        if (ch == EOF)
        {
            grid_free(grid);
            fclose(fp);
            return false;
        }
        if (isspace(ch))
        {
            continue;
        }
        if (ch != '0' && ch != '1')
        {
            grid_free(grid);
            fclose(fp);
            return false;
        }
        grid->cells[cell_index++] = (uint8_t)(ch - '0');
    }

    // Clean up and return
    fclose(fp);
    return true;
}
