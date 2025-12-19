#!/bin/bash
# scripts/deploy_single_nic.sh
# "Suicide Script" for Single-NIC Deployment
# Handles atomic network migration to DPDK and auto-rollback.

# 1. Configuration & defaults
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
OVERRIDE_NIC=""
ROLLBACK_TIMEOUT=60
LOG_FILE="/tmp/hft_deploy_single.log"
STATE_FILE="/tmp/hft_nic_state.env"

# Redirect all output
touch "$LOG_FILE" 2>/dev/null || true
exec >> "$LOG_FILE" 2>&1

echo "=== Single-NIC Deployment Started at $(date) ==="

# Parse Args
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --nic) OVERRIDE_NIC="$2"; shift ;;
        --timeout) ROLLBACK_TIMEOUT="$2"; shift ;;
        *) echo "Unknown parameter: $1"; exit 1 ;;
    esac
    shift
done

# 2. NIC Detection
TARGET_NIC=""
if [ -n "$OVERRIDE_NIC" ]; then
    TARGET_NIC="$OVERRIDE_NIC"
else
    # Detect first interface matching eth* or ens* with an IP
    TARGET_NIC=$(ip -o -4 addr show | awk '{print $2}' | grep -E '^(eth|ens)' | head -n1)
    
    # Fallback: If no interface found, check if we are already in a DPDK state
    if [ -z "$TARGET_NIC" ] && [ -f "$STATE_FILE" ]; then
        echo "WARN: No active interface found. Checking previous state file..."
        source "$STATE_FILE"
        if [ -n "$ORIG_NIC" ]; then
            echo "WARN: Detected previous state for $ORIG_NIC. Assuming recovery/retry."
            TARGET_NIC="$ORIG_NIC"
            # If we are recovering, we might skip capture if variables are loaded
            RECOVERY_MODE=1
        fi
    fi
fi

if [ -z "$TARGET_NIC" ]; then
    echo "ERROR: Could not detect a valid network interface. Use --nic."
    exit 1
fi

echo "Target Interface: $TARGET_NIC"

# 2.5 Pre-deployment: Build (BEFORE network is lost!)
echo "=== Pre-deployment: Building ==="

# Kill any existing instances (exclude current script)
for pid in $(pgrep -f "hft-app" | grep -v $$); do kill -9 $pid 2>/dev/null || true; done
rm -f "$STATE_FILE"

# Build
BUILD_DIR="$PROJECT_ROOT/build"
if [ ! -d "$BUILD_DIR" ]; then
    meson setup "$BUILD_DIR" > "$BUILD_DIR/setup.log" 2>&1
fi
echo "Compiling... (Log: $BUILD_DIR/compile.log)"
meson compile -C "$BUILD_DIR" > "$BUILD_DIR/compile.log" 2>&1 || { cat "$BUILD_DIR/compile.log"; exit 1; }

echo "=== Pre-deployment complete ==="

# 3. Capture State
# 3. Capture State
if [ -z "$RECOVERY_MODE" ]; then
    echo "Capturing Network State..."
    ORIG_IP=$(ip -o -4 addr show "$TARGET_NIC" | head -n1 | awk '{print $4}') # CIDR format
    ORIG_GW=$(ip route show default | awk '/default/ {print $3}' | head -n1)
    ORIG_PCI=$(ethtool -i "$TARGET_NIC" | grep bus-info | awk '{print $2}')
    ORIG_DRIVER=$(ethtool -i "$TARGET_NIC" | grep driver | awk '{print $2}')
    ORIG_MAC=$(cat "/sys/class/net/$TARGET_NIC/address")

    if [ -z "$ORIG_PCI" ]; then
        # Try finding PCI via sysfs if ethtool fails (unlikely on VM but possible)
        # Assume standard bus-info is not reliably available via ethtool on all virtio
        # Try lspci mapping?
        # Simple workaround: read link
        if [ -L "/sys/class/net/$TARGET_NIC/device" ]; then
            ORIG_PCI=$(basename "$(readlink "/sys/class/net/$TARGET_NIC/device")")
        fi
    fi
else
    echo "Recovery Mode: Skipping state capture. Using cached values."
fi

if [ -z "$ORIG_IP" ] || [ -z "$ORIG_PCI" ]; then
    echo "ERROR: Failed to capture critical state (IP or PCI missing)."
    echo "IP: $ORIG_IP | PCI: $ORIG_PCI"
    exit 1
fi

# Save State for Rollback
cat <<EOF > "$STATE_FILE"
ORIG_NIC="$TARGET_NIC"
ORIG_IP="$ORIG_IP"
ORIG_GW="$ORIG_GW"
ORIG_PCI="$ORIG_PCI"
ORIG_DRIVER="$ORIG_DRIVER"
ORIG_MAC="$ORIG_MAC"
EOF

echo "State saved to $STATE_FILE"
echo "  PCI: $ORIG_PCI"
echo "  IP:  $ORIG_IP"
echo "  GW:  $ORIG_GW"

# 4. Preparing for Death (SSH Disconnect)
echo "WARNING: Network connection will be lost for up to $ROLLBACK_TIMEOUT seconds."
echo "Unbinding $TARGET_NIC from kernel..."

# Ensure VFIO is loaded and allows No-IOMMU (common in VMs)
if ! lsmod | grep -q vfio_pci; then
    modprobe vfio-pci || true
fi
# Enable unsafe No-IOMMU mode if file exists (for VMs without VT-d)
if [ -f "/sys/module/vfio/parameters/enable_unsafe_noiommu_mode" ]; then
    echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode || true
fi

# Unbind
# We use dpdk-devbind.py if available, or manual generic
# Unbind
# We use dpdk-devbind.py if available, or manual generic
if [ -z "$RECOVERY_MODE" ]; then
    if command -v dpdk-devbind.py >/dev/null; then
        # --force is REQUIRED to unbind an interface with an active route (SSH)
        dpdk-devbind.py --force -u "$ORIG_PCI"
        dpdk-devbind.py -b vfio-pci "$ORIG_PCI"
    else
        # Manual unbind not fully implemented here for safety, rely on deploy.sh or preflight
        # Assuming preflight checked devbind presence.
        echo "ERROR: dpdk-devbind.py missing at critical stage."
        exit 1
    fi
else
    echo "Recovery Mode: Skipping unbind/bind (assuming already bound)."
fi

# Note: SSH is DEAD here.
# Sleep bit to settle
sleep 2

# 5. Launch HFT App via deploy.sh
# 5. Launch HFT App via deploy.sh
echo "Launching deploy.sh..."

# KILL existing instances if recovering
if [ -n "$RECOVERY_MODE" ]; then
    echo "Killing existing hft-app instances..."
    pkill -9 -f "hft-app" || true
    sleep 2
fi

# We pass --ip "$ORIG_IP" to ensure tap0 takes over the original identity
# We MUST export the detected PCI address so deploy.sh uses it instead of default
export HFT_PHY_PCI="$ORIG_PCI"

# We must NOT pass --single-nic again to avoid recursion loop.
"$SCRIPT_DIR/deploy.sh" --ip "$ORIG_IP" > "${LOG_FILE}.deploy" 2>&1 &
DEPLOY_PID=$!

# Stream the log to stdout (background) so it shows up in nohup
tail -f "${LOG_FILE}.deploy" &
TAIL_PID=$!

# 6. Monitor Loop (The Race against Timeout)
START_TIME=$(date +%s)
SUCCESS=0

while [ $SUCCESS -eq 0 ]; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))

    # Check Timeout
    if [ "$ELAPSED" -ge "$ROLLBACK_TIMEOUT" ]; then
        echo "TIMEOUT: Deployment took longer than $ROLLBACK_TIMEOUT seconds."
        break
    fi

    # Check if deploy.sh died
    if ! kill -0 "$DEPLOY_PID" 2>/dev/null; then
        echo "Deploy script exited unexpectedly."
        break
    fi

    # Check for TAP interface AND IP address
    # We must wait for IP to be assigned by deploy.sh before adding routes
    if ip addr show tap0 | grep -q "inet "; then
        echo "DEBUG: tap0 has IP address. Proceeding to routing."
        # Check if TAP has IP? deploy.sh sets it.
        # If tap0 exists, we assume deploy.sh is succeeding.
        # But we need to ensure Routing is fixed.
        
        # 7. Migrate Gateway
        # deploy.sh sets IP on tap0.
        # We need to add default route back via tap0 IF gateway was present.
        if [ -n "$ORIG_GW" ]; then
             if ! ip route | grep -q default; then
                 echo "DEBUG: Restoring Default Gateway $ORIG_GW via tap0"
                 ip route add default via "$ORIG_GW" dev tap0 || echo "DEBUG: Failed to add route"
             fi
        else
            echo "DEBUG: No original gateway found? ORIG_GW is empty."
        fi
        
        # Debug Routing State
        echo "DEBUG: Current Routing Table:"
        ip route
        
        # 8. Verify External Connectivity (Ping Check)
        # Exception Path Verification: Ensure packets can go out via tap0
        echo "Verifying Internet Connectivity (ping 8.8.8.8)..."
        if ping -c 1 -W 1 8.8.8.8 >/dev/null 2>&1; then
             echo "SUCCESS: Internet reachable."
             
             # 9. Verify/Fix DNS (Critical for Single-NIC)
             # systemd-resolved uses 127.0.0.53 which fails after NIC unbind
             # We MUST override (not append) to ensure 8.8.8.8 is used
             echo "Verifying DNS..."
             if ! getent hosts google.com >/dev/null 2>&1; then
                 echo "WARN: DNS lookup failed. Overriding /etc/resolv.conf..."
                 echo "nameserver 8.8.8.8" > /etc/resolv.conf
                 if getent hosts google.com >/dev/null 2>&1; then
                     echo "SUCCESS: DNS restored."
                 else
                     echo "ERROR: Failed to restore DNS."
                 fi
             else
                 echo "SUCCESS: DNS is working."
             fi
             
             SUCCESS=1
             echo "SUCCESS: tap0 is up. Network migrated."
             break
        else
             echo "Waiting for internet connectivity..."
             # Don't break yet, keep waiting until timeout
        fi
    fi
    
    sleep 1
done

# Kill the tail process
kill "$TAIL_PID" 2>/dev/null || true

# 8. Success or Rollback
if [ $SUCCESS -eq 1 ]; then
    echo "Deployment Complete. SSH should be restored."
    # We stay alive to monitor? Or just exit?
    # Spec says monitor loop.
    # For now, simplistic: We exit, assuming sysadmin takes over.
    # OR: we could monitor hft-app and auto-rollback if it dies later.
    # Spec says "Monitor PID... on exit, trigger rollback."
    echo "Monitoring HFT App..."
    APP_PID=$(pgrep -f "hft-app" | head -n1)
    
    while kill -0 "$APP_PID" 2>/dev/null; do
        sleep 5
    done
    
    echo "HFT App died! Triggering Rollback..."
    ./scripts/nic_rollback.sh
else
    echo "Deployment FAILED. Rolling back..."
    ./scripts/nic_rollback.sh
fi
