#include "cbs.h"

#include <float.h>
#include <stdio.h>

void problem_instance_init(ProblemInstance *instance, int num_agents)
{
    instance->num_agents = num_agents;
    instance->starts = (GridCoord *)calloc((size_t)num_agents, sizeof(GridCoord));
    instance->goals = (GridCoord *)calloc((size_t)num_agents, sizeof(GridCoord));
    instance->map.width = 0;
    instance->map.height = 0;
    instance->map.cells = NULL;
}

void problem_instance_free(ProblemInstance *instance)
{
    grid_free(&instance->map);
    free(instance->starts);
    free(instance->goals);
    instance->starts = NULL;
    instance->goals = NULL;
    instance->num_agents = 0;
}

HighLevelNode *cbs_node_create(int num_agents)
{
    HighLevelNode *node = (HighLevelNode *)calloc(1, sizeof(HighLevelNode));
    if (!node)
    {
        return NULL;
    }
    node->id = -1;
    node->parent_id = -1;
    node->depth = 0;
    node->cost = 0.0;
    node->num_agents = num_agents;
    node->paths = (AgentPath *)calloc((size_t)num_agents, sizeof(AgentPath));
    constraint_set_init(&node->constraints, 0);
    return node;
}

void cbs_node_free(HighLevelNode *node)
{
    if (!node)
    {
        return;
    }
    for (int i = 0; i < node->num_agents; ++i)
    {
        path_free(&node->paths[i]);
    }
    free(node->paths);
    constraint_set_free(&node->constraints);
    free(node);
}

double cbs_compute_soc(const HighLevelNode *node)
{
    double soc = 0.0;
    for (int i = 0; i < node->num_agents; ++i)
    {
        soc += node->paths[i].length;
    }
    return soc;
}

static GridCoord get_step_with_wait(const AgentPath *path, int time_index)
{
    if (time_index < path->length)
    {
        return path->steps[time_index];
    }
    return path->steps[path->length - 1];
}

bool cbs_detect_conflict(const HighLevelNode *node, Conflict *conflict)
{
    int max_len = 0;
    for (int i = 0; i < node->num_agents; ++i)
    {
        if (node->paths[i].length > max_len)
        {
            max_len = node->paths[i].length;
        }
    }

    for (int t = 0; t < max_len; ++t)
    {
        for (int a = 0; a < node->num_agents; ++a)
        {
            GridCoord pa_curr = get_step_with_wait(&node->paths[a], t);
            GridCoord pa_next = get_step_with_wait(&node->paths[a], t + 1);

            for (int b = a + 1; b < node->num_agents; ++b)
            {
                GridCoord pb_curr = get_step_with_wait(&node->paths[b], t);
                GridCoord pb_next = get_step_with_wait(&node->paths[b], t + 1);

                if (pa_curr.x == pb_curr.x && pa_curr.y == pb_curr.y)
                {
                    if (conflict)
                    {
                        conflict->agent_a = a;
                        conflict->agent_b = b;
                        conflict->time = t;
                        conflict->position = pa_curr;
                        conflict->is_vertex_conflict = true;
                    }
                    return true;
                }

                if (pa_curr.x == pb_next.x && pa_curr.y == pb_next.y &&
                    pb_curr.x == pa_next.x && pb_curr.y == pa_next.y)
                {
                    if (conflict)
                    {
                        conflict->agent_a = a;
                        conflict->agent_b = b;
                        conflict->time = t;
                        conflict->position = pa_curr;
                        conflict->edge_to = pa_next;
                        conflict->is_vertex_conflict = false;
                    }
                    return true;
                }
            }
        }
    }
    return false;
}
