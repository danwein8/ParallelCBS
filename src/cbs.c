#include "cbs.h"

#include <float.h>
#include <stdio.h>

/* 
Initialize ProblemInstance with a given number of agents and no map

@param instance Pointer to the ProblemInstance to initialize
@param num_agents Number of agents in the problem instance
*/
void problem_instance_init(ProblemInstance *instance, int num_agents)
{
    instance->num_agents = num_agents;
    instance->starts = (GridCoord *)calloc((size_t)num_agents, sizeof(GridCoord));
    instance->goals = (GridCoord *)calloc((size_t)num_agents, sizeof(GridCoord));
    instance->map.width = 0;
    instance->map.height = 0;
    instance->map.cells = NULL;
    // load_grid_from_file? grid_init?
}

/* 
Free memory used by ProblemInstance 

@param instance Pointer to the ProblemInstance to free
*/
void problem_instance_free(ProblemInstance *instance)
{
    grid_free(&instance->map);
    free(instance->starts);
    free(instance->goals);
    instance->starts = NULL;
    instance->goals = NULL;
    instance->num_agents = 0;
}

/*
Initialize a HighLevelNode 

@param num_agents Number of agents in the problem instance
@return Pointer to the newly created HighLevelNode, or NULL on failure
*/
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

/* 
Free memory used by HighLevelNode

@param node Pointer to the HighLevelNode to free
*/
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

/* 
Compute the sum of costs (SOC) for all agents in the given HighLevelNode

@param node Pointer to the HighLevelNode
@return Sum of costs for all agents
*/
double cbs_compute_soc(const HighLevelNode *node)
{
    double soc = 0.0;
    for (int i = 0; i < node->num_agents; ++i)
    {
        soc += node->paths[i].length;
    }
    return soc;
}

/* 
Get the step of an agent's path at a given time index, waiting at the goal if necessary

@param path Pointer to the AgentPath
@param time_index Time index to get the position for
@return GridCoord position of the agent at the given time index
*/
static GridCoord get_step_with_wait(const AgentPath *path, int time_index)
{
    if (path->length <= 0)
    {
        return (GridCoord){0, 0};
    }
    if (time_index < path->length)
    {
        return path->steps[time_index];
    }
    return path->steps[path->length - 1];
}

/* 
Detect the first conflict in the given HighLevelNode's paths

@param node Pointer to the HighLevelNode
@param conflict Pointer to store the detected Conflict (can be NULL)
@return true if a conflict is detected, false otherwise
*/
bool cbs_detect_conflict(const HighLevelNode *node, Conflict *conflict)
{
    int max_len = 0;
    // iterate thru all agents to find the longest path length
    for (int i = 0; i < node->num_agents; ++i)
    {
        if (node->paths[i].length > max_len)
        {
            max_len = node->paths[i].length;
        }
    }

    // iterate through all time steps up to the longest path length
    for (int t = 0; t < max_len; ++t)
    {
        // iterate through all agents pairwise to check for conflicts at time t
        for (int a = 0; a < node->num_agents; ++a)
        {
            // get current and next positions for agent a
            GridCoord pa_curr = get_step_with_wait(&node->paths[a], t);
            GridCoord pa_next = get_step_with_wait(&node->paths[a], t + 1);

            // get current and next positions for agent b
            for (int b = a + 1; b < node->num_agents; ++b)
            {
                GridCoord pb_curr = get_step_with_wait(&node->paths[b], t);
                GridCoord pb_next = get_step_with_wait(&node->paths[b], t + 1);

                // check for vertex conflict
                if (pa_curr.x == pb_curr.x && pa_curr.y == pb_curr.y)
                {
                    // vertex conflict detected
                    printf("Conflict detected between agent %d and agent %d at time %d at position (%d, %d)\n",
                           a, b, t, pa_curr.x, pa_curr.y);
                    if (conflict)
                    {
                        // fill conflict details
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
                    // edge conflict detected
                    printf("Edge conflict detected between agent %d and agent %d at time %d from (%d, %d) to (%d, %d)\n",
                           a, b, t, pa_curr.x, pa_curr.y, pa_next.x, pa_next.y);
                    if (conflict)
                    {
                        // fill conflict details
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
