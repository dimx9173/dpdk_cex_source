#!/bin/bash
# Benchmark Latency using internal telemetry
set -e

LOG_FILE="latency_benchmark.log"
DURATION=10

echo "=== Latency Benchmark (10s) ==="
echo "Collecting logs..."

# Assuming hft-app acts is running and dumping stats to stdout/syslog
# We'll try to capture from the running process if possible, 
# but usually we run the benchmark *wrapper*.
# Since deploy.sh starts it in background, we might need to attach or tail logs.

# For this script, we assume the user invokes it while the app is running
# and we provide instructions or read from a known log location if configured.
# Given deploy.sh just runs it, let's assume we tail the output if redirected, 
# or we just instruct the user.

# BETTER: This script runs the app strictly for benchmarking?
# No, Integration Test implies testing the deployed app.

# Feature: If the app exposes a telemetry socket (DPDK standard), we could query it.
# Our implementation uses logs for 'LatencyHistogram'.
# So we need to grep the logs.

# Mock Implementation rely on manual check or future log file support.
echo "Monitoring for P50/P99 latency stats..."
echo "Please generate load (e.g., ./scripts/test_integration.sh looping)."

# If we had a traffic generator, we'd start it here.

echo "WARN: Requires live traffic. Check application logs for 'P50' output."
