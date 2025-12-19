#!/bin/bash
# Benchmark Throughput 
# Requires TRex or PKTGEN

TARGET_IP="192.168.100.1"
DURATION=30

echo "=== Throughput Benchmark ==="

if ! command -v pktgen &>/dev/null; then
    echo "Error: 'pktgen' not found. Please install DPDK Pktgen or TRex."
    echo "Steps:"
    echo "1. Configure traffic generator on peer machine."
    echo "2. Send 64-byte UDP packets to $TARGET_IP:45678."
    echo "3. Measure RX/TX rate on this machine via 'dpdk-proc-info --stats'."
    exit 1
fi

echo "Starting Traffic Generator..."
# Placeholder for actual pktgen command
