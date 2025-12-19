#ifndef _PROTO_H_
#define _PROTO_H_

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h> // Though we primarily use TCP, this is good to have for general packet parsing

// Define WebSocket header structure if needed, or inline parsing logic
// For WebSocket, the header is variable, so a simple struct might not be sufficient.
// We'll define a minimal fixed part for now and handle extensions/payload length dynamically.

/*
 *  WebSocket Frame Header (RFC 6455)
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-------+-+-------------+-------------------------------+
 * |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 * |I|S|S|S|  (4)  |A|     (7)     |         (16/64) bits          |
 * |N|V|V|V|       |S|             |   (if Payload len is 126/127) |
 * | |1|2|3|       |K|             |                               |
 * +-+-+-+-+-------+-+-------------+-------------------------------+
 * |                       Masking-key (32 bits)                   |
 * +---------------------------------------------------------------+
 */
typedef struct websocket_hdr {
    uint8_t fin_rsv_opcode;
    uint8_t mask_payload_len;
    // Extended payload length and Masking-key follow, but are variable.
    // This struct represents just the fixed first two bytes.
} __attribute__((__packed__)) websocket_hdr_t;


// Other custom application-level protocol structs can go here
// For now, we reuse DPDK's standard headers for L2/L3/L4.

#endif /* _PROTO_H_ */