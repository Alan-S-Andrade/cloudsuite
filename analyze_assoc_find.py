#!/usr/bin/env python3
"""
Analyze assoc_find() latency logs from memcached and generate CDF plot
Usage: python3 analyze_assoc_find.py <container_name> [output_file]
"""

import subprocess
import re
import sys
import signal
import numpy as np
import matplotlib.pyplot as plt
from statistics import mean, median, stdev

# 5 minute timeout
TIMEOUT = 300

def timeout_handler(signum, frame):
    print("\nTimeout reached. Processing collected data...")
    raise TimeoutError("Script execution timeout")

def analyze_container_logs(container_name):
    """Get and parse logs from Docker container with timeout"""
    try:
        # Use --tail to get only recent logs (much faster than reading all logs)
        result = subprocess.run(
            ['docker', 'logs', '--tail', '1000000', container_name],
            capture_output=True,
            text=True,
            timeout=60  # 1 minute timeout for log retrieval
        )
        logs = result.stdout
        if result.stderr:
            logs += result.stderr
    except subprocess.TimeoutExpired:
        print("Docker logs collection timeout, retrying with fewer logs...")
        try:
            # Try with even fewer logs
            result = subprocess.run(
                ['docker', 'logs', '--tail', '100000', container_name],
                capture_output=True,
                text=True,
                timeout=30
            )
            logs = result.stdout
            if result.stderr:
                logs += result.stderr
        except subprocess.TimeoutExpired:
            print("Still timing out. Using available data...")
            return None
    except Exception as e:
        print(f"Error getting logs: {e}")
        return None
    
    hits = []
    misses = []
    
    # Pattern: assoc_find HIT depth=0 time_ns=12345
    pattern = r'assoc_find\s+(HIT|MISS)\s+depth=(\d+)\s+time_ns=(\d+)'
    
    for match in re.finditer(pattern, logs):
        result_type = match.group(1)
        depth = int(match.group(2))
        time_ns = int(match.group(3))
        
        if result_type == "HIT":
            hits.append((depth, time_ns))
        else:
            misses.append((depth, time_ns))
    
    return hits, misses

def calculate_cdf(times):
    """Calculate CDF for a list of times"""
    sorted_times = np.sort(times)
    cdf = np.arange(1, len(sorted_times) + 1) / len(sorted_times)
    return sorted_times, cdf

def generate_cdf_plot(hits, misses, output_file):
    """Generate CDF plot for hits and misses"""
    if not hits or not misses:
        print("Error: Need both hits and misses data for CDF plot")
        return False
    
    hit_times = np.array([t for _, t in hits])
    miss_times = np.array([t for _, t in misses])
    
    hit_sorted, hit_cdf = calculate_cdf(hit_times)
    miss_sorted, miss_cdf = calculate_cdf(miss_times)
    
    # Create figure with larger size
    fig, ax = plt.subplots(figsize=(14, 8))
    
    # Convert to microseconds for readability
    # Plot with different styles and markers for better visibility
    ax.plot(hit_sorted / 1000, hit_cdf * 100, linewidth=2.5, label='Cache Hit', 
            color='green', linestyle='-', marker='o', markersize=2, markevery=100, alpha=0.8)
    ax.plot(miss_sorted / 1000, miss_cdf * 100, linewidth=2.5, label='Cache Miss', 
            color='red', linestyle='--', marker='s', markersize=2, markevery=100, alpha=0.8)
    
    ax.set_xlabel('Latency (µs)', fontsize=13, fontweight='bold')
    ax.set_ylabel('Cumulative Probability (%)', fontsize=13, fontweight='bold')
    ax.set_title('assoc_find() Latency CDF - Hits vs Misses', fontsize=15, fontweight='bold')
    ax.legend(fontsize=12, loc='lower right', framealpha=0.9)
    ax.grid(True, alpha=0.3, linestyle=':')
    
    # Add some statistics as text
    hit_p99 = np.percentile(hit_times, 99) / 1000
    miss_p99 = np.percentile(miss_times, 99) / 1000
    textstr = f'Hit 99th: {hit_p99:.2f}µs | Miss 99th: {miss_p99:.2f}µs | Ratio: {miss_p99/hit_p99:.2f}x'
    ax.text(0.98, 0.05, textstr, transform=ax.transAxes, fontsize=10,
            verticalalignment='bottom', horizontalalignment='right',
            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
    
    plt.tight_layout()
    
    # Save figure
    plt.savefig(output_file, dpi=150)
    print(f"CDF plot saved to: {output_file}")
    
    # Also save as PDF
    pdf_file = output_file.replace('.png', '.pdf')
    plt.savefig(pdf_file, dpi=150)
    print(f"CDF plot also saved to: {pdf_file}")
    
    plt.close()
    return True

def print_statistics(hits, misses):
    """Print summary statistics"""
    hit_times = [t for _, t in hits]
    miss_times = [t for _, t in misses]
    
    print("\n" + "=" * 70)
    print("assoc_find() Latency Analysis - CDF")
    print("=" * 70)
    
    print(f"\nCACHE HITS:")
    print(f"  Count:                {len(hit_times):,}")
    print(f"  Mean latency:         {mean(hit_times):.2f} ns ({mean(hit_times)/1000:.4f} µs)")
    print(f"  Median latency:       {median(hit_times):.2f} ns ({median(hit_times)/1000:.4f} µs)")
    print(f"  Std Dev:              {stdev(hit_times):.2f} ns")
    print(f"  Min latency:          {min(hit_times)} ns")
    print(f"  Max latency:          {max(hit_times)} ns")
    print(f"  99th percentile:      {np.percentile(hit_times, 99):.2f} ns ({np.percentile(hit_times, 99)/1000:.4f} µs)")
    print(f"  95th percentile:      {np.percentile(hit_times, 95):.2f} ns ({np.percentile(hit_times, 95)/1000:.4f} µs)")
    print(f"  50th percentile:      {np.percentile(hit_times, 50):.2f} ns ({np.percentile(hit_times, 50)/1000:.4f} µs)")
    
    print(f"\nCACHE MISSES:")
    print(f"  Count:                {len(miss_times):,}")
    print(f"  Mean latency:         {mean(miss_times):.2f} ns ({mean(miss_times)/1000:.4f} µs)")
    print(f"  Median latency:       {median(miss_times):.2f} ns ({median(miss_times)/1000:.4f} µs)")
    print(f"  Std Dev:              {stdev(miss_times):.2f} ns")
    print(f"  Min latency:          {min(miss_times)} ns")
    print(f"  Max latency:          {max(miss_times)} ns")
    print(f"  99th percentile:      {np.percentile(miss_times, 99):.2f} ns ({np.percentile(miss_times, 99)/1000:.4f} µs)")
    print(f"  95th percentile:      {np.percentile(miss_times, 95):.2f} ns ({np.percentile(miss_times, 95)/1000:.4f} µs)")
    print(f"  50th percentile:      {np.percentile(miss_times, 50):.2f} ns ({np.percentile(miss_times, 50)/1000:.4f} µs)")
    
    total = len(hit_times) + len(miss_times)
    print(f"\nOVERALL SUMMARY:")
    print(f"  Total operations:     {total:,}")
    print(f"  Hit rate:             {len(hit_times) * 100.0 / total:.2f}%")
    print(f"  Miss rate:            {len(miss_times) * 100.0 / total:.2f}%")
    hit_avg = mean(hit_times)
    miss_avg = mean(miss_times)
    print(f"  Mean latency ratio:   {miss_avg / hit_avg:.2f}x (miss slower)")
    print("=" * 70)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 analyze_assoc_find.py <container_name> [output_file.png]")
        print("Example: python3 analyze_assoc_find.py dc-server assoc_find_cdf.png")
        print("Note: Script has a 5-minute timeout")
        sys.exit(1)
    
    # Set up timeout handler
    signal.signal(signal.SIGALRM, timeout_handler)
    signal.alarm(TIMEOUT)
    
    try:
        container_name = sys.argv[1]
        output_file = sys.argv[2] if len(sys.argv) > 2 else "assoc_find_cdf.png"
        
        print(f"Analyzing assoc_find() latency from container: {container_name}")
        print(f"Timeout: {TIMEOUT} seconds")
        print("Collecting logs (max 4 minutes)...")
        
        result = analyze_container_logs(container_name)
        if result is None:
            print("No data collected")
            signal.alarm(0)
            sys.exit(1)
        
        hits, misses = result
        
        if not hits or not misses:
            print("Error: No hit or miss data found in logs")
            print("Make sure the container is running with verbose mode (-vv)")
            signal.alarm(0)
            sys.exit(1)
        
        print(f"Collected {len(hits)} hits and {len(misses)} misses")
        print("Generating plot...")
        
        print_statistics(hits, misses)
        generate_cdf_plot(hits, misses, output_file)
        
        signal.alarm(0)  # Cancel alarm
        print("\nDone!")
        
    except TimeoutError:
        print("Overall execution timeout reached")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        sys.exit(0)

if __name__ == '__main__':
    main()
