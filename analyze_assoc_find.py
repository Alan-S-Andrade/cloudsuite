#!/usr/bin/env python3
"""
Analyze assoc_find() latency logs from memcached
Usage: python3 analyze_assoc_find.py <container_name> [output_file]
"""

import subprocess
import re
import sys
from statistics import mean, median, stdev

def analyze_container_logs(container_name):
    """Get and parse logs from Docker container"""
    try:
        result = subprocess.run(
            ['docker', 'logs', container_name],
            capture_output=True,
            text=True,
            stderr=subprocess.STDOUT
        )
        logs = result.stdout + result.stderr
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

def calculate_stats(data, name):
    """Calculate statistics for a dataset"""
    if not data:
        print(f"\n{name}:")
        print("  No data collected")
        return
    
    times = [t for _, t in data]
    depths = [d for d, _ in data]
    
    print(f"\n{name}:")
    print(f"  Count:              {len(times)}")
    print(f"  Total time:         {sum(times):,} ns")
    print(f"  Average time:       {mean(times):.2f} ns ({mean(times)/1000:.4f} µs)")
    print(f"  Median time:        {median(times):.2f} ns ({median(times)/1000:.4f} µs)")
    
    if len(times) > 1:
        print(f"  Std Dev:            {stdev(times):.2f} ns")
    
    print(f"  Min time:           {min(times)} ns")
    print(f"  Max time:           {max(times)} ns")
    print(f"  Avg depth:          {mean(depths):.2f}")
    print(f"  Min depth:          {min(depths)}")
    print(f"  Max depth:          {max(depths)}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 analyze_assoc_find.py <container_name> [output_file]")
        print("Example: python3 analyze_assoc_find.py dc-server timing_analysis.txt")
        sys.exit(1)
    
    container_name = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else "assoc_find_analysis.txt"
    
    print(f"Analyzing assoc_find() latency from container: {container_name}")
    print("Collecting logs...")
    
    result = analyze_container_logs(container_name)
    if result is None:
        sys.exit(1)
    
    hits, misses = result
    
    print("\n" + "=" * 60)
    print("assoc_find() Latency Analysis")
    print("=" * 60)
    
    calculate_stats(hits, "HITS")
    calculate_stats(misses, "MISSES")
    
    # Summary
    total = len(hits) + len(misses)
    if total > 0:
        print(f"\nSUMMARY:")
        print(f"  Total operations:   {total}")
        print(f"  Hit rate:           {len(hits) * 100.0 / total:.2f}%")
        print(f"  Miss rate:          {len(misses) * 100.0 / total:.2f}%")
        if len(hits) > 0 and len(misses) > 0:
            hit_avg = mean([t for _, t in hits])
            miss_avg = mean([t for _, t in misses])
            print(f"  Hit/Miss avg ratio: {hit_avg / miss_avg:.2f}x (miss slower)")
    
    # Write to file
    print("\n" + "=" * 60)
    print(f"Analysis saved to: {output_file}")

if __name__ == '__main__':
    main()
