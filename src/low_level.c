#include "low_level.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    int agent_id;
    int start_x;
    int start_y;
    int goal_x;
    int goal_y;
    int constraint_count;
} LLRequestHeader;

typedef struct
{
    int status;
    int path_length;
} LLResponseHeader;

_Static_assert(sizeof(LLRequestHeader) == sizeof(int) * 6, "LLRequestHeader padding mismatch");
_Static_assert(sizeof(LLResponseHeader) == sizeof(int) * 2, "LLResponseHeader padding mismatch");

static void build_constraint_buffer(const ConstraintSet *constraints, int agent_id, int **buffer_out, int *count_out)
{
    int filtered = 0;
    for (int i = 0; i < constraints->count; ++i)
    {
        if (constraints->items[i].agent_id == agent_id || constraints->items[i].agent_id < 0)
        {
            filtered++;
        }
    }

    *count_out = filtered;
    if (filtered == 0)
    {
        *buffer_out = NULL;
        return;
    }

    int *buf = (int *)malloc(sizeof(int) * (size_t)(filtered * 7));
    int cursor = 0;
    for (int i = 0; i < constraints->count; ++i)
    {
        const Constraint *c = &constraints->items[i];
        if (c->agent_id == agent_id || c->agent_id < 0)
        {
            buf[cursor++] = c->agent_id;
            buf[cursor++] = c->time;
            buf[cursor++] = (int)c->type;
            buf[cursor++] = c->vertex.x;
            buf[cursor++] = c->vertex.y;
            buf[cursor++] = c->edge_to.x;
            buf[cursor++] = c->edge_to.y;
        }
    }
    *buffer_out = buf;
}

static void fill_constraint_set(ConstraintSet *set, const int *buffer, int count)
{
    for (int i = 0; i < count; ++i)
    {
        Constraint c = {
            .agent_id = buffer[i * 7 + 0],
            .time = buffer[i * 7 + 1],
            .type = (ConstraintType)buffer[i * 7 + 2],
            .vertex = {.x = buffer[i * 7 + 3], .y = buffer[i * 7 + 4]},
            .edge_to = {.x = buffer[i * 7 + 5], .y = buffer[i * 7 + 6]}};
        constraint_set_add(set, c);
    }
}

bool low_level_request_path(const ProblemInstance *instance,
                            const ConstraintSet *constraints,
                            int agent_id,
                            const LowLevelContext *ctx,
                            AgentPath *out_path)
{
    int world_rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    if (ctx->manager_world_rank < 0)
    {
        return sequential_a_star(&instance->map, constraints, instance->starts[agent_id], instance->goals[agent_id], agent_id, out_path);
    }

    int constraint_count = 0;
    int *constraint_buffer = NULL;
    build_constraint_buffer(constraints, agent_id, &constraint_buffer, &constraint_count);

    LLRequestHeader header = {
        .agent_id = agent_id,
        .start_x = instance->starts[agent_id].x,
        .start_y = instance->starts[agent_id].y,
        .goal_x = instance->goals[agent_id].x,
        .goal_y = instance->goals[agent_id].y,
        .constraint_count = constraint_count};

    printf("[LL req %d] agent=%d constraints=%d -> manager %d\n",
           world_rank,
           agent_id,
           constraint_count,
           ctx->manager_world_rank);
    fflush(stdout);

    MPI_Send(&header, sizeof(header) / sizeof(int), MPI_INT, ctx->manager_world_rank, TAG_LL_REQUEST, MPI_COMM_WORLD);
    if (constraint_count > 0)
    {
        MPI_Send(constraint_buffer,
                 constraint_count * 7,
                 MPI_INT,
                 ctx->manager_world_rank,
                 TAG_LL_REQUEST,
                 MPI_COMM_WORLD);
    }

    free(constraint_buffer);

    LLResponseHeader response;
    MPI_Recv(&response, sizeof(response) / sizeof(int), MPI_INT, ctx->manager_world_rank, TAG_LL_RESPONSE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (response.status == 0)
    {
        printf("[LL resp %d] agent=%d status=fail\n", world_rank, agent_id);
        fflush(stdout);
        return false;
    }

    int path_ints = response.path_length * 2;
    int *path_buffer = NULL;
    if (path_ints > 0)
    {
        path_buffer = (int *)malloc(sizeof(int) * (size_t)path_ints);
        MPI_Recv(path_buffer, path_ints, MPI_INT, ctx->manager_world_rank, TAG_LL_RESPONSE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    printf("[LL resp %d] agent=%d status=ok len=%d\n", world_rank, agent_id, response.path_length);
    fflush(stdout);

    path_reserve(out_path, response.path_length);
    out_path->length = response.path_length;
    for (int i = 0; i < response.path_length; ++i)
    {
        out_path->steps[i].x = path_buffer[i * 2];
        out_path->steps[i].y = path_buffer[i * 2 + 1];
    }

    free(path_buffer);
    return true;
}

void low_level_request_shutdown(const LowLevelContext *ctx)
{
    if (ctx->manager_world_rank < 0)
    {
        return;
    }
    LLRequestHeader header = {.agent_id = -1,
                              .start_x = 0,
                              .start_y = 0,
                              .goal_x = 0,
                              .goal_y = 0,
                              .constraint_count = 0};
    MPI_Send(&header, sizeof(header) / sizeof(int), MPI_INT, ctx->manager_world_rank, TAG_LL_REQUEST, MPI_COMM_WORLD);
}

void low_level_service_loop(const ProblemInstance *instance, const LowLevelContext *ctx)
{
    if (ctx->pool_comm == MPI_COMM_NULL)
    {
        return;
    }

    int pool_rank = 0;
    MPI_Comm_rank(ctx->pool_comm, &pool_rank);
    int world_rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    int running = 1;
    while (running)
    {
        LLRequestHeader header;
        int request_source = MPI_PROC_NULL;

        if (pool_rank == 0)
        {
            MPI_Status status;
            MPI_Recv(&header,
                     sizeof(header) / sizeof(int),
                     MPI_INT,
                     MPI_ANY_SOURCE,
                     TAG_LL_REQUEST,
                     MPI_COMM_WORLD,
                     &status);
            request_source = status.MPI_SOURCE;
            printf("[LL mgr world %d pool %d] recv request from %d agent=%d constraints=%d\n",
                   world_rank,
                   pool_rank,
                   request_source,
                   header.agent_id,
                   header.constraint_count);
            fflush(stdout);
        }

        MPI_Bcast(&request_source, 1, MPI_INT, 0, ctx->pool_comm);
        MPI_Bcast(&header, sizeof(header) / sizeof(int), MPI_INT, 0, ctx->pool_comm);

        if (header.agent_id < 0)
        {
            running = 0;
            continue;
        }

        int constraint_entries = header.constraint_count * 7;
        int *constraint_buffer = NULL;
        if (pool_rank == 0 && constraint_entries > 0)
        {
            constraint_buffer = (int *)malloc(sizeof(int) * (size_t)constraint_entries);
            MPI_Recv(constraint_buffer,
                     constraint_entries,
                     MPI_INT,
                     request_source,
                     TAG_LL_REQUEST,
                     MPI_COMM_WORLD,
                     MPI_STATUS_IGNORE);
        }

        if (constraint_entries > 0)
        {
            if (pool_rank != 0)
            {
                constraint_buffer = (int *)malloc(sizeof(int) * (size_t)constraint_entries);
            }
            MPI_Bcast(constraint_buffer, constraint_entries, MPI_INT, 0, ctx->pool_comm);
        }

        ConstraintSet agent_constraints;
        constraint_set_init(&agent_constraints, header.constraint_count);
        if (constraint_entries > 0 && constraint_buffer != NULL)
        {
            fill_constraint_set(&agent_constraints, constraint_buffer, header.constraint_count);
        }

        AgentPath path;
        path_init(&path, 0);
        bool success = parallel_a_star(&instance->map,
                                       &agent_constraints,
                                       (GridCoord){.x = header.start_x, .y = header.start_y},
                                       (GridCoord){.x = header.goal_x, .y = header.goal_y},
                                       header.agent_id,
                                       ctx->pool_comm,
                                       &path);

        if (pool_rank == 0)
        {
            LLResponseHeader response = {.status = success ? 1 : 0, .path_length = success ? path.length : 0};
            MPI_Send(&response, sizeof(response) / sizeof(int), MPI_INT, request_source, TAG_LL_RESPONSE, MPI_COMM_WORLD);
            if (success && path.length > 0)
            {
                int ints = path.length * 2;
                int *buffer = (int *)malloc(sizeof(int) * (size_t)ints);
                for (int i = 0; i < path.length; ++i)
                {
                    buffer[i * 2] = path.steps[i].x;
                    buffer[i * 2 + 1] = path.steps[i].y;
                }
                MPI_Send(buffer, ints, MPI_INT, request_source, TAG_LL_RESPONSE, MPI_COMM_WORLD);
                free(buffer);
            }
            printf("[LL mgr world %d] send response to %d agent=%d status=%d len=%d\n",
                   world_rank,
                   request_source,
                   header.agent_id,
                   success ? 1 : 0,
                   success ? path.length : 0);
            fflush(stdout);
        }

        path_free(&path);
        constraint_set_free(&agent_constraints);
        free(constraint_buffer);
    }

    /* Ensure all ranks exit together */
    int dummy = 0;
    MPI_Bcast(&dummy, 1, MPI_INT, 0, ctx->pool_comm);
}
