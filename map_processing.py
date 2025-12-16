import os
import random

def process_map_to_binary(input_map, outfile):
    free = [".", "G"]
    blocked = ["@", "O", "T", "S", "W"]
    with open(input_map, 'r') as infile, open(outfile, 'w') as out:
        type = infile.readline().strip()
        height_line = infile.readline().strip()
        width_line = infile.readline().strip()
        blank_line = infile.readline().strip()

        height = int(height_line.split()[1])
        width = int(width_line.split()[1])
        # Binary maps in this project use "width height" on the first line
        out.write(f"{width} {height}\n")

        grid = []
        for _ in range(height):
            row_data = infile.readline().strip()
            if len(row_data) != width:
                raise ValueError(f"Map file error: row length {len(row_data)} does not equal width {width}")
            
            row = []
            for c in row_data:
                if c in free:
                    row.append(0)
                else:
                    row.append(1)
            grid.append(row)
            out.write("".join(str(x) for x in row) + "\n")

def compute_components(grid, width, height):
    """Label connected components of free cells; returns list of lists of (x,y)."""
    comps = []
    comp_id = [[-1 for _ in range(width)] for _ in range(height)]
    dirs = [(1,0),(-1,0),(0,1),(0,-1)]
    for y in range(height):
        for x in range(width):
            if grid[y][x] != 0 or comp_id[y][x] != -1:
                continue
            cid = len(comps)
            stack = [(x, y)]
            cells = []
            while stack:
                cx, cy = stack.pop()
                if comp_id[cy][cx] != -1:
                    continue
                comp_id[cy][cx] = cid
                cells.append((cx, cy))
                for dx, dy in dirs:
                    nx, ny = cx + dx, cy + dy
                    if 0 <= nx < width and 0 <= ny < height and grid[ny][nx] == 0 and comp_id[ny][nx] == -1:
                        stack.append((nx, ny))
            comps.append(cells)
    return comps

def pick_random_start_goal(map_file, num_agents, outfile):
    with open(map_file, 'r') as mapf, open(outfile, 'w') as out:
        out.write(f"{num_agents}\n")
        width_str, height_str = mapf.readline().strip().split()
        width = int(width_str)
        height = int(height_str)
        print(f"Processing map {map_file} of size {width}x{height} for {num_agents} agents.")
        grid = []
        for _ in range(height):
            row = mapf.readline().strip()
            if len(row) != width:
                raise ValueError(f"Map row length {len(row)} != width {width}")
            grid.append([int(c) for c in row])

        # Precompute connected components of free cells to ensure reachability
        components = compute_components(grid, width, height)
        # Remove any components with fewer than 2 cells (can't host a start/goal pair)
        components = [c for c in components if len(c) >= 2]
        if not components:
            raise ValueError("No connected free regions with at least 2 cells.")

        for agent in range(num_agents):
            # Pick a component that still has at least 2 free cells
            components = [c for c in components if len(c) >= 2]
            if not components:
                raise ValueError("Not enough connected free cells to assign start and goal positions for all agents.")
            comp = random.choice(components)
            start_idx = random.randrange(len(comp))
            start = comp.pop(start_idx)
            goal_idx = random.randrange(len(comp))
            goal = comp.pop(goal_idx)
            out.write(f"{start[0]} {start[1]} {goal[0]} {goal[1]}\n")
            
if __name__ == "__main__":
    # benchmark_dir = r"MAPF_benchmark_maps"
    # for file in os.listdir(benchmark_dir):
    #     if file.endswith(".map"):
    #         inpath = os.path.join(benchmark_dir, file)
    #         outpath = os.path.join(r"Processed_MAPF_maps", file[:-4] + "_binary.map")
    #         process_map_to_binary(inpath, outpath)
    
    processed_dir = r"Processed_MAPF_maps"
    out_dir = r"Random_agent_scenarios"
    for file in os.listdir(processed_dir):
        if file.endswith("_binary.map"):
            map_path = os.path.join(processed_dir, file)
            for num_agents in [10, 20, 30]:
                out_path = os.path.join(out_dir, str(num_agents) + "_" + file[:-11] + "_scenario.txt")
                pick_random_start_goal(map_path, num_agents, out_path)
