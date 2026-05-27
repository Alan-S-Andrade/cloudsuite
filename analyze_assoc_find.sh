#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <container_name> [output_file]"
    echo "Example: $0 dc-server timing_analysis.txt"
    exit 1
fi

CONTAINER=$1
OUTPUT_FILE=${2:-assoc_find_analysis.txt}

echo "Analyzing assoc_find() latency from container: $CONTAINER"
echo "Collecting aggregate timing from logs..."

SUMMARY=$(docker logs "$CONTAINER" 2>&1 | grep "assoc_find Time:" | tail -n 1 || true)

{
    echo "assoc_find() Latency Analysis"
    echo "============================="
    echo
    if [ -n "$SUMMARY" ]; then
        echo "$SUMMARY"
    else
        echo "No aggregate assoc_find timing line found."
        echo "Make sure the server image was rebuilt and memcached exited cleanly."
    fi
} | tee "$OUTPUT_FILE"

echo
echo "Analysis saved to: $OUTPUT_FILE"
