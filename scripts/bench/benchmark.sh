#!/bin/bash
# Performance Benchmark Script
# Measures latency metrics for HFT Gateway

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_metric() { echo -e "${CYAN}[METRIC]${NC} $1"; }

# Benchmark JSON parsing with simdjson
benchmark_json_parsing() {
    log_info "Benchmarking JSON parsing..."
    
    python3 << 'EOF'
import time
import json

# Sample OKX message
sample_msg = '''
{
    "arg": {"channel": "books-l2-tbt", "instId": "BTC-USDT"},
    "action": "snapshot",
    "data": [{
        "bids": [["60000.5", "1.5", "0", "1"], ["60000.0", "2.0", "0", "1"]],
        "asks": [["60001.0", "0.5", "0", "1"], ["60001.5", "1.0", "0", "1"]],
        "ts": "1702500000000"
    }]
}
'''

iterations = 100000

# Benchmark stdlib json
start = time.perf_counter_ns()
for _ in range(iterations):
    data = json.loads(sample_msg)
    _ = data["data"][0]["bids"][0]
end = time.perf_counter_ns()

avg_ns = (end - start) / iterations
print(f"  Python json.loads: {avg_ns:.0f} ns/parse ({iterations} iterations)")
print(f"  Throughput: {1e9 / avg_ns:.0f} parses/sec")
EOF
    
    echo ""
}

# Benchmark WebSocket message roundtrip
benchmark_websocket_roundtrip() {
    log_info "Benchmarking WebSocket roundtrip..."
    
    # Check if mock exchange is running
    if ! nc -z localhost 8765 2>/dev/null; then
        log_info "Starting mock exchange..."
        python3 "$SCRIPT_DIR/mock_exchange.py" &
        MOCK_PID=$!
        sleep 2
        trap "kill $MOCK_PID 2>/dev/null" EXIT
    fi
    
    python3 << 'EOF'
import asyncio
import websockets
import json
import time
import statistics

async def benchmark():
    uri = "ws://localhost:8765/ws/v5/public"
    latencies = []
    
    async with websockets.connect(uri, ping_interval=None) as ws:
        # Subscribe
        await ws.send(json.dumps({
            "op": "subscribe",
            "args": [{"channel": "books-l2-tbt", "instId": "BTC-USDT"}]
        }))
        
        # Measure receive latency
        for _ in range(100):
            start = time.perf_counter_ns()
            msg = await ws.recv()
            data = json.loads(msg)
            end = time.perf_counter_ns()
            
            if "data" in data:
                latencies.append(end - start)
    
    if latencies:
        avg = statistics.mean(latencies) / 1000  # us
        p50 = statistics.median(latencies) / 1000
        p99 = sorted(latencies)[int(len(latencies) * 0.99)] / 1000
        
        print(f"  Avg Latency:  {avg:.1f} μs")
        print(f"  P50 Latency:  {p50:.1f} μs")
        print(f"  P99 Latency:  {p99:.1f} μs")
        print(f"  Samples:      {len(latencies)}")

asyncio.run(benchmark())
EOF
    
    echo ""
}

# Summary
print_benchmark_summary() {
    echo ""
    echo "========================================"
    echo "    Performance Benchmark Summary       "
    echo "========================================"
    echo "  Target: Wire-to-Strategy < 5μs"
    echo "  Target: Throughput > 10 Mpps/core"
    echo "========================================"
    echo ""
    log_info "Note: Full DPDK latency requires hardware NIC testing"
}

# Main
main() {
    log_info "Starting Performance Benchmark"
    echo ""
    
    benchmark_json_parsing
    benchmark_websocket_roundtrip
    print_benchmark_summary
}

main "$@"
