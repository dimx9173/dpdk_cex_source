#!/bin/bash
# Verify WebSocket Network Resilience
# Usage: sudo ./scripts/verify_network_resilience.sh

export WS_TEST_DURATION=60
BUILD_DIR=build
TEST_BIN=$BUILD_DIR/tests/ws_test
LOG_FILE=network_resilience.log

# Ensure we are running as root
if [ "$EUID" -ne 0 ]; then 
  echo "Please run as root"
  exit 1
fi

echo "Starting Network Resilience Test..."
echo "duration: 60s per exchange. Total approx 120s."

# Start test in background
echo "Launching ws_test..."
$TEST_BIN > $LOG_FILE 2>&1 &
TEST_PID=$!
echo "ws_test started with PID $TEST_PID"

# --- OKX Phase (0-60s) ---
echo "[OKX] Waiting 10s for success connection..."
sleep 10

echo "[OKX] BLOCKING Port 8443 (Simulating Network Failure)..."
iptables -I OUTPUT -p tcp --dport 8443 -j DROP

echo "[OKX] Port blocked. Waiting 20s..."
sleep 20

echo "[OKX] UNBLOCKING Port 8443..."
iptables -D OUTPUT -p tcp --dport 8443 -j DROP
echo "[OKX] Network restored. Expect reconnection..."

echo "[OKX] Waiting 35s for phase completion..."
sleep 35

# --- Bybit Phase (60-120s) ---
echo "[Bybit] Test phase starting (approx)..."
echo "[Bybit] Waiting 10s for success connection..."
sleep 10

echo "[Bybit] BLOCKING Port 443 (Simulating Network Failure)..."
iptables -I OUTPUT -p tcp --dport 443 -j DROP

echo "[Bybit] Port blocked. Waiting 20s..."
sleep 20

echo "[Bybit] UNBLOCKING Port 443..."
iptables -D OUTPUT -p tcp --dport 443 -j DROP
echo "[Bybit] Network restored. Expect reconnection..."

# Wait for process
echo "Waiting for test process to exit..."
wait $TEST_PID

echo "Test complete."
echo "Showing Reconnection Events from Log:"
grep -E "Successfully connected|Reconnection|Reconnected" $LOG_FILE
