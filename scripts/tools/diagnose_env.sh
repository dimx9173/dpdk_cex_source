#!/bin/bash
# scripts/diagnose_env.sh

echo "=== LSPCI ==="
lspci -nn

echo "=== DPDK Status ==="
if command -v dpdk-devbind.py &>/dev/null; then
    dpdk-devbind.py --status
else
    echo "dpdk-devbind.py not found in PATH"
fi

echo "=== Hugepages ==="
grep Huge /proc/meminfo

echo "=== Modules ==="
lsmod | grep -E "uio|vfio|igb_uio|rte_kni|tun|vhost"

echo "=== Vhost Net ==="
ls -l /dev/vhost-net
