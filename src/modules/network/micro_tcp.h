#ifndef _MICRO_TCP_H_
#define _MICRO_TCP_H_

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_tcp.h>

#include "proto.h" // For websocket_hdr_t, etc.

class MicroTcp {
public:
  enum TcpState {
    CLOSED,
    SYN_SENT,
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    TIME_WAIT
  };

  MicroTcp(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip,
           uint16_t dst_port, const rte_ether_addr &src_mac,
           const rte_ether_addr &dst_mac, struct rte_mempool *mbuf_pool);

  TcpState get_state() const { return state_; }

  // Initiate a connection
  rte_mbuf *connect();

  // Process an incoming packet
  std::vector<rte_mbuf *> process_rx(rte_mbuf *rx_mbuf);

  // Send data over the TCP connection
  rte_mbuf *send_data(const uint8_t *data, uint16_t len);

  // Extract buffered RX data (consumes buffer)
  std::vector<uint8_t> extract_rx_data();

  static constexpr size_t MAX_RX_BUFFER_SIZE = 10 * 1024 * 1024; // 10MB limit

private:
  TcpState state_;

  // Connection details
  uint32_t src_ip_;
  uint16_t src_port_;
  uint32_t dst_ip_;
  uint16_t dst_port_;
  rte_ether_addr src_mac_;
  rte_ether_addr dst_mac_;

  // Sequence numbers
  uint32_t iss_;     // Initial Send Sequence
  uint32_t snd_una_; // Send Unacknowledged
  uint32_t snd_nxt_; // Send Next
  uint32_t rcv_nxt_; // Receive Next

  // Mbuf pool
  struct rte_mempool *mbuf_pool_;

  // Pre-calculated header template (Eth + IPv4 + TCP)
  // Max size estimated: 14 + 20 + 20 = 54 bytes
  uint8_t cached_headers_[64];

  // Internal buffer for received application data
  std::deque<uint8_t> rx_buffer_;

  // Helper functions
  rte_mbuf *create_tcp_packet(uint8_t flags, const uint8_t *payload,
                              uint16_t payload_len);
  void parse_tcp_packet(rte_mbuf *mbuf, rte_ether_hdr *&eth_hdr,
                        rte_ipv4_hdr *&ipv4_hdr, rte_tcp_hdr *&tcp_hdr);
  uint16_t calculate_ipv4_checksum(rte_ipv4_hdr *ipv4_hdr);
  uint16_t calculate_tcp_checksum(rte_ipv4_hdr *ipv4_hdr, rte_tcp_hdr *tcp_hdr,
                                  const uint8_t *payload, uint16_t payload_len);

  // Update TCP sequence numbers after sending/receiving
  void update_snd_nxt(uint32_t sent_len);
  void update_rcv_nxt(uint32_t received_len);
};

#endif // _MICRO_TCP_H_