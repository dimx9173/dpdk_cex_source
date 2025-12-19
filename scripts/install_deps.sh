#!/bin/bash
# Script to install build dependencies for HFT Gateway (DPDK)

set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root (sudo)."
    exit 1
fi

if [ -f /etc/debian_version ]; then
    echo "Detected Debian/Ubuntu system."
    apt-get update
    apt-get install -y build-essential meson ninja-build pkg-config libdpdk-dev dpdk
elif [ -f /etc/redhat-release ]; then
    echo "Detected RHEL/CentOS/Fedora system."
    dnf install -y @development-tools meson ninja-build pkgconf-pkg-config dpdk-devel
else
    echo "Unsupported OS. Please install the following manually:"
    echo "- meson"
    echo "- ninja"
    echo "- pkg-config"
    echo "- libdpdk-dev (>= 23.11)"
    exit 1
fi

echo "Dependencies installed successfully."
echo "Please verify installation by running: pkg-config --modversion libdpdk"
