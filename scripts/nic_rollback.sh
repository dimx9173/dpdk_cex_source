#!/bin/bash
# scripts/nic_rollback.sh
# Emergency Rollback Script for Single-NIC Deployment
# Restores NIC to Kernel Control and connectivity.

set -e

# Log / Output
LOG_FILE="/tmp/hft_rollback.log"
touch "$LOG_FILE" 2>/dev/null || true
exec >> "$LOG_FILE" 2>&1

echo "=== NIC Rollback Initiated at $(date) ==="

# 1. Load Saved State
STATE_FILE="/tmp/hft_nic_state.env"
if [ -f "$STATE_FILE" ]; then
    source "$STATE_FILE"
    echo "Loaded state for Interface: $ORIG_NIC"
else
    echo "ERROR: State file $STATE_FILE not found. Cannot rollback automatically."
    echo "You may need to manually bind your NIC to virtio-net/vmxnet3."
    exit 1
fi

# 2. Kill Application
echo "Stopping HFT Application..."
pkill -9 -f "hft-app" || true
# Also kill deploy script if still running
pkill -f "deploy_single_nic.sh" || true 

# 3. Unbind from DPDK (vfio-pci)
echo "Unbinding $ORIG_PCI from DPDK..."
if command -v dpdk-devbind.py >/dev/null; then
    dpdk-devbind.py -u "$ORIG_PCI" || true
else
    # Fallback to sysfs
    echo "$ORIG_PCI" > /sys/bus/pci/drivers/vfio-pci/unbind || true
fi

# 4. Bind to Original Kernel Driver
echo "Binding $ORIG_PCI to $ORIG_DRIVER..."
if command -v dpdk-devbind.py >/dev/null; then
    dpdk-devbind.py -b "$ORIG_DRIVER" "$ORIG_PCI"
else
    # Fallback/Manual
    # modprobe virtio_net # Ensure driver loaded
    echo "$ORIG_PCI" > "/sys/bus/pci/drivers/$ORIG_DRIVER/bind"
fi

# 5. Restore Network Config
echo "Restoring Network Configuration..."

# Detect new interface name (Kernel might have renamed it, e.g., ens160 -> eth0)
NEW_NIC=""
if [ -d "/sys/bus/pci/devices/$ORIG_PCI/net" ]; then
    NEW_NIC=$(ls "/sys/bus/pci/devices/$ORIG_PCI/net" | head -n1)
fi

if [ -z "$NEW_NIC" ]; then
    echo "Warning: Could not detect interface name from sysfs. Defaulting to $ORIG_NIC"
    NEW_NIC="$ORIG_NIC"
else
    echo "Detected interface name: $NEW_NIC (was $ORIG_NIC)"
fi

ip link set "$NEW_NIC" up
ip addr add "$ORIG_IP" dev "$NEW_NIC" || echo "IP may already exist"

# Wait for link to be ready before adding route
sleep 2

if [ -n "$ORIG_GW" ]; then
    ip route add default via "$ORIG_GW" dev "$NEW_NIC" || echo "Gateway may already exist"
fi

echo "=== Rollback Complete ==="
echo "Connectivity should be restored via $NEW_NIC ($ORIG_IP)."

