#include "coordinator.h"
#include "instance_io.h"
#include "low_level.h"
#include "worker.h"

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void broadcast_instance(ProblemInstance *instance, int root, MPI_Comm comm)
{
    int rank = 0;
    MPI_Comm_rank(comm, &rank);

    int header[3] = {0, 0, 0};
    if (rank == root)
    {
        header[0] = instance->map.width;
        header[1] = instance->map.height;
        header[2] = instance->num_agents;
    }
    MPI_Bcast(header, 3, MPI_INT, root, comm);

    int width = header[0];
    int height = header[1];
    int agents = header[2];

    if (rank != root)
    {
        problem_instance_init(instance, agents);
        grid_init(&instance->map, width, height);
    }

    size_t cell_count = (size_t)width * (size_t)height;
    if (cell_count > 0)
    {
        MPI_Bcast(instance->map.cells, (int)cell_count, MPI_UNSIGNED_CHAR, root, comm);
    }

    if (agents > 0)
    {
        int *buffer = (int *)malloc(sizeof(int) * (size_t)agents * 2);
        if (rank == root)
        {
            for (int i = 0; i < agents; ++i)
            {
                buffer[i * 2] = instance->starts[i].x;
                buffer[i * 2 + 1] = instance->starts[i].y;
            }
        }
        MPI_Bcast(buffer, agents * 2, MPI_INT, root, comm);
        if (rank != root)
        {
            for (int i = 0; i < agents; ++i)
            {
                instance->starts[i].x = buffer[i * 2];
                instance->starts[i].y = buffer[i * 2 + 1];
            }
        }

        if (rank == root)
        {
            for (int i = 0; i < agents; ++i)
            {
                buffer[i * 2] = instance->goals[i].x;
                buffer[i * 2 + 1] = instance->goals[i].y;
            }
        }
        MPI_Bcast(buffer, agents * 2, MPI_INT, root, comm);
        if (rank != root)
        {
            for (int i = 0; i < agents; ++i)
            {
                instance->goals[i].x = buffer[i * 2];
                instance->goals[i].y = buffer[i * 2 + 1];
            }
        }
        free(buffer);
    }
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int world_rank = 0;
    int world_size = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    const char *map_path = NULL;
    const char *agents_path = NULL;
    int expanders = -1;
    int low_level_pool = 0; /* centralized version defaults to no LL pool */
    double timeout_seconds = 0.0;
    const char *csv_path = "results_central.csv";

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--map") == 0 && i + 1 < argc)
        {
            map_path = argv[++i];
        }
        else if (strcmp(argv[i], "--agents") == 0 && i + 1 < argc)
        {
            agents_path = argv[++i];
        }
        else if (strcmp(argv[i], "--expanders") == 0 && i + 1 < argc)
        {
            expanders = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--ll-pool") == 0 && i + 1 < argc)
        {
            low_level_pool = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc)
        {
            timeout_seconds = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc)
        {
            csv_path = argv[++i];
        }
    }

    int config_ok = 1;
    if (world_rank == 0)
    {
        if (!map_path || !agents_path)
        {
            fprintf(stderr, "Usage: mpirun -n <procs> central_cbs --map map.txt --agents agents.txt [--expanders N] [--ll-pool M] [--timeout SEC] [--csv path]\n");
            config_ok = 0;
        }
        if (world_size < 2)
        {
            fprintf(stderr, "At least two MPI ranks are required.\n");
            config_ok = 0;
        }
    }
    MPI_Bcast(&config_ok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!config_ok)
    {
        MPI_Finalize();
        return 1;
    }

    ProblemInstance instance;
    memset(&instance, 0, sizeof(ProblemInstance));
    int load_success = 1;
    if (world_rank == 0)
    {
        if (!load_problem_instance(map_path, agents_path, &instance))
        {
            fprintf(stderr, "Failed to load problem instance.\n");
            load_success = 0;
        }
    }
    MPI_Bcast(&load_success, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!load_success)
    {
        MPI_Finalize();
        return 1;
    }

    broadcast_instance(&instance, 0, MPI_COMM_WORLD);

    if (world_rank == 0)
    {
        int available = world_size - 1;
        if (expanders < 0)
        {
            expanders = available > 0 ? available : 1;
        }
        if (low_level_pool < 0)
        {
            low_level_pool = 0;
        }
        if (expanders < 1)
        {
            expanders = 1;
        }
        if (expanders > available)
        {
            expanders = available;
        }
    }

    MPI_Bcast(&expanders, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&low_level_pool, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int worker_count = expanders;
    int pool_start = 1 + expanders;
    int pool_end = pool_start + low_level_pool;
    int manager_rank = low_level_pool > 0 ? pool_start : -1;

    LowLevelContext ll_ctx;
    ll_ctx.manager_world_rank = manager_rank;
    ll_ctx.pool_comm = MPI_COMM_NULL;

    MPI_Comm pool_comm = MPI_COMM_NULL;
    int color = (low_level_pool > 0 && world_rank >= pool_start && world_rank < pool_end) ? 1 : MPI_UNDEFINED;
    MPI_Comm_split(MPI_COMM_WORLD, color, world_rank, &pool_comm);
    if (color == 1)
    {
        ll_ctx.pool_comm = pool_comm;
    }

    WorkerSet workers;
    workers.count = worker_count;
    workers.ranks = NULL;
    if (worker_count > 0)
    {
        workers.ranks = (int *)malloc(sizeof(int) * (size_t)worker_count);
        for (int i = 0; i < worker_count; ++i)
        {
            workers.ranks[i] = 1 + i;
        }
    }

    RunStats stats;
    if (world_rank == 0)
    {
        run_coordinator(&instance, &ll_ctx, &workers, timeout_seconds, &stats);
        low_level_request_shutdown(&ll_ctx);

        const char *map_name = map_path ? strrchr(map_path, '/') : NULL;
        map_name = map_name ? map_name + 1 : map_path ? map_path : "unknown";
        int need_header = access(csv_path, F_OK) != 0;
        FILE *fp = fopen(csv_path, "a");
        if (fp)
        {
            if (need_header)
            {
                fprintf(fp, "map,agents,width,height,nodes_expanded,nodes_generated,conflicts,cost,runtime_sec,comm_time_sec,compute_time_sec,timeout_sec,status\n");
            }
            const char *status = stats.solution_found ? "success" : (stats.timed_out ? "timeout" : "failure");
            double cost_out = stats.solution_found ? stats.best_cost : -1.0;
            fprintf(fp,
                    "%s,%d,%d,%d,%lld,%lld,%lld,%.0f,%.6f,%.6f,%.6f,%.2f,%s\n",
                    map_name,
                    instance.num_agents,
                    instance.map.width,
                    instance.map.height,
                    stats.nodes_expanded,
                    stats.nodes_generated,
                    stats.conflicts_detected,
                    cost_out,
                    stats.runtime_sec,
                    stats.comm_time_sec,
                    stats.compute_time_sec,
                    timeout_seconds,
                    status);
            fclose(fp);
        }
        else
        {
            fprintf(stderr, "Warning: could not open CSV file %s for writing.\n", csv_path);
        }
    }
    else if (world_rank >= 1 && world_rank < 1 + worker_count)
    {
        run_worker(&instance, &ll_ctx, 0);
    }
    else if (low_level_pool > 0 && world_rank >= pool_start && world_rank < pool_end)
    {
        low_level_service_loop(&instance, &ll_ctx);
    }

    if (pool_comm != MPI_COMM_NULL)
    {
        MPI_Comm_free(&pool_comm);
    }

    problem_instance_free(&instance);
    free(workers.ranks);

    MPI_Finalize();
    return 0;
}
