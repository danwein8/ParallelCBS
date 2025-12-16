#!/bin/bash
#
# Benchmark Pipeline for CBS Implementations
# Runs serial, centralized, and decentralized CBS on the same problems
# Handles failures gracefully - logs errors and continues
#

set -o pipefail

# Default configuration
DEFAULT_TIMEOUT=300
DEFAULT_PROCS=8
DEFAULT_MAP_DIR="Processed_MAPF_maps"
DEFAULT_AGENT_DIR="Random_agent_scenarios"
LOG_DIR="benchmark_logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="${LOG_DIR}/benchmark_${TIMESTAMP}.log"

# CSV output files
SERIAL_CSV="results_serial.csv"
CENTRAL_CSV="results_central.csv"
DECENTRAL_CSV="results_decentral.csv"

# Colors for terminal output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse command line arguments
TIMEOUT=${DEFAULT_TIMEOUT}
PROCS=${DEFAULT_PROCS}
MAP_DIR=${DEFAULT_MAP_DIR}
AGENT_DIR=${DEFAULT_AGENT_DIR}
SPECIFIC_MAP=""
SPECIFIC_AGENTS=""
DRY_RUN=0
SKIP_SERIAL=1
SKIP_CENTRAL=1
SKIP_DECENTRAL=0

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --timeout SEC       Timeout per run in seconds (default: $DEFAULT_TIMEOUT)"
    echo "  --procs N           Number of MPI processes (default: $DEFAULT_PROCS)"
    echo "  --map-dir DIR       Directory containing maps (default: $DEFAULT_MAP_DIR)"
    echo "  --agent-dir DIR     Directory containing agent scenarios (default: $DEFAULT_AGENT_DIR)"
    echo "  --map FILE          Run on a specific map file only"
    echo "  --agents FILE       Run on a specific agents file only"
    echo "  --skip-serial       Skip serial CBS runs"
    echo "  --skip-central      Skip centralized CBS runs"
    echo "  --skip-decentral    Skip decentralized CBS runs"
    echo "  --dry-run           Show what would be run without executing"
    echo "  --help              Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 --timeout 30 --procs 8"
    echo "  $0 --map Processed_MAPF_maps/arena_binary.map --agents Random_agent_scenarios/10_arena_scenario.txt"
    echo "  $0 --skip-serial --timeout 120"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        --procs)
            PROCS="$2"
            shift 2
            ;;
        --map-dir)
            MAP_DIR="$2"
            shift 2
            ;;
        --agent-dir)
            AGENT_DIR="$2"
            shift 2
            ;;
        --map)
            SPECIFIC_MAP="$2"
            shift 2
            ;;
        --agents)
            SPECIFIC_AGENTS="$2"
            shift 2
            ;;
        --skip-serial)
            SKIP_SERIAL=1
            shift
            ;;
        --skip-central)
            SKIP_CENTRAL=1
            shift
            ;;
        --skip-decentral)
            SKIP_DECENTRAL=1
            shift
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# Create log directory
mkdir -p "$LOG_DIR"

# Logging function
log() {
    local level=$1
    shift
    local message="$*"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] [$level] $message" >> "$LOG_FILE"
    
    case $level in
        INFO)
            echo -e "${BLUE}[INFO]${NC} $message"
            ;;
        SUCCESS)
            echo -e "${GREEN}[SUCCESS]${NC} $message"
            ;;
        WARNING)
            echo -e "${YELLOW}[WARNING]${NC} $message"
            ;;
        ERROR)
            echo -e "${RED}[ERROR]${NC} $message"
            ;;
        *)
            echo "[$level] $message"
            ;;
    esac
}

# Run a single benchmark and handle failures
run_benchmark() {
    local version=$1      # serial, central, or decentral
    local map_file=$2
    local agent_file=$3
    local csv_file=$4
    
    local map_name=$(basename "$map_file")
    local agent_name=$(basename "$agent_file")
    local run_log="${LOG_DIR}/${version}_${map_name}_${agent_name}_${TIMESTAMP}.log"
    
    log INFO "Running $version CBS: map=$map_name agents=$agent_name"
    
    local cmd=""
    local exit_code=0
    
    case $version in
        serial)
            cmd="timeout $((TIMEOUT + 10)) ./serial_cbs --map \"$map_file\" --agents \"$agent_file\" --timeout $TIMEOUT --csv \"$csv_file\""
            ;;
        central)
            cmd="timeout $((TIMEOUT + 30)) mpirun --oversubscribe -n $PROCS ./central_cbs --map \"$map_file\" --agents \"$agent_file\" --timeout $TIMEOUT --csv \"$csv_file\""
            ;;
        decentral)
            cmd="timeout $((TIMEOUT + 30)) mpirun --oversubscribe -n $PROCS ./decentralized_cbs --map \"$map_file\" --agents \"$agent_file\" --timeout $TIMEOUT --csv \"$csv_file\""
            ;;
        *)
            log ERROR "Unknown version: $version"
            return 1
            ;;
    esac
    
    if [[ $DRY_RUN -eq 1 ]]; then
        log INFO "[DRY RUN] Would execute: $cmd"
        return 0
    fi
    
    # Execute the command, capturing output
    log INFO "Command: $cmd"
    echo "=== $version CBS: $map_name + $agent_name ===" >> "$run_log"
    echo "Command: $cmd" >> "$run_log"
    echo "Started: $(date)" >> "$run_log"
    echo "---" >> "$run_log"
    
    # Run with timeout and capture exit code
    eval "$cmd" >> "$run_log" 2>&1
    exit_code=$?
    
    echo "---" >> "$run_log"
    echo "Finished: $(date)" >> "$run_log"
    echo "Exit code: $exit_code" >> "$run_log"
    
    # Interpret exit code
    case $exit_code in
        0)
            log SUCCESS "$version completed successfully"
            return 0
            ;;
        124)
            log WARNING "$version timed out (external timeout killed process)"
            # This is expected for hard problems - the CSV should still be updated
            return 0
            ;;
        137)
            log WARNING "$version was killed (SIGKILL) - possibly OOM"
            return 1
            ;;
        139)
            log ERROR "$version crashed with segmentation fault"
            return 1
            ;;
        143)
            log WARNING "$version was terminated (SIGTERM)"
            # Normal for timeout
            return 0
            ;;
        *)
            log ERROR "$version failed with exit code $exit_code"
            return 1
            ;;
    esac
}

# Extract map base name from agent file (e.g., "10_arena_scenario.txt" -> "arena")
get_map_for_agents() {
    local agent_file=$1
    local agent_name=$(basename "$agent_file" .txt)
    # Remove the leading number and underscore (e.g., "10_arena_scenario" -> "arena_scenario")
    local map_base=$(echo "$agent_name" | sed 's/^[0-9]*_//' | sed 's/_scenario$//')
    echo "$map_base"
}

# Find matching map file for an agent scenario
find_map_for_agents() {
    local agent_file=$1
    local map_base=$(get_map_for_agents "$agent_file")
    
    # Try different possible map file patterns
    local possible_maps=(
        "${MAP_DIR}/${map_base}_binary.map"
        "${MAP_DIR}/${map_base}.map"
    )
    
    for map in "${possible_maps[@]}"; do
        if [[ -f "$map" ]]; then
            echo "$map"
            return 0
        fi
    done
    
    return 1
}

# Main benchmark loop
main() {
    log INFO "=========================================="
    log INFO "CBS Benchmark Pipeline Started"
    log INFO "=========================================="
    log INFO "Configuration:"
    log INFO "  Timeout: ${TIMEOUT}s"
    log INFO "  MPI Processes: $PROCS"
    log INFO "  Log file: $LOG_FILE"
    log INFO "  Skip serial: $SKIP_SERIAL"
    log INFO "  Skip central: $SKIP_CENTRAL"
    log INFO "  Skip decentral: $SKIP_DECENTRAL"
    
    # Check that executables exist
    local missing_exe=0
    if [[ $SKIP_SERIAL -eq 0 ]] && [[ ! -x "./serial_cbs" ]]; then
        log ERROR "serial_cbs executable not found. Run 'make serial_cbs' first."
        missing_exe=1
    fi
    if [[ $SKIP_CENTRAL -eq 0 ]] && [[ ! -x "./central_cbs" ]]; then
        log ERROR "central_cbs executable not found. Run 'make central_cbs' first."
        missing_exe=1
    fi
    if [[ $SKIP_DECENTRAL -eq 0 ]] && [[ ! -x "./decentralized_cbs" ]]; then
        log ERROR "decentralized_cbs executable not found. Run 'make decentralized_cbs' first."
        missing_exe=1
    fi
    
    if [[ $missing_exe -eq 1 ]]; then
        log ERROR "Missing executables. Run 'make all' to build."
        exit 1
    fi
    
    # Determine what to run
    local problems=()
    
    if [[ -n "$SPECIFIC_MAP" ]] && [[ -n "$SPECIFIC_AGENTS" ]]; then
        # Single specific problem
        problems+=("$SPECIFIC_MAP|$SPECIFIC_AGENTS")
    elif [[ -n "$SPECIFIC_AGENTS" ]]; then
        # Specific agent file, find matching map
        local map=$(find_map_for_agents "$SPECIFIC_AGENTS")
        if [[ -n "$map" ]]; then
            problems+=("$map|$SPECIFIC_AGENTS")
        else
            log ERROR "Could not find map for agents file: $SPECIFIC_AGENTS"
            exit 1
        fi
    elif [[ -n "$SPECIFIC_MAP" ]]; then
        # Specific map, find all matching agent files
        local map_base=$(basename "$SPECIFIC_MAP" .map | sed 's/_binary$//')
        for agent_file in "${AGENT_DIR}"/*_${map_base}_scenario.txt; do
            if [[ -f "$agent_file" ]]; then
                problems+=("$SPECIFIC_MAP|$agent_file")
            fi
        done
    else
        # Run all combinations - find all agent files and match to maps
        for agent_file in "${AGENT_DIR}"/*.txt; do
            if [[ -f "$agent_file" ]]; then
                local map=$(find_map_for_agents "$agent_file")
                if [[ -n "$map" ]]; then
                    problems+=("$map|$agent_file")
                else
                    log WARNING "No map found for agents: $(basename "$agent_file")"
                fi
            fi
        done
    fi
    
    if [[ ${#problems[@]} -eq 0 ]]; then
        log ERROR "No problems found to run!"
        exit 1
    fi
    
    log INFO "Found ${#problems[@]} problem(s) to benchmark"
    
    # Counters
    local total_runs=0
    local successful_runs=0
    local failed_runs=0
    
    # Run benchmarks
    for problem in "${problems[@]}"; do
        IFS='|' read -r map_file agent_file <<< "$problem"
        
        log INFO "------------------------------------------"
        log INFO "Problem: $(basename "$map_file") + $(basename "$agent_file")"
        log INFO "------------------------------------------"
        
        # Run serial
        if [[ $SKIP_SERIAL -eq 0 ]]; then
            ((total_runs++))
            if run_benchmark "serial" "$map_file" "$agent_file" "$SERIAL_CSV"; then
                ((successful_runs++))
            else
                ((failed_runs++))
            fi
        fi
        
        # Run centralized
        if [[ $SKIP_CENTRAL -eq 0 ]]; then
            ((total_runs++))
            if run_benchmark "central" "$map_file" "$agent_file" "$CENTRAL_CSV"; then
                ((successful_runs++))
            else
                ((failed_runs++))
            fi
        fi
        
        # Run decentralized
        if [[ $SKIP_DECENTRAL -eq 0 ]]; then
            ((total_runs++))
            if run_benchmark "decentral" "$map_file" "$agent_file" "$DECENTRAL_CSV"; then
                ((successful_runs++))
            else
                ((failed_runs++))
            fi
        fi
        
        log INFO ""
    done
    
    # Summary
    log INFO "=========================================="
    log INFO "Benchmark Pipeline Complete"
    log INFO "=========================================="
    log INFO "Total runs: $total_runs"
    log SUCCESS "Successful: $successful_runs"
    if [[ $failed_runs -gt 0 ]]; then
        log ERROR "Failed: $failed_runs"
    else
        log INFO "Failed: 0"
    fi
    log INFO "Results saved to:"
    [[ $SKIP_SERIAL -eq 0 ]] && log INFO "  Serial:       $SERIAL_CSV"
    [[ $SKIP_CENTRAL -eq 0 ]] && log INFO "  Centralized:  $CENTRAL_CSV"
    [[ $SKIP_DECENTRAL -eq 0 ]] && log INFO "  Decentralized: $DECENTRAL_CSV"
    log INFO "Detailed logs in: $LOG_DIR/"
    
    # Exit with failure if any runs failed
    if [[ $failed_runs -gt 0 ]]; then
        exit 1
    fi
    exit 0
}

# Run main
main
