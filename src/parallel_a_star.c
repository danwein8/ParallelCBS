#include "parallel_a_star.h"

#include "messages.h"
#include "priority_queue.h"

#include <limits.h>
#include <math.h>
#include <memory.h>

#define MAX_NEIGHBORS 5

typedef struct
{
    int node_index;
    int x;
    int y;
    int g;
    int time;
} LLTaskMessage;

typedef struct
{
    int from_node_index;
    int neighbor_count;
    int data[MAX_NEIGHBORS][4]; /* x, y, g, time */
} LLResultMessage;

_Static_assert(sizeof(LLTaskMessage) == sizeof(int) * 5, "LLTaskMessage padding mismatch");
_Static_assert(sizeof(LLResultMessage) == sizeof(int) * (2 + MAX_NEIGHBORS * 4), "LLResultMessage padding mismatch");

void a_star_buffer_init(AStarNodeBuffer *buffer)
{
    buffer->nodes = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
}

void a_star_buffer_free(AStarNodeBuffer *buffer)
{
    free(buffer->nodes);
    buffer->nodes = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
}

int a_star_buffer_add(AStarNodeBuffer *buffer, AStarNode node)
{
    if (buffer->count >= buffer->capacity)
    {
        int new_cap = buffer->capacity == 0 ? 256 : buffer->capacity * 2;
        buffer->nodes = (AStarNode *)realloc(buffer->nodes, sizeof(AStarNode) * (size_t)new_cap);
        buffer->capacity = new_cap;
    }
    buffer->nodes[buffer->count] = node;
    return buffer->count++;
}

static inline double heuristic(GridCoord a, GridCoord b)
{
    return fabs((double)a.x - (double)b.x) + fabs((double)a.y - (double)b.y);
}

static inline int state_index(const Grid *grid, int time, int x, int y)
{
    int plane = grid->width * grid->height;
    return time * plane + y * grid->width + x;
}

static bool violates_constraint(const ConstraintSet *set,
                                int agent_id,
                                int time_from,
                                int time_to,
                                GridCoord from,
                                GridCoord to)
{
    for (int i = 0; i < set->count; ++i)
    {
        const Constraint *c = &set->items[i];
        if (c->agent_id >= 0 && c->agent_id != agent_id)
        {
            continue;
        }
        if (c->type == CONSTRAINT_VERTEX && c->time == time_to &&
            c->vertex.x == to.x && c->vertex.y == to.y)
        {
            return true;
        }
        if (c->type == CONSTRAINT_EDGE && c->time == time_from &&
            c->vertex.x == from.x && c->vertex.y == from.y &&
            c->edge_to.x == to.x && c->edge_to.y == to.y)
        {
            return true;
        }
    }
    return false;
}

static int generate_neighbors(const Grid *grid,
                              const ConstraintSet *constraints,
                              int agent_id,
                              const AStarNode *node,
                              GridCoord neighbors[MAX_NEIGHBORS],
                              int g_costs[MAX_NEIGHBORS],
                              int times[MAX_NEIGHBORS])
{
    static const GridCoord moves[MAX_NEIGHBORS] = {
        {0, 0},  /* wait */
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}};

    int produced = 0;
    for (int i = 0; i < MAX_NEIGHBORS; ++i)
    {
        GridCoord next = {node->position.x + moves[i].x, node->position.y + moves[i].y};
        int next_time = node->time + 1;

        if (!grid_in_bounds(grid, next.x, next.y))
        {
            continue;
        }
        if (moves[i].x != 0 || moves[i].y != 0)
        {
            if (grid_is_obstacle(grid, next.x, next.y))
            {
                continue;
            }
        }

        if (violates_constraint(constraints,
                                agent_id,
                                node->time,
                                next_time,
                                node->position,
                                next))
        {
            continue;
        }

        neighbors[produced] = next;
        g_costs[produced] = node->g_cost + 1;
        times[produced] = next_time;
        produced++;
    }

    return produced;
}

static void reconstruct_path(const AStarNodeBuffer *buffer, int goal_index, AgentPath *path)
{
    const AStarNode *node = &buffer->nodes[goal_index];
    int length = node->time + 1;
    path_reserve(path, length);

    int idx = goal_index;
    int write_pos = length - 1;
    while (idx >= 0 && write_pos >= 0)
    {
        const AStarNode *current = &buffer->nodes[idx];
        path->steps[write_pos] = current->position;
        idx = current->parent_index;
        write_pos--;
    }
    path->length = length;
}

bool sequential_a_star(const Grid *grid,
                       const ConstraintSet *constraints,
                       GridCoord start,
                       GridCoord goal,
                       int agent_id,
                       AgentPath *out_path)
{
    AStarNodeBuffer buffer;
    a_star_buffer_init(&buffer);

    PriorityQueue open;
    pq_init(&open);

    int max_time = MAX_PATH_LENGTH;
    int plane = grid->width * grid->height;
    int total = max_time * plane;
    int *best_cost = (int *)malloc(sizeof(int) * (size_t)total);
    for (int i = 0; i < total; ++i)
    {
        best_cost[i] = INT_MAX;
    }

    AStarNode root = {.position = start, .g_cost = 0, .f_cost = (int)heuristic(start, goal), .parent_index = -1, .time = 0};
    int root_index = a_star_buffer_add(&buffer, root);
    pq_push(&open, root.f_cost, (void *)(intptr_t)root_index);
    best_cost[state_index(grid, 0, start.x, start.y)] = 0;

    bool found = false;
    int goal_index = -1;

    while (open.count > 0)
    {
        double key = 0.0;
        int node_index = (int)(intptr_t)pq_pop(&open, &key);
        AStarNode *node = &buffer.nodes[node_index];
        if (node->position.x == goal.x && node->position.y == goal.y)
        {
            found = true;
            goal_index = node_index;
            break;
        }

        GridCoord neighbors[MAX_NEIGHBORS];
        int g_costs[MAX_NEIGHBORS];
        int times[MAX_NEIGHBORS];
        int count = generate_neighbors(grid, constraints, agent_id, node, neighbors, g_costs, times);
        for (int i = 0; i < count; ++i)
        {
            if (times[i] >= max_time)
            {
                continue;
            }
            int idx = state_index(grid, times[i], neighbors[i].x, neighbors[i].y);
            if (best_cost[idx] <= g_costs[i])
            {
                continue;
            }
            best_cost[idx] = g_costs[i];
            int h = (int)heuristic(neighbors[i], goal);
            AStarNode child = {.position = neighbors[i],
                               .g_cost = g_costs[i],
                               .f_cost = g_costs[i] + h,
                               .parent_index = node_index,
                               .time = times[i]};
            int child_index = a_star_buffer_add(&buffer, child);
            pq_push(&open, child.f_cost, (void *)(intptr_t)child_index);
        }
    }

    if (found)
    {
        reconstruct_path(&buffer, goal_index, out_path);
    }

    free(best_cost);
    pq_free(&open);
    a_star_buffer_free(&buffer);
    return found;
}

bool parallel_a_star(const Grid *grid,
                     const ConstraintSet *constraints,
                     GridCoord start,
                     GridCoord goal,
                     int agent_id,
                     MPI_Comm comm,
                     AgentPath *out_path)
{
    int rank = 0;
    int size = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    if (size == 1)
    {
        return sequential_a_star(grid, constraints, start, goal, agent_id, out_path);
    }

    /* Copy grid to all ranks */
    Grid local_grid = *grid;
    size_t cell_count = (size_t)grid->width * (size_t)grid->height;
    local_grid.cells = (uint8_t *)malloc(cell_count);
    memcpy(local_grid.cells, grid->cells, cell_count);

    ConstraintSet local_constraints = *constraints;
    local_constraints.items = NULL;
    local_constraints.count = constraints->count;
    local_constraints.capacity = constraints->count;
    if (constraints->count > 0)
    {
        local_constraints.items = (Constraint *)malloc(sizeof(Constraint) * (size_t)constraints->count);
        memcpy(local_constraints.items, constraints->items, sizeof(Constraint) * (size_t)constraints->count);
    }

    bool success = false;
    int success_flag = 0;

    if (rank == 0)
    {
        AStarNodeBuffer buffer;
        a_star_buffer_init(&buffer);

        PriorityQueue open;
        pq_init(&open);

        int max_time = MAX_PATH_LENGTH;
        int plane = local_grid.width * local_grid.height;
        int total = max_time * plane;
        int *best_cost = (int *)malloc(sizeof(int) * (size_t)total);
        for (int i = 0; i < total; ++i)
        {
            best_cost[i] = INT_MAX;
        }

        AStarNode root = {.position = start,
                          .g_cost = 0,
                          .f_cost = (int)heuristic(start, goal),
                          .parent_index = -1,
                          .time = 0};
        int root_index = a_star_buffer_add(&buffer, root);
        pq_push(&open, root.f_cost, (void *)(intptr_t)root_index);
        best_cost[state_index(&local_grid, 0, start.x, start.y)] = 0;

        int next_worker = 1;
        int goal_index = -1;

        while (open.count > 0)
        {
            int max_tasks = size - 1;
            if (max_tasks <= 0)
            {
                break;
            }

            int task_count = 0;
            int max_batch = max_tasks > 0 ? max_tasks : 0;
            int *task_nodes = max_batch > 0 ? (int *)malloc(sizeof(int) * (size_t)max_batch) : NULL;
            while (task_count < max_tasks && open.count > 0)
            {
                double key = 0.0;
                int node_index = (int)(intptr_t)pq_pop(&open, &key);
                task_nodes[task_count++] = node_index;
            }

            if (task_count == 0)
            {
                free(task_nodes);
                break;
            }

            for (int i = 0; i < task_count; ++i)
            {
                int worker_rank = next_worker;
                next_worker++;
                if (next_worker >= size)
                {
                    next_worker = 1;
                }

                AStarNode *node = &buffer.nodes[task_nodes[i]];
                LLTaskMessage msg = {.node_index = task_nodes[i],
                                     .x = node->position.x,
                                     .y = node->position.y,
                                     .g = node->g_cost,
                                     .time = node->time};
                MPI_Send(&msg, sizeof(LLTaskMessage) / sizeof(int), MPI_INT, worker_rank, TAG_LL_TASK, comm);
            }

            for (int i = 0; i < task_count; ++i)
            {
                LLResultMessage result;
                MPI_Status status;
                MPI_Recv(&result,
                         sizeof(result) / sizeof(int),
                         MPI_INT,
                         MPI_ANY_SOURCE,
                         TAG_LL_RESULT,
                         comm,
                         &status);

                for (int n = 0; n < result.neighbor_count; ++n)
                {
                    GridCoord pos = {.x = result.data[n][0], .y = result.data[n][1]};
                    int g_val = result.data[n][2];
                    int time_val = result.data[n][3];
                    if (time_val >= max_time)
                    {
                        continue;
                    }
                    int idx = state_index(&local_grid, time_val, pos.x, pos.y);
                    if (best_cost[idx] <= g_val)
                    {
                        continue;
                    }
                    best_cost[idx] = g_val;
                    int h = (int)heuristic(pos, goal);
                    AStarNode child = {.position = pos,
                                       .g_cost = g_val,
                                       .f_cost = g_val + h,
                                       .parent_index = result.from_node_index,
                                       .time = time_val};
                    int child_index = a_star_buffer_add(&buffer, child);
                    pq_push(&open, child.f_cost, (void *)(intptr_t)child_index);

                    if (pos.x == goal.x && pos.y == goal.y)
                    {
                        goal_index = child_index;
                        success_flag = 1;
                    }
                }
            }

            free(task_nodes);

            if (success_flag)
            {
                break;
            }
        }

        if (success_flag && goal_index >= 0)
        {
            reconstruct_path(&buffer, goal_index, out_path);
            success = true;
        }

        /* Inform workers to stop */
        for (int worker_rank = 1; worker_rank < size; ++worker_rank)
        {
            MPI_Send(NULL, 0, MPI_INT, worker_rank, TAG_LL_TERMINATE, comm);
        }

        free(best_cost);
        pq_free(&open);
        a_star_buffer_free(&buffer);
    }
    else
    {
        int agent = agent_id; /* unused but kept for parity */
        (void)agent;

        while (true)
        {
            MPI_Status status;
            MPI_Probe(0, MPI_ANY_TAG, comm, &status);
            if (status.MPI_TAG == TAG_LL_TERMINATE)
            {
                MPI_Recv(NULL, 0, MPI_INT, 0, TAG_LL_TERMINATE, comm, MPI_STATUS_IGNORE);
                break;
            }
            else if (status.MPI_TAG == TAG_LL_TASK)
            {
                LLTaskMessage task;
                MPI_Recv(&task, sizeof(LLTaskMessage) / sizeof(int), MPI_INT, 0, TAG_LL_TASK, comm, MPI_STATUS_IGNORE);
                AStarNode node = {.position = {.x = task.x, .y = task.y},
                                  .g_cost = task.g,
                                  .time = task.time};
                GridCoord neighbors[MAX_NEIGHBORS];
                int g_costs[MAX_NEIGHBORS];
                int times[MAX_NEIGHBORS];
                int count = generate_neighbors(&local_grid,
                                               &local_constraints,
                                               agent_id,
                                               &node,
                                               neighbors,
                                               g_costs,
                                               times);

                LLResultMessage result = {.from_node_index = task.node_index, .neighbor_count = count};
                for (int i = 0; i < count; ++i)
                {
                    result.data[i][0] = neighbors[i].x;
                    result.data[i][1] = neighbors[i].y;
                    result.data[i][2] = g_costs[i];
                    result.data[i][3] = times[i];
                }
                MPI_Send(&result, sizeof(result) / sizeof(int), MPI_INT, 0, TAG_LL_RESULT, comm);
            }
        }
    }

    MPI_Bcast(&success_flag, 1, MPI_INT, 0, comm);
    if (rank != 0)
    {
        success = success_flag != 0;
    }

    free(local_grid.cells);
    free(local_constraints.items);
    return success;
}
