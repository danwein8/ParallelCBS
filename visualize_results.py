#!/usr/bin/env python3
"""
CBS Results Visualization Pipeline

Compares performance metrics across Serial, Centralized, and Decentralized CBS implementations.
Calculates speedup and efficiency for parallel versions run with 16 MPI processors.

Author: Generated for ParallelCBS project
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
from pathlib import Path

# Configuration
NUM_PROCESSORS = 16
RESULTS_DIR = Path("final_results")
OUTPUT_DIR = Path("plots")

# Color scheme for consistency
COLORS = {
    'serial': '#2E86AB',      # Blue
    'centralized': '#A23B72',  # Magenta
    'decentralized': '#F18F01' # Orange
}

def load_results():
    """Load all CSV result files."""
    serial = pd.read_csv(RESULTS_DIR / "results_serial_nano.csv")
    central = pd.read_csv(RESULTS_DIR / "results_central_nano.csv")
    decentral = pd.read_csv(RESULTS_DIR / "results_decentral_nano.csv")
    
    # Clean up any empty rows
    serial = serial.dropna(subset=['map'])
    central = central.dropna(subset=['map'])
    decentral = decentral.dropna(subset=['map'])
    
    # Add version labels
    serial['version'] = 'serial'
    central['version'] = 'centralized'
    decentral['version'] = 'decentralized'
    
    # Add comm/compute columns for serial (all compute, no comm)
    if 'comm_time_sec' not in serial.columns:
        serial['comm_time_sec'] = 0.0
        serial['compute_time_sec'] = serial['runtime_sec']
    
    # Handle old CSVs without comm/compute columns for parallel versions
    if 'comm_time_sec' not in central.columns:
        central['comm_time_sec'] = np.nan
        central['compute_time_sec'] = np.nan
    if 'comm_time_sec' not in decentral.columns:
        decentral['comm_time_sec'] = np.nan
        decentral['compute_time_sec'] = np.nan
    
    return serial, central, decentral

def create_merged_dataset(serial, central, decentral):
    """Merge datasets on map and agents for comparison."""
    # Create keys for merging
    serial_success = serial[serial['status'] == 'success'].copy()
    central_success = central[central['status'] == 'success'].copy()
    decentral_success = decentral[decentral['status'] == 'success'].copy()
    
    # Filter out anomalously fast runs (likely cached/trivial solutions)
    # These show 0.000xxx seconds and distort speedup calculations
    MIN_RUNTIME = 0.01  # 10ms minimum for meaningful comparison
    serial_success = serial_success[serial_success['runtime_sec'] >= MIN_RUNTIME]
    central_success = central_success[central_success['runtime_sec'] >= MIN_RUNTIME]
    decentral_success = decentral_success[decentral_success['runtime_sec'] >= MIN_RUNTIME]
    
    # Merge serial with centralized
    merged_central = serial_success.merge(
        central_success,
        on=['map', 'agents'],
        suffixes=('_serial', '_central')
    )
    
    # Merge serial with decentralized
    merged_decentral = serial_success.merge(
        decentral_success,
        on=['map', 'agents'],
        suffixes=('_serial', '_decentral')
    )
    
    return merged_central, merged_decentral

def calculate_speedup_efficiency(merged_central, merged_decentral):
    """Calculate speedup and efficiency metrics."""
    # Centralized speedup and efficiency
    merged_central['speedup'] = merged_central['runtime_sec_serial'] / merged_central['runtime_sec_central']
    merged_central['efficiency'] = merged_central['speedup'] / NUM_PROCESSORS * 100
    
    # Decentralized speedup and efficiency
    merged_decentral['speedup'] = merged_decentral['runtime_sec_serial'] / merged_decentral['runtime_sec_decentral']
    merged_decentral['efficiency'] = merged_decentral['speedup'] / NUM_PROCESSORS * 100
    
    return merged_central, merged_decentral

def plot_runtime_comparison(serial, central, decentral):
    """Plot runtime comparison across all versions."""
    fig, ax = plt.subplots(figsize=(14, 8))
    
    # Combine all data
    all_data = pd.concat([serial, central, decentral])
    all_data = all_data[all_data['status'] == 'success']
    
    # Group by map and agents
    maps = all_data['map'].unique()
    agents_list = sorted(all_data['agents'].unique())
    
    # Create grouped bar chart
    x = np.arange(len(agents_list))
    width = 0.25
    
    for i, version in enumerate(['serial', 'centralized', 'decentralized']):
        version_data = all_data[all_data['version'] == version]
        runtimes = []
        for agents in agents_list:
            agent_data = version_data[version_data['agents'] == agents]
            if len(agent_data) > 0:
                runtimes.append(agent_data['runtime_sec'].mean())
            else:
                runtimes.append(0)
        ax.bar(x + i * width, runtimes, width, label=version.capitalize(), 
               color=COLORS[version], alpha=0.8)
    
    ax.set_xlabel('Number of Agents', fontsize=12)
    ax.set_ylabel('Runtime (seconds)', fontsize=12)
    ax.set_title('Average Runtime Comparison by Number of Agents\n(Successful Runs Only)', fontsize=14)
    ax.set_xticks(x + width)
    ax.set_xticklabels(agents_list)
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_yscale('log')
    
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'runtime_comparison.png', dpi=150, bbox_inches='tight')
    plt.savefig(OUTPUT_DIR / 'runtime_comparison.pdf', bbox_inches='tight')
    plt.close()
    print("✓ Saved runtime_comparison.png/pdf")

def plot_speedup_analysis(merged_central, merged_decentral):
    """Plot speedup analysis for parallel versions."""
    fig, axes = plt.subplots(1, 2, figsize=(14, 6))
    
    # Centralized speedup
    ax1 = axes[0]
    for map_name in merged_central['map'].unique():
        map_data = merged_central[merged_central['map'] == map_name]
        ax1.scatter(map_data['agents'], map_data['speedup'], 
                   label=map_name.replace('_binary.map', ''), s=80, alpha=0.7)
    
    ax1.axhline(y=1, color='red', linestyle='--', label='Baseline (speedup=1)', alpha=0.5)
    ax1.axhline(y=NUM_PROCESSORS, color='green', linestyle='--', label=f'Ideal ({NUM_PROCESSORS}x)', alpha=0.5)
    ax1.set_xlabel('Number of Agents', fontsize=12)
    ax1.set_ylabel('Speedup (Serial / Centralized)', fontsize=12)
    ax1.set_title('Centralized CBS Speedup\n(16 MPI Processors)', fontsize=12)
    ax1.legend(loc='upper right', fontsize=8)
    ax1.grid(True, alpha=0.3)
    ax1.set_ylim(bottom=0)
    
    # Decentralized speedup
    ax2 = axes[1]
    for map_name in merged_decentral['map'].unique():
        map_data = merged_decentral[merged_decentral['map'] == map_name]
        ax2.scatter(map_data['agents'], map_data['speedup'], 
                   label=map_name.replace('_binary.map', ''), s=80, alpha=0.7)
    
    ax2.axhline(y=1, color='red', linestyle='--', label='Baseline (speedup=1)', alpha=0.5)
    ax2.axhline(y=NUM_PROCESSORS, color='green', linestyle='--', label=f'Ideal ({NUM_PROCESSORS}x)', alpha=0.5)
    ax2.set_xlabel('Number of Agents', fontsize=12)
    ax2.set_ylabel('Speedup (Serial / Decentralized)', fontsize=12)
    ax2.set_title('Decentralized CBS Speedup\n(16 MPI Processors)', fontsize=12)
    ax2.legend(loc='upper right', fontsize=8)
    ax2.grid(True, alpha=0.3)
    ax2.set_ylim(bottom=0)
    
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'speedup_analysis.png', dpi=150, bbox_inches='tight')
    plt.savefig(OUTPUT_DIR / 'speedup_analysis.pdf', bbox_inches='tight')
    plt.close()
    print("✓ Saved speedup_analysis.png/pdf")

def plot_efficiency(merged_central, merged_decentral):
    """Plot parallel efficiency."""
    fig, ax = plt.subplots(figsize=(12, 6))
    
    # Calculate mean efficiency by agents
    central_eff = merged_central.groupby('agents')['efficiency'].mean()
    decentral_eff = merged_decentral.groupby('agents')['efficiency'].mean()
    
    width = 0.35
    agents_list = sorted(set(central_eff.index) | set(decentral_eff.index))
    x = np.arange(len(agents_list))
    
    central_vals = [central_eff.get(a, 0) for a in agents_list]
    decentral_vals = [decentral_eff.get(a, 0) for a in agents_list]
    
    ax.bar(x - width/2, central_vals, width, label='Centralized', color=COLORS['centralized'], alpha=0.8)
    ax.bar(x + width/2, decentral_vals, width, label='Decentralized', color=COLORS['decentralized'], alpha=0.8)
    
    ax.axhline(y=100, color='green', linestyle='--', label='Ideal Efficiency (100%)', alpha=0.5)
    ax.set_xlabel('Number of Agents', fontsize=12)
    ax.set_ylabel('Efficiency (%)', fontsize=12)
    ax.set_title(f'Parallel Efficiency (Speedup / {NUM_PROCESSORS} processors × 100%)', fontsize=14)
    ax.set_xticks(x)
    ax.set_xticklabels(agents_list)
    ax.legend()
    ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'efficiency.png', dpi=150, bbox_inches='tight')
    plt.savefig(OUTPUT_DIR / 'efficiency.pdf', bbox_inches='tight')
    plt.close()
    print("✓ Saved efficiency.png/pdf")

def plot_nodes_expanded(serial, central, decentral):
    """Plot nodes expanded comparison (work done)."""
    fig, ax = plt.subplots(figsize=(14, 8))
    
    all_data = pd.concat([serial, central, decentral])
    all_data = all_data[all_data['status'] == 'success']
    all_data = all_data[all_data['nodes_expanded'] > 0]  # Filter out trivial cases
    
    agents_list = sorted(all_data['agents'].unique())
    x = np.arange(len(agents_list))
    width = 0.25
    
    for i, version in enumerate(['serial', 'centralized', 'decentralized']):
        version_data = all_data[all_data['version'] == version]
        nodes = []
        for agents in agents_list:
            agent_data = version_data[version_data['agents'] == agents]
            if len(agent_data) > 0:
                nodes.append(agent_data['nodes_expanded'].mean())
            else:
                nodes.append(0)
        ax.bar(x + i * width, nodes, width, label=version.capitalize(), 
               color=COLORS[version], alpha=0.8)
    
    ax.set_xlabel('Number of Agents', fontsize=12)
    ax.set_ylabel('Nodes Expanded', fontsize=12)
    ax.set_title('Average Nodes Expanded by Number of Agents\n(Work Done per Version)', fontsize=14)
    ax.set_xticks(x + width)
    ax.set_xticklabels(agents_list)
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_yscale('log')
    
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'nodes_expanded.png', dpi=150, bbox_inches='tight')
    plt.savefig(OUTPUT_DIR / 'nodes_expanded.pdf', bbox_inches='tight')
    plt.close()
    print("✓ Saved nodes_expanded.png/pdf")

def plot_success_rate(serial, central, decentral):
    """Plot success/timeout/failure rates."""
    fig, axes = plt.subplots(1, 3, figsize=(15, 5))
    
    versions = [('Serial', serial), ('Centralized', central), ('Decentralized', decentral)]
    colors_status = {'success': '#2ECC71', 'timeout': '#F39C12', 'failure': '#E74C3C'}
    
    for ax, (name, data) in zip(axes, versions):
        status_counts = data['status'].value_counts()
        labels = status_counts.index.tolist()
        sizes = status_counts.values.tolist()
        colors = [colors_status.get(s, 'gray') for s in labels]
        
        wedges, texts, autotexts = ax.pie(sizes, labels=labels, autopct='%1.1f%%',
                                          colors=colors, startangle=90)
        ax.set_title(f'{name} CBS\n(n={len(data)} runs)', fontsize=12)
    
    plt.suptitle('Run Status Distribution by CBS Version', fontsize=14, y=1.02)
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'success_rate.png', dpi=150, bbox_inches='tight')
    plt.savefig(OUTPUT_DIR / 'success_rate.pdf', bbox_inches='tight')
    plt.close()
    print("✓ Saved success_rate.png/pdf")

def plot_runtime_by_map(serial, central, decentral):
    """Plot runtime comparison per map."""
    all_data = pd.concat([serial, central, decentral])
    all_data = all_data[all_data['status'] == 'success']
    
    maps = sorted(all_data['map'].unique())
    n_maps = len(maps)
    
    fig, axes = plt.subplots(2, 3, figsize=(15, 10))
    axes = axes.flatten()
    
    for idx, map_name in enumerate(maps):
        if idx >= 6:
            break
        ax = axes[idx]
        map_data = all_data[all_data['map'] == map_name]
        
        for version in ['serial', 'centralized', 'decentralized']:
            version_data = map_data[map_data['version'] == version]
            if len(version_data) > 0:
                version_data = version_data.sort_values('agents')
                ax.plot(version_data['agents'], version_data['runtime_sec'], 
                       marker='o', label=version.capitalize(), color=COLORS[version], 
                       linewidth=2, markersize=8)
        
        ax.set_xlabel('Agents', fontsize=10)
        ax.set_ylabel('Runtime (s)', fontsize=10)
        ax.set_title(map_name.replace('_binary.map', ''), fontsize=11)
        ax.legend(fontsize=8)
        ax.grid(True, alpha=0.3)
        ax.set_yscale('log')
    
    # Hide unused subplots
    for idx in range(n_maps, 6):
        axes[idx].set_visible(False)
    
    plt.suptitle('Runtime by Map and CBS Version', fontsize=14, y=1.02)
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'runtime_by_map.png', dpi=150, bbox_inches='tight')
    plt.savefig(OUTPUT_DIR / 'runtime_by_map.pdf', bbox_inches='tight')
    plt.close()
    print("✓ Saved runtime_by_map.png/pdf")

def plot_speedup_heatmap(merged_central, merged_decentral):
    """Create heatmaps showing speedup across maps and agent counts."""
    fig, axes = plt.subplots(1, 2, figsize=(14, 6))
    
    # Centralized heatmap
    pivot_central = merged_central.pivot_table(
        values='speedup', 
        index='map', 
        columns='agents',
        aggfunc='mean'
    )
    pivot_central.index = pivot_central.index.str.replace('_binary.map', '')
    
    im1 = axes[0].imshow(pivot_central.values, cmap='RdYlGn', aspect='auto', 
                         vmin=0, vmax=max(16, pivot_central.values.max()))
    axes[0].set_xticks(range(len(pivot_central.columns)))
    axes[0].set_xticklabels(pivot_central.columns)
    axes[0].set_yticks(range(len(pivot_central.index)))
    axes[0].set_yticklabels(pivot_central.index)
    axes[0].set_xlabel('Number of Agents')
    axes[0].set_ylabel('Map')
    axes[0].set_title('Centralized CBS Speedup')
    plt.colorbar(im1, ax=axes[0], label='Speedup')
    
    # Add text annotations
    for i in range(len(pivot_central.index)):
        for j in range(len(pivot_central.columns)):
            val = pivot_central.values[i, j]
            if not np.isnan(val):
                axes[0].text(j, i, f'{val:.1f}', ha='center', va='center', fontsize=9)
    
    # Decentralized heatmap
    pivot_decentral = merged_decentral.pivot_table(
        values='speedup', 
        index='map', 
        columns='agents',
        aggfunc='mean'
    )
    pivot_decentral.index = pivot_decentral.index.str.replace('_binary.map', '')
    
    im2 = axes[1].imshow(pivot_decentral.values, cmap='RdYlGn', aspect='auto',
                         vmin=0, vmax=max(16, pivot_decentral.values.max()))
    axes[1].set_xticks(range(len(pivot_decentral.columns)))
    axes[1].set_xticklabels(pivot_decentral.columns)
    axes[1].set_yticks(range(len(pivot_decentral.index)))
    axes[1].set_yticklabels(pivot_decentral.index)
    axes[1].set_xlabel('Number of Agents')
    axes[1].set_ylabel('Map')
    axes[1].set_title('Decentralized CBS Speedup')
    plt.colorbar(im2, ax=axes[1], label='Speedup')
    
    # Add text annotations
    for i in range(len(pivot_decentral.index)):
        for j in range(len(pivot_decentral.columns)):
            val = pivot_decentral.values[i, j]
            if not np.isnan(val):
                axes[1].text(j, i, f'{val:.1f}', ha='center', va='center', fontsize=9)
    
    plt.suptitle(f'Speedup Heatmaps (Serial / Parallel, {NUM_PROCESSORS} processors)', fontsize=14, y=1.02)
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'speedup_heatmap.png', dpi=150, bbox_inches='tight')
    plt.savefig(OUTPUT_DIR / 'speedup_heatmap.pdf', bbox_inches='tight')
    plt.close()
    print("✓ Saved speedup_heatmap.png/pdf")

def plot_comm_compute_breakdown(central, decentral):
    """Plot communication vs computation time breakdown for parallel versions."""
    # Check if we have the required columns
    has_central_timing = 'comm_time_sec' in central.columns and central['comm_time_sec'].notna().any()
    has_decentral_timing = 'comm_time_sec' in decentral.columns and decentral['comm_time_sec'].notna().any()
    
    if not has_central_timing and not has_decentral_timing:
        print("⚠ No communication/computation timing data available (run new benchmarks)")
        return
    
    fig, axes = plt.subplots(1, 2, figsize=(14, 6))
    
    # Centralized breakdown
    ax1 = axes[0]
    if has_central_timing:
        central_success = central[(central['status'] == 'success') & (central['runtime_sec'] > 0.01)]
        if len(central_success) > 0:
            central_success = central_success.sort_values('agents')
            agents = central_success['agents'].values
            comm = central_success['comm_time_sec'].values
            compute = central_success['compute_time_sec'].values
            
            x = np.arange(len(agents))
            width = 0.6
            
            ax1.bar(x, compute, width, label='Computation', color='#2E86AB', alpha=0.8)
            ax1.bar(x, comm, width, bottom=compute, label='Communication', color='#E74C3C', alpha=0.8)
            
            ax1.set_xlabel('Number of Agents', fontsize=12)
            ax1.set_ylabel('Time (seconds)', fontsize=12)
            ax1.set_title('Centralized CBS\nTime Breakdown', fontsize=12)
            ax1.set_xticks(x)
            ax1.set_xticklabels(agents)
            ax1.legend()
            ax1.grid(True, alpha=0.3)
    else:
        ax1.text(0.5, 0.5, 'No timing data', ha='center', va='center', transform=ax1.transAxes)
        ax1.set_title('Centralized CBS\n(No data)')
    
    # Decentralized breakdown
    ax2 = axes[1]
    if has_decentral_timing:
        decentral_success = decentral[(decentral['status'] == 'success') & (decentral['runtime_sec'] > 0.01)]
        if len(decentral_success) > 0:
            decentral_success = decentral_success.sort_values('agents')
            agents = decentral_success['agents'].values
            comm = decentral_success['comm_time_sec'].values
            compute = decentral_success['compute_time_sec'].values
            
            x = np.arange(len(agents))
            width = 0.6
            
            ax2.bar(x, compute, width, label='Computation', color='#2E86AB', alpha=0.8)
            ax2.bar(x, comm, width, bottom=compute, label='Communication', color='#E74C3C', alpha=0.8)
            
            ax2.set_xlabel('Number of Agents', fontsize=12)
            ax2.set_ylabel('Time (seconds)', fontsize=12)
            ax2.set_title('Decentralized CBS\nTime Breakdown', fontsize=12)
            ax2.set_xticks(x)
            ax2.set_xticklabels(agents)
            ax2.legend()
            ax2.grid(True, alpha=0.3)
    else:
        ax2.text(0.5, 0.5, 'No timing data', ha='center', va='center', transform=ax2.transAxes)
        ax2.set_title('Decentralized CBS\n(No data)')
    
    plt.suptitle('Communication vs Computation Time', fontsize=14, y=1.02)
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'comm_compute_breakdown.png', dpi=150, bbox_inches='tight')
    plt.savefig(OUTPUT_DIR / 'comm_compute_breakdown.pdf', bbox_inches='tight')
    plt.close()
    print("✓ Saved comm_compute_breakdown.png/pdf")

def plot_comm_percentage(central, decentral):
    """Plot communication time as percentage of total runtime."""
    has_central_timing = 'comm_time_sec' in central.columns and central['comm_time_sec'].notna().any()
    has_decentral_timing = 'comm_time_sec' in decentral.columns and decentral['comm_time_sec'].notna().any()
    
    if not has_central_timing and not has_decentral_timing:
        return
    
    fig, ax = plt.subplots(figsize=(12, 6))
    
    all_data = []
    
    if has_central_timing:
        central_success = central[(central['status'] == 'success') & (central['runtime_sec'] > 0.01)].copy()
        if len(central_success) > 0:
            total_time = central_success['comm_time_sec'] + central_success['compute_time_sec']
            central_success['comm_pct'] = np.where(total_time > 0, (central_success['comm_time_sec'] / total_time) * 100, 0)
            central_success['version'] = 'centralized'
            all_data.append(central_success[['agents', 'comm_pct', 'version']])
    
    if has_decentral_timing:
        decentral_success = decentral[(decentral['status'] == 'success') & (decentral['runtime_sec'] > 0.01)].copy()
        if len(decentral_success) > 0:
            total_time = decentral_success['comm_time_sec'] + decentral_success['compute_time_sec']
            decentral_success['comm_pct'] = np.where(total_time > 0, (decentral_success['comm_time_sec'] / total_time) * 100, 0)
            decentral_success['version'] = 'decentralized'
            all_data.append(decentral_success[['agents', 'comm_pct', 'version']])
    
    if not all_data:
        plt.close()
        return
    
    combined = pd.concat(all_data)
    agents_list = sorted(combined['agents'].unique())
    x = np.arange(len(agents_list))
    width = 0.35
    
    central_pcts = []
    decentral_pcts = []
    for agents in agents_list:
        agent_data = combined[combined['agents'] == agents]
        c_data = agent_data[agent_data['version'] == 'centralized']
        d_data = agent_data[agent_data['version'] == 'decentralized']
        central_pcts.append(c_data['comm_pct'].mean() if len(c_data) > 0 else 0)
        decentral_pcts.append(d_data['comm_pct'].mean() if len(d_data) > 0 else 0)
    
    ax.bar(x - width/2, central_pcts, width, label='Centralized', color=COLORS['centralized'], alpha=0.8)
    ax.bar(x + width/2, decentral_pcts, width, label='Decentralized', color=COLORS['decentralized'], alpha=0.8)
    
    ax.set_xlabel('Number of Agents', fontsize=12)
    ax.set_ylabel('Communication Time (%)', fontsize=12)
    ax.set_title('Communication Overhead (% of Total Runtime)', fontsize=14)
    ax.set_xticks(x)
    ax.set_xticklabels(agents_list)
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_ylim(0, 100)
    
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'comm_percentage.png', dpi=150, bbox_inches='tight')
    plt.savefig(OUTPUT_DIR / 'comm_percentage.pdf', bbox_inches='tight')
    plt.close()
    print("✓ Saved comm_percentage.png/pdf")

def generate_summary_stats(serial, central, decentral, merged_central, merged_decentral):
    """Generate and save summary statistics."""
    summary = []
    
    summary.append("=" * 70)
    summary.append("CBS PERFORMANCE SUMMARY")
    summary.append(f"Parallel implementations run with {NUM_PROCESSORS} MPI processors")
    summary.append("=" * 70)
    
    # Overall statistics
    summary.append("\n--- Run Statistics ---")
    for name, data in [('Serial', serial), ('Centralized', central), ('Decentralized', decentral)]:
        success = len(data[data['status'] == 'success'])
        timeout = len(data[data['status'] == 'timeout'])
        failure = len(data[data['status'] == 'failure'])
        total = len(data)
        summary.append(f"{name:15}: {success:3} success, {timeout:3} timeout, {failure:3} failure (total: {total})")
    
    # Speedup statistics
    summary.append("\n--- Speedup Statistics ---")
    if len(merged_central) > 0:
        summary.append(f"Centralized CBS:")
        summary.append(f"  Mean speedup: {merged_central['speedup'].mean():.2f}x")
        summary.append(f"  Max speedup:  {merged_central['speedup'].max():.2f}x")
        summary.append(f"  Min speedup:  {merged_central['speedup'].min():.2f}x")
        summary.append(f"  Mean efficiency: {merged_central['efficiency'].mean():.1f}%")
    
    if len(merged_decentral) > 0:
        summary.append(f"Decentralized CBS:")
        summary.append(f"  Mean speedup: {merged_decentral['speedup'].mean():.2f}x")
        summary.append(f"  Max speedup:  {merged_decentral['speedup'].max():.2f}x")
        summary.append(f"  Min speedup:  {merged_decentral['speedup'].min():.2f}x")
        summary.append(f"  Mean efficiency: {merged_decentral['efficiency'].mean():.1f}%")
    
    # Runtime comparison (successful runs only)
    summary.append("\n--- Average Runtime by Agents (successful runs) ---")
    all_data = pd.concat([serial, central, decentral])
    all_data = all_data[all_data['status'] == 'success']
    
    for agents in sorted(all_data['agents'].unique()):
        agent_data = all_data[all_data['agents'] == agents]
        summary.append(f"Agents = {agents}:")
        for version in ['serial', 'centralized', 'decentralized']:
            version_data = agent_data[agent_data['version'] == version]
            if len(version_data) > 0:
                summary.append(f"  {version:15}: {version_data['runtime_sec'].mean():.3f}s (n={len(version_data)})")
    
    summary.append("\n" + "=" * 70)
    
    summary_text = "\n".join(summary)
    print(summary_text)
    
    with open(OUTPUT_DIR / 'summary_stats.txt', 'w') as f:
        f.write(summary_text)
    print(f"\n✓ Saved summary_stats.txt")
    
    return summary_text

def export_comparison_csv(merged_central, merged_decentral):
    """Export detailed comparison data to CSV."""
    # Centralized comparison
    central_export = merged_central[['map', 'agents', 'runtime_sec_serial', 'runtime_sec_central', 
                                      'speedup', 'efficiency', 'nodes_expanded_serial', 'nodes_expanded_central']].copy()
    central_export.columns = ['map', 'agents', 'serial_runtime', 'central_runtime', 
                              'speedup', 'efficiency_pct', 'serial_nodes', 'central_nodes']
    central_export = central_export.round(4)
    central_export.to_csv(OUTPUT_DIR / 'comparison_central.csv', index=False)
    
    # Decentralized comparison
    decentral_export = merged_decentral[['map', 'agents', 'runtime_sec_serial', 'runtime_sec_decentral',
                                          'speedup', 'efficiency', 'nodes_expanded_serial', 'nodes_expanded_decentral']].copy()
    decentral_export.columns = ['map', 'agents', 'serial_runtime', 'decentral_runtime',
                                'speedup', 'efficiency_pct', 'serial_nodes', 'decentral_nodes']
    decentral_export = decentral_export.round(4)
    decentral_export.to_csv(OUTPUT_DIR / 'comparison_decentral.csv', index=False)
    
    print("✓ Saved comparison_central.csv and comparison_decentral.csv")

def main():
    """Main visualization pipeline."""
    print("=" * 50)
    print("CBS Results Visualization Pipeline")
    print("=" * 50)
    
    # Create output directory
    OUTPUT_DIR.mkdir(exist_ok=True)
    print(f"\nOutput directory: {OUTPUT_DIR.absolute()}")
    
    # Load data
    print("\n--- Loading data ---")
    serial, central, decentral = load_results()
    print(f"Serial:       {len(serial)} rows")
    print(f"Centralized:  {len(central)} rows")
    print(f"Decentralized: {len(decentral)} rows")
    
    # Create merged datasets for speedup calculation
    print("\n--- Creating merged datasets ---")
    merged_central, merged_decentral = create_merged_dataset(serial, central, decentral)
    merged_central, merged_decentral = calculate_speedup_efficiency(merged_central, merged_decentral)
    print(f"Matched serial+centralized runs: {len(merged_central)}")
    print(f"Matched serial+decentralized runs: {len(merged_decentral)}")
    
    # Generate plots
    print("\n--- Generating plots ---")
    plot_runtime_comparison(serial, central, decentral)
    plot_speedup_analysis(merged_central, merged_decentral)
    plot_efficiency(merged_central, merged_decentral)
    plot_nodes_expanded(serial, central, decentral)
    plot_success_rate(serial, central, decentral)
    plot_runtime_by_map(serial, central, decentral)
    plot_speedup_heatmap(merged_central, merged_decentral)
    plot_comm_compute_breakdown(central, decentral)
    plot_comm_percentage(central, decentral)
    
    # Generate statistics
    print("\n--- Generating summary ---")
    generate_summary_stats(serial, central, decentral, merged_central, merged_decentral)
    
    # Export comparison data
    print("\n--- Exporting comparison data ---")
    export_comparison_csv(merged_central, merged_decentral)
    
    print("\n" + "=" * 50)
    print("Visualization pipeline complete!")
    print(f"Output files saved to: {OUTPUT_DIR.absolute()}")
    print("=" * 50)

if __name__ == "__main__":
    main()
