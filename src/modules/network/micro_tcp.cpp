#include "micro_tcp.h"

#include "core/logging.h"
#include <iostream>

#include <rte_byteorder.h> // For rte_cpu_to_be_16/32 etc.
#include <rte_common.h>    // Common DPDK definitions
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h> // mbuf related functions
#include <rte_tcp.h>

#include <rte_cycles.h>
#include <rte_memcpy.h>
#include <rte_random.h>

#ifndef RTE_TCP_MIN_HDR_SIZE
#define RTE_TCP_MIN_HDR_SIZE 20
#endif

#ifndef RTE_TCP_OFFSET_UNIT
#define RTE_TCP_OFFSET_UNIT 4
#endif

// For pseudo-header checksum calculation
struct ipv4_psd_hdr {
  uint32_t src_addr;
  uint32_t dst_addr;
  uint8_t zero;
  uint8_t proto;
  uint16_t len;
} __attribute__((__packed__));

MicroTcp::MicroTcp(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip,
                   uint16_t dst_port, const rte_ether_addr &src_mac,
                   const rte_ether_addr &dst_mac, struct rte_mempool *mbuf_pool)
    : state_(CLOSED), src_ip_(src_ip), src_port_(src_port), dst_ip_(dst_ip),
      dst_port_(dst_port), src_mac_(src_mac), dst_mac_(dst_mac), snd_una_(0),
      snd_nxt_(0), rcv_nxt_(0), mbuf_pool_(mbuf_pool) {
  LOG_SYSTEM("MicroTcp constructor called by Strategy");
  iss_ = rte_rand(); // Initial Send Sequence number
  snd_nxt_ = iss_;

  // Initialize Cached Headers (Packet Template)
  // -----------------------------------------------------
  memset(cached_headers_, 0, sizeof(cached_headers_));

  // Ethernet Header
  rte_ether_hdr *eth_hdr = (rte_ether_hdr *)cached_headers_;
  rte_memcpy(&eth_hdr->src_addr, &src_mac_, sizeof(rte_ether_addr));
  rte_memcpy(&eth_hdr->dst_addr, &dst_mac_, sizeof(rte_ether_addr));
  eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

  // IPv4 Header
  rte_ipv4_hdr *ipv4_hdr = (rte_ipv4_hdr *)(eth_hdr + 1);
  ipv4_hdr->version_ihl = RTE_IPV4_VHL_DEF;
  ipv4_hdr->type_of_service = 0;
  ipv4_hdr->time_to_live = 64;
  ipv4_hdr->next_proto_id = IPPROTO_TCP;
  ipv4_hdr->src_addr = rte_cpu_to_be_32(src_ip_);
  ipv4_hdr->dst_addr = rte_cpu_to_be_32(dst_ip_);
  ipv4_hdr->fragment_offset = rte_cpu_to_be_16(RTE_IPV4_HDR_DF_FLAG);

  // TCP Header
  rte_tcp_hdr *tcp_hdr = (rte_tcp_hdr *)(ipv4_hdr + 1);
  tcp_hdr->src_port = rte_cpu_to_be_16(src_port_);
  tcp_hdr->dst_port = rte_cpu_to_be_16(dst_port_);
  tcp_hdr->data_off = ((sizeof(rte_tcp_hdr) / RTE_TCP_OFFSET_UNIT) << 4);
  tcp_hdr->rx_win = rte_cpu_to_be_16(65535);

  LOG_SYSTEM("MicroTcp initialized. Packet Template created. ISS: " << iss_);
}

rte_mbuf *MicroTcp::connect() {
  if (state_ != CLOSED) {
    LOG_SYSTEM("Error: Connection not in CLOSED state.");
    return nullptr;
  }

  state_ = SYN_SENT;
  LOG_SYSTEM("Sending SYN packet...");
  return create_tcp_packet(RTE_TCP_SYN_FLAG, nullptr, 0);
}

std::vector<rte_mbuf *> MicroTcp::process_rx(rte_mbuf *rx_mbuf) {
  std::vector<rte_mbuf *> tx_pkts;
  rte_ether_hdr *eth_hdr;
  rte_ipv4_hdr *ipv4_hdr;
  rte_tcp_hdr *tcp_hdr;

  parse_tcp_packet(rx_mbuf, eth_hdr, ipv4_hdr, tcp_hdr);

  if (!eth_hdr || !ipv4_hdr || !tcp_hdr) {
    rte_pktmbuf_free(rx_mbuf);
    return tx_pkts;
  }

  fprintf(
      stderr,
      "DEBUG: MicroTcp RX Packet: SrcIP=%u SrcPort=%u DstIP=%u DstPort=%u "
      "(Expected: SrcIP=%u SrcPort=%u DstIP=%u DstPort=%u)\n",
      rte_be_to_cpu_32(ipv4_hdr->src_addr), rte_be_to_cpu_16(tcp_hdr->src_port),
      rte_be_to_cpu_32(ipv4_hdr->dst_addr), rte_be_to_cpu_16(tcp_hdr->dst_port),
      dst_ip_, dst_port_, src_ip_, src_port_);

  // Filter packets not for this connection
  if (rte_be_to_cpu_32(ipv4_hdr->dst_addr) != src_ip_ ||
      rte_be_to_cpu_16(tcp_hdr->dst_port) != src_port_) {

    fprintf(stderr,
            "DEBUG: MicroTcp filtered out packet (Dst mismatch). DstIP=%u "
            "(Ref=%u) DstPort=%u (Ref=%u)\n",
            rte_be_to_cpu_32(ipv4_hdr->dst_addr), src_ip_,
            rte_be_to_cpu_16(tcp_hdr->dst_port), src_port_);

    // Not for us, or not for this specific connection
    rte_pktmbuf_free(rx_mbuf);
    return tx_pkts;
  }

  // Check if it's from the expected source IP and port
  if (rte_be_to_cpu_32(ipv4_hdr->src_addr) != dst_ip_ ||
      rte_be_to_cpu_16(tcp_hdr->src_port) != dst_port_) {

    fprintf(stderr,
            "DEBUG: MicroTcp filtered out packet (Src mismatch). SrcIP=%u "
            "(Ref=%u) SrcPort=%u (Ref=%u)\n",
            rte_be_to_cpu_32(ipv4_hdr->src_addr), dst_ip_,
            rte_be_to_cpu_16(tcp_hdr->src_port), dst_port_);

    // Not from the peer we are connected to/trying to connect to
    rte_pktmbuf_free(rx_mbuf);
    return tx_pkts;
  }

  // fprintf(stderr, "DEBUG: MicroTcp RX. Flags: %02x, Seq: %u, Ack: %u\n",
  // tcp_hdr->tcp_flags, rte_be_to_cpu_32(tcp_hdr->sent_seq),
  // rte_be_to_cpu_32(tcp_hdr->recv_ack));

  // Calculate TCP data length with safety checks
  uint16_t ip_len = rte_be_to_cpu_16(ipv4_hdr->total_length);
  uint16_t ip_hdr_len = (ipv4_hdr->ihl * 4);
  uint16_t tcp_hdr_len = (tcp_hdr->data_off >> 4) * 4;

  if (ip_len < (ip_hdr_len + tcp_hdr_len)) {
    LOG_SYSTEM("Malforming packet detected: total_length "
               << ip_len << " is less than headers "
               << (ip_hdr_len + tcp_hdr_len));
    rte_pktmbuf_free(rx_mbuf);
    return tx_pkts;
  }

  uint16_t tcp_data_len = ip_len - ip_hdr_len - tcp_hdr_len;

  // Update receive next (ACK number we expect to send)
  uint32_t received_seq = rte_be_to_cpu_32(tcp_hdr->sent_seq);
  uint32_t received_ack = rte_be_to_cpu_32(tcp_hdr->recv_ack);

  // State machine logic
  switch (state_) {
  case SYN_SENT:
    if ((tcp_hdr->tcp_flags & RTE_TCP_SYN_FLAG) &&
        (tcp_hdr->tcp_flags & RTE_TCP_ACK_FLAG)) {
      // Received SYN-ACK
      LOG_SYSTEM("DEBUG: RX SYN-ACK. rec_ack=" << received_ack
                                               << ", iss+1=" << (iss_ + 1));
      if (received_ack == (iss_ + 1)) { // ACK confirms our SYN
        rcv_nxt_ =
            received_seq + 1; // Expect next from server to be received_seq + 1
        snd_una_ = received_ack; // Our SYN is acknowledged

        state_ = ESTABLISHED;
        LOG_SYSTEM("DEBUG: State -> ESTABLISHED. Sending ACK.");
        tx_pkts.push_back(create_tcp_packet(RTE_TCP_ACK_FLAG, nullptr, 0));

        // Notify WebSocketClient via callback check?
        // No, WebSocketClient polls state via get_state()
      }
    }
    break;
  case ESTABLISHED: {
    // Handle incoming data
    if (tcp_data_len > 0) {
      if (received_seq == rcv_nxt_) { // In-order data
        const uint8_t *payload = rte_pktmbuf_mtod_offset(
            rx_mbuf, const uint8_t *,
            sizeof(rte_ether_hdr) + (ipv4_hdr->ihl * 4) +
                (tcp_hdr->data_off >> 4) * 4);
        // DEBUG: Print first few bytes of payload
        // LOG_TRADE("DEBUG MicroTcp: Extracted payload... (len=" <<
        // tcp_data_len << ")");
        // Limit RX buffer size to prevent OOM
        if (rx_buffer_.size() + tcp_data_len > MAX_RX_BUFFER_SIZE) {
          LOG_SYSTEM("WARNING: MicroTcp RX buffer full. Dropping data.");
          // For HFT, we might want to drop oldest or just stop.
          // Here we just limit the growth to prevent crash.
        } else {
          rx_buffer_.insert(rx_buffer_.end(), payload, payload + tcp_data_len);
        }
        rcv_nxt_ += tcp_data_len;
        LOG_TRADE("Received " << tcp_data_len
                              << " bytes of data. New rcv_nxt: " << rcv_nxt_);
        tx_pkts.push_back(
            create_tcp_packet(RTE_TCP_ACK_FLAG, nullptr, 0)); // ACK data
      } else if (received_seq < rcv_nxt_) {
        // Duplicate packet, re-ACK
        std::cout << "Received duplicate packet, re-ACKing." << std::endl;
        tx_pkts.push_back(create_tcp_packet(RTE_TCP_ACK_FLAG, nullptr, 0));
      } else {
        // Out-of-order packet. For simplicity, we drop for now and hope for
        // retransmission.
        std::cerr << "Received out-of-order packet. Dropping. Current rcv_nxt: "
                  << rcv_nxt_ << ", received_seq: " << received_seq
                  << std::endl;
      }
    }

    // Handle peer ACKs for our sent data
    if (received_ack > snd_una_) {
      snd_una_ = received_ack; // Data up to snd_una is acknowledged
      std::cout << "ACKed by peer. snd_una_ updated to: " << snd_una_
                << std::endl;
    }

    if (tcp_hdr->tcp_flags & RTE_TCP_FIN_FLAG) {
      rcv_nxt_ += 1;       // FIN consumes one sequence number
      state_ = FIN_WAIT_1; // Actually, would be CLOSE_WAIT if we are server.
                           // For client, assume FIN_WAIT_1 and then FIN_WAIT_2
                           // after our FIN. Simplified: just acknowledge and go
                           // to FIN_WAIT_2 for now.
      tx_pkts.push_back(
          create_tcp_packet(RTE_TCP_ACK_FLAG, nullptr, 0)); // ACK their FIN
      state_ = FIN_WAIT_2; // Go straight to FIN_WAIT_2 after ACKing their FIN.
      std::cout << "Received FIN. Sending ACK. State FIN_WAIT_2." << std::endl;
    }
    break;
  }
  case FIN_WAIT_2:
    // If we receive ACK for our FIN, or just more ACKs, that's fine.
    // If we initiated FIN, we'd be waiting for their FIN then ACK it.
    // For now, if we receive another FIN, we ACK it and go to TIME_WAIT.
    if (tcp_hdr->tcp_flags & RTE_TCP_FIN_FLAG) {
      rcv_nxt_ += 1;
      tx_pkts.push_back(create_tcp_packet(RTE_TCP_ACK_FLAG, nullptr, 0));
      state_ = TIME_WAIT;
      std::cout << "Received FIN again. Sending ACK. State TIME_WAIT."
                << std::endl;
    }
    break;
  default:
    std::cerr << "Received packet in unexpected state: " << state_ << std::endl;
    break;
  }

  rte_pktmbuf_free(rx_mbuf);
  return tx_pkts;
}

rte_mbuf *MicroTcp::send_data(const uint8_t *data, uint16_t len) {
  if (state_ != ESTABLISHED) {
    std::cerr << "Error: Cannot send data, connection not ESTABLISHED."
              << std::endl;
    return nullptr;
  }
  LOG_TRADE("Sending " << len << " bytes of data.");
  return create_tcp_packet(RTE_TCP_PSH_FLAG | RTE_TCP_ACK_FLAG, data, len);
}

rte_mbuf *MicroTcp::create_tcp_packet(uint8_t flags, const uint8_t *payload,
                                      uint16_t payload_len) {
  uint16_t total_len = sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) +
                       sizeof(rte_tcp_hdr) + payload_len;

  rte_mbuf *m = rte_pktmbuf_alloc(mbuf_pool_);
  if (m == nullptr) {
    std::cerr << "Failed to allocate mbuf for TCP packet." << std::endl;
    return nullptr;
  }
  m->data_len = total_len;
  m->pkt_len = total_len;

  m->data_len = total_len;
  m->pkt_len = total_len;

  // Copy Pre-calculated Headers (Template) - ZERO COPY OPTIMIZATION PART 1
  uint8_t *packet_data = rte_pktmbuf_mtod(m, uint8_t *);
  const size_t header_len =
      sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_tcp_hdr);
  rte_memcpy(packet_data, cached_headers_, header_len);

  // Get Pointers to Headers in mbuf (to update dynamic fields)
  rte_ether_hdr *eth_hdr = (rte_ether_hdr *)packet_data;
  rte_ipv4_hdr *ipv4_hdr = (rte_ipv4_hdr *)(eth_hdr + 1);
  rte_tcp_hdr *tcp_hdr = (rte_tcp_hdr *)(ipv4_hdr + 1);

  // IPv4 Dynamic Fields
  ipv4_hdr->total_length = rte_cpu_to_be_16(total_len - sizeof(rte_ether_hdr));
  ipv4_hdr->hdr_checksum = 0;
  ipv4_hdr->hdr_checksum = calculate_ipv4_checksum(ipv4_hdr);

  // TCP Dynamic Fields
  tcp_hdr->tcp_flags = flags;
  tcp_hdr->cksum = 0;

  // Set sequence numbers
  if (flags & RTE_TCP_SYN_FLAG) {
    tcp_hdr->sent_seq = rte_cpu_to_be_32(iss_);
    tcp_hdr->recv_ack = rte_cpu_to_be_32(0); // No ACK for SYN
  } else {
    tcp_hdr->sent_seq = rte_cpu_to_be_32(snd_nxt_);
    tcp_hdr->recv_ack =
        rte_cpu_to_be_32(rcv_nxt_); // ACK previous received data
  }

  // Copy payload if any
  if (payload_len > 0 && payload != nullptr) {
    uint8_t *packet_payload = (uint8_t *)(tcp_hdr + 1);
    rte_memcpy(packet_payload, payload, payload_len);
    update_snd_nxt(payload_len); // Data consumes sequence numbers
  } else if (flags & RTE_TCP_SYN_FLAG || flags & RTE_TCP_FIN_FLAG) {
    update_snd_nxt(1); // SYN and FIN flags consume one sequence number
  }

  // Calculate TCP checksum
  tcp_hdr->cksum =
      calculate_tcp_checksum(ipv4_hdr, tcp_hdr, payload, payload_len);

  return m;
}

void MicroTcp::parse_tcp_packet(rte_mbuf *mbuf, rte_ether_hdr *&eth_hdr,
                                rte_ipv4_hdr *&ipv4_hdr,
                                rte_tcp_hdr *&tcp_hdr) {
  eth_hdr = nullptr;
  ipv4_hdr = nullptr;
  tcp_hdr = nullptr;

  if (unlikely(mbuf == nullptr))
    return;

  // 1. Ethernet Header bounds check
  if (mbuf->data_len < sizeof(rte_ether_hdr))
    return;
  eth_hdr = rte_pktmbuf_mtod(mbuf, rte_ether_hdr *);

  if (rte_be_to_cpu_16(eth_hdr->ether_type) != RTE_ETHER_TYPE_IPV4) {
    eth_hdr = nullptr;
    return;
  }

  // 2. IPv4 Header bounds check
  size_t eth_len = sizeof(rte_ether_hdr);
  if (mbuf->data_len < eth_len + sizeof(rte_ipv4_hdr)) {
    eth_hdr = nullptr;
    return;
  }
  ipv4_hdr = (rte_ipv4_hdr *)(eth_hdr + 1);

  if (ipv4_hdr->next_proto_id != IPPROTO_TCP) {
    ipv4_hdr = nullptr;
    eth_hdr = nullptr;
    return;
  }

  // 3. TCP Header bounds check
  uint16_t ihl = ipv4_hdr->ihl * 4;
  if (ihl < sizeof(rte_ipv4_hdr) ||
      mbuf->data_len < eth_len + ihl + sizeof(rte_tcp_hdr)) {
    ipv4_hdr = nullptr;
    eth_hdr = nullptr;
    return;
  }
  tcp_hdr = (rte_tcp_hdr *)((uint8_t *)ipv4_hdr + ihl);

  // 4. Validate TCP data offset
  uint16_t tcp_off = (tcp_hdr->data_off >> 4) * 4;
  if (tcp_off < sizeof(rte_tcp_hdr) ||
      mbuf->data_len < eth_len + ihl + tcp_off) {
    tcp_hdr = nullptr;
    ipv4_hdr = nullptr;
    eth_hdr = nullptr;
    return;
  }
}

// Use DPDK's built-in checksum functions
uint16_t MicroTcp::calculate_ipv4_checksum(rte_ipv4_hdr *ipv4_hdr) {
  ipv4_hdr->hdr_checksum = 0;
  return rte_ipv4_cksum(ipv4_hdr);
}

uint16_t MicroTcp::calculate_tcp_checksum(rte_ipv4_hdr *ipv4_hdr,
                                          rte_tcp_hdr *tcp_hdr,
                                          const uint8_t *payload,
                                          uint16_t payload_len) {
  tcp_hdr->cksum = 0;
  // rte_ipv4_udptcp_cksum requires the IP header (for pseudo-header) and the
  // TCP header (extended with payload if needed).
  // But rte_ipv4_udptcp_cksum assumes the L4 header is contiguous with the
  // payload if passed as a single pointer. Since we constructed the mbuf such
  // that payload follows TCP header, we can just pass tcp_hdr. HOWEVER,
  // rte_ipv4_udptcp_cksum expects the FULL L4 header + payload.

  // Actually, DPDK's rte_ipv4_udptcp_cksum simplifies this.
  // It takes the IPv4 header and the L4 header pointer.
  // It calculates the Pseudo Header checksum + the L4 Header checksum.
  // BUT it relies on the mbuf if we want to handle non-contiguous payload?
  // No, it takes raw void *.

  // Let's use the explicit raw calculation using rte_ipv4_udptcp_cksum which
  // covers the pseudo-header and then manually adding the payload checksum if
  // needed, OR just trust that our mbuf is contiguous (which it is, we
  // allocated it that way in create_tcp_packet).

  return rte_ipv4_udptcp_cksum(ipv4_hdr, tcp_hdr);
}

void MicroTcp::update_snd_nxt(uint32_t sent_len) {
  snd_nxt_ += sent_len;
  // LOG_TRADE("snd_nxt_ updated to: " << snd_nxt_);
}

std::vector<uint8_t> MicroTcp::extract_rx_data() {
  std::vector<uint8_t> data(rx_buffer_.begin(), rx_buffer_.end());
  rx_buffer_.clear();
  return data;
}
