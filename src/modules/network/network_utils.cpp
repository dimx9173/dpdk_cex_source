/**
 * @file network_utils.cpp
 * @brief Implementation of network utility functions
 */

#include "network_utils.h"

#include <arpa/inet.h>
#include <cstring>
#include <fstream>
#include <ifaddrs.h>
#include <iostream>
#include <netdb.h>
#include <sstream>

#include <rte_ethdev.h>

namespace aero {

std::optional<uint32_t>
NetworkUtils::resolve_hostname(const std::string &hostname) {
  struct addrinfo hints{};
  struct addrinfo *result = nullptr;

  hints.ai_family = AF_INET; // IPv4 only
  hints.ai_socktype = SOCK_STREAM;

  int ret = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
  if (ret != 0) {
    std::cerr << "DNS resolution failed for " << hostname << ": "
              << gai_strerror(ret) << std::endl;
    return std::nullopt;
  }

  // Get first IPv4 address
  for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
    if (rp->ai_family == AF_INET) {
      struct sockaddr_in *addr = (struct sockaddr_in *)rp->ai_addr;
      uint32_t ip = ntohl(addr->sin_addr.s_addr);
      freeaddrinfo(result);
      std::cout << "Resolved " << hostname << " to " << ip_to_string(ip)
                << std::endl;
      return ip;
    }
  }

  freeaddrinfo(result);
  return std::nullopt;
}

std::optional<uint32_t> NetworkUtils::get_gateway_ip() {
  // Parse /proc/net/route to find default gateway
  std::ifstream route_file("/proc/net/route");
  if (!route_file.is_open()) {
    std::cerr << "Failed to open /proc/net/route" << std::endl;
    return std::nullopt;
  }

  std::string line;
  std::getline(route_file, line); // Skip header

  while (std::getline(route_file, line)) {
    std::istringstream iss(line);
    std::string iface, dest_hex, gateway_hex;
    iss >> iface >> dest_hex >> gateway_hex;

    // Default route has destination 00000000
    if (dest_hex == "00000000") {
      uint32_t gateway_net = std::stoul(gateway_hex, nullptr, 16);
      // /proc/net/route stores in network byte order
      uint32_t gateway_host = ntohl(gateway_net);
      std::cout << "Default gateway: " << ip_to_string(gateway_host) << " on "
                << iface << std::endl;
      return gateway_host;
    }
  }

  std::cerr << "No default gateway found in /proc/net/route" << std::endl;
  return std::nullopt;
}

bool NetworkUtils::lookup_arp(uint32_t ip_addr, rte_ether_addr &out_mac) {
  // Parse /proc/net/arp
  std::ifstream arp_file("/proc/net/arp");
  if (!arp_file.is_open()) {
    std::cerr << "Failed to open /proc/net/arp" << std::endl;
    return false;
  }

  std::string target_ip = ip_to_string(ip_addr);

  std::string line;
  std::getline(arp_file, line); // Skip header

  while (std::getline(arp_file, line)) {
    std::istringstream iss(line);
    std::string ip_str, hw_type, flags, mac_str;
    iss >> ip_str >> hw_type >> flags >> mac_str;

    if (ip_str == target_ip) {
      // Parse MAC address (format: xx:xx:xx:xx:xx:xx)
      unsigned int mac_bytes[6];
      if (sscanf(mac_str.c_str(), "%x:%x:%x:%x:%x:%x", &mac_bytes[0],
                 &mac_bytes[1], &mac_bytes[2], &mac_bytes[3], &mac_bytes[4],
                 &mac_bytes[5]) == 6) {
        for (int i = 0; i < 6; i++) {
          out_mac.addr_bytes[i] = static_cast<uint8_t>(mac_bytes[i]);
        }
        std::cout << "ARP lookup: " << ip_str << " -> "
                  << mac_to_string(out_mac) << std::endl;
        return true;
      }
    }
  }

  std::cerr << "ARP entry not found for " << target_ip << std::endl;
  return false;
}

bool NetworkUtils::get_gateway_mac(rte_ether_addr &out_mac) {
  auto gateway_ip = get_gateway_ip();
  if (!gateway_ip) {
    return false;
  }
  return lookup_arp(*gateway_ip, out_mac);
}

std::string NetworkUtils::ip_to_string(uint32_t ip) {
  char buf[INET_ADDRSTRLEN];
  struct in_addr addr;
  addr.s_addr = htonl(ip);
  inet_ntop(AF_INET, &addr, buf, sizeof(buf));
  return std::string(buf);
}

std::string NetworkUtils::mac_to_string(const rte_ether_addr &mac) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac.addr_bytes[0],
           mac.addr_bytes[1], mac.addr_bytes[2], mac.addr_bytes[3],
           mac.addr_bytes[4], mac.addr_bytes[5]);
  return std::string(buf);
}

bool NetworkUtils::get_nic_mac(uint16_t port_id, rte_ether_addr &out_mac) {
  int ret = rte_eth_macaddr_get(port_id, &out_mac);
  if (ret != 0) {
    std::cerr << "Failed to get MAC addr for port " << port_id << ": " << ret
              << std::endl;
    return false;
  }
  std::cout << "NIC MAC (port " << port_id << "): " << mac_to_string(out_mac)
            << std::endl;
  return true;
}

std::optional<uint32_t>
NetworkUtils::get_interface_ip(const std::string &iface_name) {
  struct ifaddrs *ifaddr = nullptr;
  if (getifaddrs(&ifaddr) == -1) {
    std::cerr << "getifaddrs() failed" << std::endl;
    return std::nullopt;
  }

  std::optional<uint32_t> result = std::nullopt;
  for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr)
      continue;
    if (ifa->ifa_addr->sa_family != AF_INET)
      continue;
    if (iface_name != ifa->ifa_name)
      continue;

    struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
    uint32_t ip = ntohl(addr->sin_addr.s_addr);
    std::cout << "Interface " << iface_name << " IP: " << ip_to_string(ip)
              << std::endl;
    result = ip;
    break;
  }

  freeifaddrs(ifaddr);
  if (!result) {
    std::cerr << "No IPv4 address found for interface: " << iface_name
              << std::endl;
  }
  return result;
}

std::optional<uint32_t>
NetworkUtils::get_source_ip(const std::string &tap_iface) {
  // Priority 1: Check SRC_IP environment variable
  const char *src_ip_env = getenv("SRC_IP");
  if (src_ip_env != nullptr && strlen(src_ip_env) > 0) {
    struct in_addr addr;
    if (inet_pton(AF_INET, src_ip_env, &addr) == 1) {
      uint32_t ip = ntohl(addr.s_addr);
      std::cout << "Source IP from SRC_IP env: " << ip_to_string(ip)
                << std::endl;
      return ip;
    } else {
      std::cerr << "Invalid SRC_IP format: " << src_ip_env << std::endl;
    }
  }

  // Priority 2: Auto-detect from TAP interface
  auto tap_ip = get_interface_ip(tap_iface);
  if (tap_ip) {
    return tap_ip;
  }

  // Priority 3: Return nullopt (caller should handle error)
  std::cerr << "No source IP available. Set SRC_IP env var or configure "
            << tap_iface << std::endl;
  return std::nullopt;
}

} // namespace aero
