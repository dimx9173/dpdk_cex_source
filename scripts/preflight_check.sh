#!/bin/bash
set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 1. Check Root Privilege
if [ "$EUID" -ne 0 ]; then
    log_error "This script must be run as root (or with sudo)."
    exit 1
fi

# 2. Check Dependencies
DEPENDENCIES=("ip" "modprobe" "meson" "ninja" "lspci")
for dep in "${DEPENDENCIES[@]}"; do
    if ! command -v "$dep" &>/dev/null; then
        log_error "Dependency not found: $dep"
        exit 1
    fi
done

# 3. Check Hugepages
HUGEPAGE_MOUNT="/dev/hugepages"
if ! mountpoint -q "$HUGEPAGE_MOUNT"; then
    log_warn "Hugepages not mounted at $HUGEPAGE_MOUNT"
    # Try to verify via /proc/mounts just in case
    if ! grep -q "hugetlbfs" /proc/mounts; then
        log_error "Hugepages (hugetlbfs) not detected in /proc/mounts. Please configure hugepages."
        exit 1
    else
        log_warn "Found hugetlbfs elsewhere: $(grep hugetlbfs /proc/mounts | awk '{print $2}')"
    fi
else
    log_info "Hugepages mounted at $HUGEPAGE_MOUNT"
fi

# Verify allocated pages
NR_HUGEPAGES=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages 2>/dev/null || echo 0)
if [ "$NR_HUGEPAGES" -lt 256 ]; then
    log_warn "Low hugepage count: $NR_HUGEPAGES. Attempting to allocate 1024..."
    echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages || true
    NR_HUGEPAGES_NEW=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages 2>/dev/null || echo 0)
    if [ "$NR_HUGEPAGES_NEW" -lt 256 ]; then
         log_error "Failed to allocate hugepages. Current: $NR_HUGEPAGES_NEW. Please allocate explicitly."
         exit 1
    else
         log_info "Allocated hugepages: $NR_HUGEPAGES_NEW"
    fi
else
    log_info "Hugepages available: $NR_HUGEPAGES"
fi

# 4. Check TUN Module
if [ ! -d "/sys/module/tun" ]; then
    log_warn "TUN module not loaded. Attempting to load..."
    if ! modprobe tun; then
        log_error "Failed to load 'tun' module."
        exit 1
    fi
    log_info "TUN module loaded."
else
    log_info "TUN module already loaded."
fi

# 5. Check Single-NIC Dependencies (if requested)
# Note: dpdk-devbind.py is usually in <dpdk_install_dir>/usertools/
# We check if it's in PATH or attempt to locate it.
if ! command -v dpdk-devbind.py &>/dev/null; then
    # Try common location on Ubuntu DPDK package
    if [ -f "/usr/bin/dpdk-devbind.py" ]; then
        log_info "Found dpdk-devbind.py at /usr/bin/dpdk-devbind.py"
    else
        log_warn "dpdk-devbind.py not found in PATH. Required for Single-NIC mode."
    fi
fi

# Check for vfio-pci module
if [ ! -d "/sys/module/vfio_pci" ]; then
    log_warn "vfio-pci module not loaded. Attempting to load..."
    if ! modprobe vfio-pci; then
        log_error "Failed to load 'vfio-pci' module."
        # Don't exit yet, might be using uio
    fi
else
    log_info "vfio-pci module loaded."
fi

# 6. Check Directory Structure
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

if [ ! -d "$PROJECT_ROOT/src" ]; then
  log_error "Source directory not found at $PROJECT_ROOT/src"
  exit 1
fi

log_info "Preflight check PASSED."
exit 0
