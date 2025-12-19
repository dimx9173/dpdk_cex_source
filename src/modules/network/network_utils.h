/**
 * @file network_utils.h
 * @brief Utility functions for network resolution (DNS, ARP)
 *
 * Provides automatic hostname resolution and gateway MAC detection
 * to simplify network configuration.
 */

#ifndef _NETWORK_UTILS_H_
#define _NETWORK_UTILS_H_

#include <cstdint>
#include <optional>
#include <string>

#include <rte_ether.h>

namespace aero {

/**
 * @brief Network utility functions for automatic configuration
 */
class NetworkUtils {
public:
  /**
   * @brief Resolve a hostname to an IPv4 address
   * @param hostname The hostname to resolve (e.g., "ws.okx.com")
   * @return IPv4 address in host byte order, or std::nullopt on failure
   */
  static std::optional<uint32_t> resolve_hostname(const std::string &hostname);

  /**
   * @brief Get the default gateway's MAC address from the system ARP table
   * @param out_mac Output MAC address
   * @return true on success, false if gateway MAC not found
   *
   * This function reads /proc/net/route to find the default gateway IP,
   * then reads /proc/net/arp to find its MAC address.
   */
  static bool get_gateway_mac(rte_ether_addr &out_mac);

  /**
   * @brief Get the default gateway's IP address
   * @return Gateway IP in host byte order, or std::nullopt if not found
   */
  static std::optional<uint32_t> get_gateway_ip();

  /**
   * @brief Look up a MAC address from the system ARP table for a given IP
   * @param ip_addr IP address in host byte order
   * @param out_mac Output MAC address
   * @return true if found, false otherwise
   */
  static bool lookup_arp(uint32_t ip_addr, rte_ether_addr &out_mac);

  /**
   * @brief Convert IP (host byte order) to dotted decimal string
   */
  static std::string ip_to_string(uint32_t ip);

  /**
   * @brief Convert MAC address to string
   */
  static std::string mac_to_string(const rte_ether_addr &mac);

  /**
   * @brief Get MAC address from a DPDK NIC port
   * @param port_id DPDK port ID
   * @param out_mac Output MAC address
   * @return true on success, false on failure
   */
  static bool get_nic_mac(uint16_t port_id, rte_ether_addr &out_mac);

  /**
   * @brief Get IPv4 address of a network interface
   * @param iface_name Interface name (e.g., "tap0", "eth0")
   * @return IPv4 address in host byte order, or std::nullopt if not found
   *
   * Uses getifaddrs() to query the system for interface addresses.
   */
  static std::optional<uint32_t>
  get_interface_ip(const std::string &iface_name);

  /**
   * @brief Get source IP with priority: env var > TAP interface > error
   * @param tap_iface TAP interface name (default: "tap0")
   * @return IPv4 address in host byte order, or std::nullopt if not available
   *
   * Priority:
   * 1. SRC_IP environment variable (if set)
   * 2. TAP interface IP auto-detection
   * 3. std::nullopt (caller should handle error)
   */
  static std::optional<uint32_t>
  get_source_ip(const std::string &tap_iface = "tap0");
};

} // namespace aero

#endif // _NETWORK_UTILS_H_
