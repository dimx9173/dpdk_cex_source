#include "parser.h"
#include "protocol.h"
#include <rte_ether.h> // For struct ether_hdr
#include <rte_ip.h>    // For struct ipv4_hdr
#include <rte_udp.h>   // For struct udp_hdr
#include <rte_common.h> // For common DPDK definitions (e.g. RTE_ETHER_TYPE_IPV4)

#define RTE_ETHER_TYPE_IPV4 0x0800 // Workaround for compilation issue

// DPDK includes for mbuf manipulation
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>

// For debugging
#include <stdio.h>

/**
 * @brief Parses an rte_mbuf for market data.
 * @param m The rte_mbuf to parse.
 * @return ParseResult containing validity and a pointer to the MdBookUpdate if valid.
 */
MdParser::ParseResult MdParser::parse(const rte_mbuf* m) {
    ParseResult result = {.valid = false, .update = nullptr};

    if (m == nullptr) {
        return result;
    }

    // Pointers to the start of various headers within the mbuf data
    const struct rte_ether_hdr *eth_hdr;
    const struct rte_ipv4_hdr *ipv4_hdr;
    const struct rte_udp_hdr *udp_hdr;
    MdHeader *md_header;
    MdBookUpdate *md_book_update;

    uint16_t eth_type;
    uint16_t ip_proto;

    // 1. Ethernet Header
    eth_hdr = rte_pktmbuf_mtod(m, const struct rte_ether_hdr *);
    if (m->data_len < sizeof(struct rte_ether_hdr)) {
        // Truncated packet
        return result;
    }
    eth_type = rte_be_to_cpu_16(eth_hdr->ether_type);

    // Only handle IPv4 for now
    if (eth_type != RTE_ETHER_TYPE_IPV4) {
        return result;
    }

    // 2. IPv4 Header
    ipv4_hdr = (const struct rte_ipv4_hdr *)(rte_pktmbuf_mtod(m, char *) + sizeof(struct rte_ether_hdr));
    if (m->data_len < sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr)) {
        // Truncated packet
        return result;
    }
    ip_proto = ipv4_hdr->next_proto_id;

    // Only handle UDP for now
    if (ip_proto != IPPROTO_UDP) {
        return result;
    }

    // 3. UDP Header
    udp_hdr = (const struct rte_udp_hdr *)(rte_pktmbuf_mtod(m, char *) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
    if (m->data_len < sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr)) {
        // Truncated packet
        return result;
    }

    // Calculate the offset to the start of the market data header
    uint16_t payload_offset = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);

    // Check if the mbuf contains at least the full MdHeader
    if (m->data_len < payload_offset + sizeof(MdHeader)) {
        return result;
    }

    md_header = (MdHeader *)(rte_pktmbuf_mtod(m, char *) + payload_offset);

    // Convert from Little Endian (Protocol Spec) to CPU order
    uint16_t magic_host = rte_le_to_cpu_16(md_header->magic);
    uint16_t msg_type_host = rte_le_to_cpu_16(md_header->msg_type);

    // Validate Magic
    if (magic_host != 0xAABB) {
        // printf("DEBUG: Invalid Magic: 0x%04x\n", magic_host); // For debugging
        return result;
    }

    // Only handle Book Update message type for now
    if (msg_type_host != 0x0001) {
        // printf("DEBUG: Invalid MsgType: 0x%04x\n", msg_type_host); // For debugging
        return result;
    }

    // Check if the mbuf contains the full Book Update payload
    if (m->data_len < payload_offset + sizeof(MdHeader) + sizeof(MdBookUpdate)) {
        // printf("DEBUG: Truncated Book Update payload. Expected %zu, got %u\n",
        //        sizeof(MdHeader) + sizeof(MdBookUpdate), m->data_len - payload_offset); // For debugging
        return result;
    }

    md_book_update = (MdBookUpdate *)(rte_pktmbuf_mtod(m, char *) + payload_offset + sizeof(MdHeader));

    // If all checks pass, the packet is valid.
    result.valid = true;
    result.update = md_book_update; // Zero-copy: point directly into the mbuf

    return result;
}
