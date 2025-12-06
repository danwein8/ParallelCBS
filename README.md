# ParallelCBS

Parallel implementations of Conflict-Based Search (CBS) for Multi-Agent Path Finding (MAPF) using MPI.

## Overview

This project implements three versions of CBS:

- **Serial CBS** - Single-threaded baseline implementation
- **Centralized CBS** - Coordinator-worker model where rank 0 manages the search and workers expand nodes
- **Decentralized CBS** - Peer-to-peer model where all processes share the workload equally

## Building

Requires MPI (OpenMPI or MPICH) and a C11-compatible compiler.

```bash
# Build all versions
make all

# Build individual versions
make serial_cbs
make central_cbs
make decentralized_cbs

# Clean build artifacts
make clean
```

## Running

### Serial CBS

```bash
./serial_cbs --map <map_file> --agents <agents_file> [OPTIONS]
```

### Centralized CBS

```bash
mpirun -n <num_procs> ./central_cbs --map <map_file> --agents <agents_file> [OPTIONS]
```

### Decentralized CBS

```bash
mpirun -n <num_procs> ./decentralized_cbs --map <map_file> --agents <agents_file> [OPTIONS]
```

### Common Options

| Flag | Description | Default |
|------|-------------|---------|
| `--map FILE` | Path to the map file (required) | - |
| `--agents FILE` | Path to the agent scenario file (required) | - |
| `--timeout SEC` | Time limit in seconds | 0 (no limit) |
| `--csv FILE` | Output CSV file for results | `results_<version>.csv` |

### Example

```bash
# Run serial CBS with 60 second timeout
./serial_cbs --map Processed_MAPF_maps/arena_binary.map \
             --agents Random_agent_scenarios/10_arena_scenario.txt \
             --timeout 60

# Run centralized CBS with 8 processes
mpirun -n 8 ./central_cbs --map Processed_MAPF_maps/arena_binary.map \
                          --agents Random_agent_scenarios/20_arena_scenario.txt \
                          --timeout 120

# Run decentralized CBS with 8 processes
mpirun -n 8 ./decentralized_cbs --map Processed_MAPF_maps/arena_binary.map \
                                --agents Random_agent_scenarios/20_arena_scenario.txt \
                                --timeout 120
```

## Benchmark Pipeline

A comprehensive benchmarking script is included to run all three versions on the same problems:

```bash
./run_benchmark.sh [OPTIONS]
```

### Pipeline Options

| Flag | Description | Default |
|------|-------------|---------|
| `--timeout SEC` | Timeout per run in seconds | 60 |
| `--procs N` | Number of MPI processes | 8 |
| `--map FILE` | Run on a specific map file only | all maps |
| `--agents FILE` | Run on a specific agents file only | all scenarios |
| `--map-dir DIR` | Directory containing maps | `Processed_MAPF_maps` |
| `--agent-dir DIR` | Directory containing scenarios | `Random_agent_scenarios` |
| `--skip-serial` | Skip serial CBS runs | - |
| `--skip-central` | Skip centralized CBS runs | - |
| `--skip-decentral` | Skip decentralized CBS runs | - |
| `--dry-run` | Show commands without executing | - |

### Pipeline Examples

```bash
# Run all versions on all problems with defaults
./run_benchmark.sh

# Run specific problem with 30 second timeout
./run_benchmark.sh --map Processed_MAPF_maps/arena_binary.map \
                   --agents Random_agent_scenarios/10_arena_scenario.txt \
                   --timeout 30

# Run only parallel versions (skip serial)
./run_benchmark.sh --skip-serial --timeout 120 --procs 8

# Preview what would run
./run_benchmark.sh --dry-run
```

The pipeline:
- Handles failures gracefully (logs errors and continues)
- Creates detailed logs in `benchmark_logs/`
- Outputs results to CSV files for analysis

## Input Format

### Map Files

Binary grid maps where:
- `.` or `G` = traversable
- `@` or `O` or `T` = obstacle

Maps are processed from the MAPF benchmark format using `map_processing.py`.

### Agent Scenario Files

Text files with start and goal positions:
```
<num_agents>
<start_x> <start_y> <goal_x> <goal_y>
...
```

## Output

Results are appended to CSV files with columns:
- `map` - Map filename
- `agents` - Number of agents
- `width`, `height` - Map dimensions
- `nodes_expanded` - CBS nodes expanded
- `nodes_generated` - CBS nodes generated
- `conflicts` - Conflicts detected
- `cost` - Solution cost (sum of costs), -1 if not found
- `runtime_sec` - Runtime in seconds
- `timeout_sec` - Timeout setting
- `status` - `success`, `timeout`, or `failure`

## Project Structure

```
├── include/           # Header files
├── src/               # Source files
├── Processed_MAPF_maps/       # Binary map files
├── Random_agent_scenarios/    # Agent scenario files
├── benchmark_logs/            # Benchmark run logs
├── run_benchmark.sh           # Benchmarking pipeline
├── map_processing.py          # Map conversion utility
└── Makefile
```

## License

MIT License
