#include "classifier.h"
#include "config/config.h"
#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

HftClassifier::HftClassifier(uint16_t target_port)
    : target_port_(target_port) {}

TrafficType HftClassifier::classify(const rte_mbuf *m) const {
  struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
  uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);

  // Debug: Print first few packets info (controlled by DEBUG_LOG_ENABLED)
  static int debug_count = 0;
  if (app_config.debug_log_enabled && debug_count < 50) {
    if (ether_type == RTE_ETHER_TYPE_IPV4) {
      // deferred
    } else if (ether_type != 0x0806) {
      printf("[Classifier] Non-IPv4: 0x%04x\n", ether_type);
      debug_count++;
    }
  }

  if (ether_type != RTE_ETHER_TYPE_IPV4) {
    if (app_config.debug_log_enabled && debug_count < 20 &&
        ether_type != 0x0806) { // Ignore ARP
      debug_count++;
      printf("[Classifier] Ignored Non-IPv4 Pkt: EtherType=0x%04x\n",
             ether_type);
    }
    return TRAFFIC_TYPE_STANDARD;
  }

  struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);

  if (app_config.debug_log_enabled && debug_count < 50) {
    printf("[Classifier] IPv4 Proto: %u\n", ip_hdr->next_proto_id);
    debug_count++;
  }

  if (ip_hdr->next_proto_id == IPPROTO_UDP) {
    // Current design uses TCP (WebSocket) for all MD.
    return TRAFFIC_TYPE_STANDARD;
  }

  if (ip_hdr->next_proto_id == IPPROTO_TCP) {
    // Correctly offset to TCP header
    // struct rte_tcp_hdr *tcp_hdr = (struct rte_tcp_hdr *)(ip_hdr + 1); // Only
    // for IHL=5 Better:
    struct rte_tcp_hdr *tcp_hdr =
        (struct rte_tcp_hdr *)((uint8_t *)ip_hdr +
                               ((ip_hdr->version_ihl & 0xf) * 4));

    uint16_t src_port = rte_be_to_cpu_16(tcp_hdr->src_port);
    uint16_t dst_port = rte_be_to_cpu_16(tcp_hdr->dst_port);

    if (app_config.debug_log_enabled && debug_count < 50) {
      printf("[Classifier] TCP: Src=%u Dst=%u\n", src_port, dst_port);
      debug_count++;
    }

    if (src_port == 8443 || dst_port == 8443) { // OKX
      if (app_config.debug_log_enabled && debug_count < 100)
        printf("[Classifier] Returning HFT for OKX\n");
      return TRAFFIC_TYPE_HFT;
    }
    if (src_port == 443 || dst_port == 443) { // Bybit
      if (app_config.debug_log_enabled && debug_count < 100)
        printf("[Classifier] Returning HFT for Bybit\n");
      return TRAFFIC_TYPE_HFT;
    }
  }

  return TRAFFIC_TYPE_STANDARD;
}
