#!/bin/bash
set -e

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

log_pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; }
log_info() { echo -e "[INFO] $1"; }

TARGET_IP="192.168.100.1" # Default TAP IP from deploy.sh

echo "=== Functional Integration Test ==="

# 1. Check if Application is Running
if ! pgrep -f "hft-app" > /dev/null; then
    log_fail "hft-app is not running. Please start it with 'scripts/deploy.sh' first."
    exit 1
fi
log_pass "hft-app is running."

# 2. Test ICMP Ping (Exception Path)
log_info "Testing ICMP Ping to $TARGET_IP (Exception Path)..."
if ping -c 3 -W 1 "$TARGET_IP" > /dev/null; then
    log_pass "ICMP Ping successful."
else
    log_fail "ICMP Ping failed. Check TAP interface configuration."
    exit 1
fi

# 3. Test SSH Connectivity (TCPSYN via Exception Path)
# We won't actually login, just check if port 22 is reachable via the TAP IP
log_info "Testing SSH Reachability on $TARGET_IP:22..."
if command -v nc > /dev/null; then
    if nc -z -w 2 "$TARGET_IP" 22; then
        log_pass "SSH Port 22 reachable via TAP."
    else
        log_fail "SSH Port 22 unreachable via TAP."
        exit 1
    fi
else
    log_info "nc (netcat) not found, skipping SSH check."
fi

# 4. HFT Mock Injection (Fast Path)
# We depend on StrategyEngine logs to confirm receipt.
# This script just sends the packet.
log_info "Injecting Mock HFT Packet (UDP) to Port 45678..."
if command -v nc > /dev/null; then
    echo '{"op":"subscribe","args":["tickers.BTC-USDT"]}' | nc -u -w 1 "$TARGET_IP" 45678
    log_pass "Packet injected. Check application logs for 'Fast Path RX'."
else
    log_info "nc not found, skipping packet injection."
fi

echo "=== Integration Test Complete ==="
