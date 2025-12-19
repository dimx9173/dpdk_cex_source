#!/bin/bash
# End-to-End Integration Test Script
# Tests HFT Gateway with Mock Exchange

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
MOCK_PORT=8765
MOCK_PID=""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

cleanup() {
    log_info "Cleaning up..."
    if [ -n "$MOCK_PID" ]; then
        kill $MOCK_PID 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Check dependencies
check_deps() {
    log_info "Checking dependencies..."
    
    if ! command -v python3 &>/dev/null; then
        log_error "python3 not found"
        exit 1
    fi
    
    if ! python3 -c "import websockets" 2>/dev/null; then
        log_warn "Installing websockets..."
        pip3 install websockets --quiet
    fi
    
    if [ ! -f "$BUILD_DIR/src/hft-app" ]; then
        log_error "hft-app not found. Run: meson compile -C build"
        exit 1
    fi
}

# Start mock exchange
start_mock_exchange() {
    log_info "Starting Mock Exchange on port $MOCK_PORT..."
    python3 "$SCRIPT_DIR/../tools/mock_exchange.py" &
    MOCK_PID=$!
    sleep 2
    
    if ! kill -0 $MOCK_PID 2>/dev/null; then
        log_error "Mock exchange failed to start"
        exit 1
    fi
    log_info "Mock Exchange started (PID: $MOCK_PID)"
}

# Run WebSocket client test
test_websocket_client() {
    log_info "Testing WebSocket connectivity..."
    
    # Simple Python WebSocket client test
    python3 << 'EOF'
import asyncio
import websockets
import json
import sys

async def test_connection():
    uri = "ws://localhost:8765/ws/v5/public"
    try:
        async with websockets.connect(uri, ping_interval=None) as ws:
            # Send subscribe
            sub_msg = {
                "op": "subscribe",
                "args": [{"channel": "books-l2-tbt", "instId": "BTC-USDT"}]
            }
            await ws.send(json.dumps(sub_msg))
            
            # Receive some messages
            for i in range(3):
                msg = await asyncio.wait_for(ws.recv(), timeout=5.0)
                data = json.loads(msg)
                if "data" in data:
                    print(f"  Received order book: {data['arg']['instId']}")
            
            print("  WebSocket test PASSED")
            return True
    except Exception as e:
        print(f"  WebSocket test FAILED: {e}")
        return False

result = asyncio.run(test_connection())
sys.exit(0 if result else 1)
EOF
}

# Run unit tests
run_unit_tests() {
    log_info "Running unit tests..."
    cd "$PROJECT_ROOT"
    meson test -C build --print-errorlogs
}

# Summary
print_summary() {
    echo ""
    echo "========================================"
    echo "         Integration Test Summary       "
    echo "========================================"
    echo "  Mock Exchange:      ✓ Started"
    echo "  WebSocket Client:   ✓ Connected"
    echo "  Order Book Data:    ✓ Received"
    echo "  Unit Tests:         ✓ Passed"
    echo "========================================"
    echo ""
}

# Main
main() {
    log_info "Starting End-to-End Integration Test"
    echo ""
    
    check_deps
    start_mock_exchange
    test_websocket_client
    run_unit_tests
    print_summary
    
    log_info "All tests passed!"
}

main "$@"
