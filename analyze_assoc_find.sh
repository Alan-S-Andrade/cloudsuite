#!/bin/bash
# Script to analyze assoc_find() latency from Docker logs
# Usage: ./analyze_assoc_find.sh <container_name> [output_file]

if [ -z "$1" ]; then
    echo "Usage: $0 <container_name> [output_file]"
    echo "Example: $0 dc-server timing_analysis.txt"
    exit 1
fi

CONTAINER=$1
OUTPUT_FILE=${2:-"assoc_find_analysis.txt"}

echo "Analyzing assoc_find() latency from container: $CONTAINER"
echo "Collecting logs..."

# Get logs and analyze with awk
docker logs $CONTAINER 2>&1 | grep "assoc_find" | awk '
BEGIN {
    hit_count = 0
    miss_count = 0
    hit_total_ns = 0
    miss_total_ns = 0
    hit_min_ns = 999999999999
    hit_max_ns = 0
    miss_min_ns = 999999999999
    miss_max_ns = 0
}
{
    # Parse: assoc_find HIT depth=0 time_ns=12345
    if ($2 == "HIT") {
        hit_count++
        time_ns = $NF
        gsub(/[^0-9]/, "", time_ns)
        hit_total_ns += time_ns
        if (time_ns < hit_min_ns) hit_min_ns = time_ns
        if (time_ns > hit_max_ns) hit_max_ns = time_ns
    } else if ($2 == "MISS") {
        miss_count++
        time_ns = $NF
        gsub(/[^0-9]/, "", time_ns)
        miss_total_ns += time_ns
        if (time_ns < miss_min_ns) miss_min_ns = time_ns
        if (time_ns > miss_max_ns) miss_max_ns = time_ns
    }
}
END {
    printf "=" > "/tmp/analysis_output.txt"
    printf "%s\n", "assoc_find() Latency Analysis" > "/tmp/analysis_output.txt"
    printf "=" > "/tmp/analysis_output.txt"
    printf "\n" > "/tmp/analysis_output.txt"
    
    printf "HITS:\n" > "/tmp/analysis_output.txt"
    printf "  Count:       %d\n", hit_count > "/tmp/analysis_output.txt"
    if (hit_count > 0) {
        avg_hit = hit_total_ns / hit_count
        printf "  Total:       %lld ns\n", hit_total_ns > "/tmp/analysis_output.txt"
        printf "  Average:     %.2f ns (%.4f µs)\n", avg_hit, avg_hit/1000 > "/tmp/analysis_output.txt"
        printf "  Min:         %lld ns\n", hit_min_ns > "/tmp/analysis_output.txt"
        printf "  Max:         %lld ns\n", hit_max_ns > "/tmp/analysis_output.txt"
    }
    printf "\n" > "/tmp/analysis_output.txt"
    
    printf "MISSES:\n" > "/tmp/analysis_output.txt"
    printf "  Count:       %d\n", miss_count > "/tmp/analysis_output.txt"
    if (miss_count > 0) {
        avg_miss = miss_total_ns / miss_count
        printf "  Total:       %lld ns\n", miss_total_ns > "/tmp/analysis_output.txt"
        printf "  Average:     %.2f ns (%.4f µs)\n", avg_miss, avg_miss/1000 > "/tmp/analysis_output.txt"
        printf "  Min:         %lld ns\n", miss_min_ns > "/tmp/analysis_output.txt"
        printf "  Max:         %lld ns\n", miss_max_ns > "/tmp/analysis_output.txt"
    }
    printf "\n" > "/tmp/analysis_output.txt"
    
    printf "SUMMARY:\n" > "/tmp/analysis_output.txt"
    printf "  Total Operations: %d\n", hit_count + miss_count > "/tmp/analysis_output.txt"
    printf "  Hit Rate:         %.2f%%\n", (hit_count * 100.0 / (hit_count + miss_count)) > "/tmp/analysis_output.txt"
    if (hit_count > 0 && miss_count > 0) {
        printf "  Hit/Miss Ratio:   %.2f\n", hit_count / miss_count > "/tmp/analysis_output.txt"
    }
}' > /tmp/analysis_output.txt

cat /tmp/analysis_output.txt
cp /tmp/analysis_output.txt "$OUTPUT_FILE"
echo ""
echo "Analysis saved to: $OUTPUT_FILE"
