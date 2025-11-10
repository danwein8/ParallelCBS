#include "instance_io.h"

#include <stdio.h>

bool load_problem_instance(const char *map_path,
                           const char *agents_path,
                           ProblemInstance *instance)
{
    // Load map
    Grid local_map = {.width = 0, .height = 0, .cells = NULL};
    if (!grid_load_from_file(&local_map, map_path))
    {
        return false;
    }

    // Load agents
    FILE *fp = fopen(agents_path, "r");
    if (!fp)
    {
        grid_free(&local_map);
        return false;
    }

    // Read number of agents
    int num_agents = 0;
    if (fscanf(fp, "%d", &num_agents) != 1 || num_agents <= 0 || num_agents > MAX_AGENTS)
    {
        fclose(fp);
        grid_free(&local_map);
        return false;
    }

    // Initialize problem instance
    problem_instance_init(instance, num_agents);
    instance->map = local_map;

    // Read start and goal positions
    for (int i = 0; i < num_agents; ++i)
    {
        int sx, sy, gx, gy;
        if (fscanf(fp, "%d %d %d %d", &sx, &sy, &gx, &gy) != 4)
        {
            fclose(fp);
            problem_instance_free(instance);
            return false;
        }
        instance->starts[i] = (GridCoord){.x = sx, .y = sy};
        instance->goals[i] = (GridCoord){.x = gx, .y = gy};
    }

    // Clean up and return
    fclose(fp);
    return true;
}
