#!/bin/bash
set -e

# 0. Environment Setup
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
APP_BIN="$BUILD_DIR/src/hft-app"

# Default Configuration
HFT_EAL_CORES="0-1"
HFT_TAP_IP="192.168.100.1/24"
HFT_TAP_DEVICE="tap0"
HFT_FILE_PREFIX="hft_"
# Default PCI - Change this or pass via environment
if [ -z "$HFT_PHY_PCI" ]; then
    HFT_PHY_PCI="0000:18:00.0" 
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Cleanup Handler
cleanup() {
    log_info "Creating shutdown signal..."
    if [ -n "$APP_PID" ]; then
        kill -SIGINT "$APP_PID" 2>/dev/null || true
        wait "$APP_PID" 2>/dev/null || true
    fi
    
    # Optional: Remove TAP device if we want a clean slate (optional)
    # ip link delete $HFT_TAP_DEVICE 2>/dev/null || true
    log_info "Cleanup complete."
}
trap cleanup EXIT

# Parse Arguments
SINGLE_NIC_MODE=0
SINGLE_NIC_ARGS=()

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --single-nic) SINGLE_NIC_MODE=1 ;;
        --nic|--timeout) SINGLE_NIC_ARGS+=("$1" "$2"); shift ;;
        --ip) HFT_TAP_IP="$2"; shift ;;
        *) echo "Unknown parameter: $1"; exit 1 ;;
    esac
    shift
done

# Delegate to Single-NIC Script if requested
if [ "$SINGLE_NIC_MODE" -eq 1 ]; then
    log_info "Delegating to Single-NIC Deployment Script..."
    exec "$SCRIPT_DIR/deploy_single_nic.sh" "${SINGLE_NIC_ARGS[@]}"
fi

# Load Secrets
if [ -f "$PROJECT_ROOT/.env" ]; then
    log_info "Loading environment variables from .env"
    set -a
    source "$PROJECT_ROOT/.env"
    set +a
fi


# 1. Kill existing hft-app instances (not deploy scripts to avoid self-kill)
log_info "Cleaning up existing instances..."
for pid in $(pgrep -f "hft-app" 2>/dev/null); do kill -9 $pid 2>/dev/null || true; done
rm -f /tmp/hft_nic_state.env

# 2. Build/Rebuild
log_info "Building project..."
cd "$PROJECT_ROOT"
if [ ! -d "$BUILD_DIR" ]; then
    meson setup "$BUILD_DIR" > "$BUILD_DIR/setup.log" 2>&1
fi
log_info "Compiling... (Log: $BUILD_DIR/compile.log)"
meson compile -C "$BUILD_DIR" > "$BUILD_DIR/compile.log" 2>&1 || { cat "$BUILD_DIR/compile.log"; exit 1; }

# 3. Preflight Check
log_info "Running Preflight Checks..."
if ! "$SCRIPT_DIR/preflight_check.sh"; then
    log_error "Preflight check failed. Aborting."
    exit 1
fi

# 2. Build Verification
log_info "Verifying Build..."
if ! command -v ninja &>/dev/null; then
     log_error "ninja not found. Cannot build."
     exit 1
fi
# Always ensure build is up to date (incremental)
meson compile -C "$BUILD_DIR" >> "$BUILD_DIR/compile.log" 2>&1 || { cat "$BUILD_DIR/compile.log"; exit 1; }

# Construct EAL Arguments
EAL_ARGS=(
    "-l" "$HFT_EAL_CORES"
    "--file-prefix=$HFT_FILE_PREFIX"
    "--proc-type=auto"
)

# Exception Path (Virtio-user)
# In recent DPDK, 'path' arg is sometimes mandatory for vhost-kernel backend
EAL_ARGS+=("--vdev=net_virtio_user0,iface=$HFT_TAP_DEVICE,path=/dev/vhost-net")

# Physical Port or Mock
# Physical Port or Mock
if [ "$HFT_MOCK_PHY" == "1" ]; then
    log_warn "Using MOCK PHYSICAL DEVICE (net_tap)"
    # Use net_tap as 'physical' port for testing
    EAL_ARGS+=("--vdev=net_tap0,iface=mockphy0")
else
    # Verify PCI address exists
    if ! lspci -D 2>/dev/null | grep -q "$HFT_PHY_PCI"; then
        log_error "PCI Device $HFT_PHY_PCI not found."
        log_warn "If running in VM, use HFT_MOCK_PHY=1 ./scripts/deploy.sh"
        exit 1
    fi
    # Real PCI Device
    EAL_ARGS+=("-a" "$HFT_PHY_PCI")
fi

# 3. Launch Application
log_info "Launching HFT Gateway..."
log_info "  Cores: $HFT_EAL_CORES"
log_info "  TAP: $HFT_TAP_DEVICE"

# Cleanup previous TAP if exists to ensure clean state
if ip link show "$HFT_TAP_DEVICE" &>/dev/null; then
    log_warn "Removing stale $HFT_TAP_DEVICE..."
    sudo ip link delete "$HFT_TAP_DEVICE" || true
fi

# Fix DNS before launching app (critical for single-NIC deployments)
# systemd-resolved (127.0.0.53) fails after NIC is unbound from kernel
log_info "Ensuring DNS is configured..."
echo "nameserver 8.8.8.8" | sudo tee /etc/resolv.conf >/dev/null

# Note: We run in background to allow script to continue and configure TAP
sudo -E "$APP_BIN" "${EAL_ARGS[@]}" &
APP_PID=$!

# 4. Wait for TAP Device
log_info "Waiting for $HFT_TAP_DEVICE to be created..."
MAX_RETRIES=50 # 5 seconds
found=0
for ((i=0; i<MAX_RETRIES; i++)); do
    # Check if process died
    if ! kill -0 "$APP_PID" 2>/dev/null; then
        log_error "Application process $APP_PID died unexpectedly."
        wait "$APP_PID"
        exit 1
    fi

    if ip link show "$HFT_TAP_DEVICE" &>/dev/null; then
        found=1
        break
    fi
    sleep 0.1
done

if [ $found -eq 0 ]; then
    log_error "Timeout waiting for TAP device. Application may have failed to start."
    wait "$APP_PID"
    exit 1
fi

# 5. Configure TAP Interface
log_info "Configuring TAP Interface ($HFT_TAP_DEVICE)..."
sudo ip addr add "$HFT_TAP_IP" dev "$HFT_TAP_DEVICE" || true # Ignore if already assigned
sudo ip link set "$HFT_TAP_DEVICE" up

log_info "Deployment Successful!"
log_info "Gateway is running with PID $APP_PID"
log_info "TAP Interface: $HFT_TAP_IP"
log_info "Press Ctrl+C to stop."

# Wait for application to exit
wait "$APP_PID"
