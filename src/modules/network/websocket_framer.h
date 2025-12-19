#ifndef _WEBSOCKET_FRAMER_H_
#define _WEBSOCKET_FRAMER_H_

#include "proto.h"
#include <cstdint>
#include <cstring>
#include <random>
#include <rte_byteorder.h>
#include <rte_cycles.h> // For randomness seeding if needed
#include <string>

namespace aero {

class WebSocketFramer {
public:
  /**
   * @brief Formats a WebSocket packet into a provided buffer.
   *
   * @param buffer Output buffer
   * @param buffer_len Size of output buffer
   * @param payload Request payload (e.g., JSON string)
   * @param opcode WebSocket Opcode (0x1 = Text, 0x2 = Binary)
   * @param mask Whether to mask the payload (Client -> Server MUST be true)
   * @return Total length of the framed packet, or 0 on error (buffer too small)
   */
  static size_t frame_message(uint8_t *buffer, size_t buffer_len,
                              const std::string &payload, uint8_t opcode = 0x1,
                              bool mask = true) {
    size_t payload_len = payload.length();
    size_t header_len = 2; // Fixed header

    // Calculate extended length bytes
    if (payload_len >= 126) {
      if (payload_len <= 65535) {
        header_len += 2;
      } else {
        header_len += 8;
      }
    }

    // Calculate masking key bytes
    if (mask) {
      header_len += 4;
    }

    // Check buffer size
    if (header_len + payload_len > buffer_len) {
      return 0; // Error: Buffer too small
    }

    // 1. First Byte: FIN + Opcode
    // FIN = 1 (0x80), RSV = 0, Opcode = provided
    buffer[0] = 0x80 | (opcode & 0x0F);

    // 2. Second Byte: Mask + Payload Len
    size_t offset = 1;
    uint8_t mask_bit = mask ? 0x80 : 0x00;

    if (payload_len < 126) {
      buffer[offset++] = mask_bit | (payload_len & 0x7F);
    } else if (payload_len <= 65535) {
      buffer[offset++] = mask_bit | 126;
      // Network Byte Order (Big Endian)
      uint16_t len16 = rte_cpu_to_be_16((uint16_t)payload_len);
      memcpy(buffer + offset, &len16, 2);
      offset += 2;
    } else {
      buffer[offset++] = mask_bit | 127;
      uint64_t len64 = rte_cpu_to_be_64((uint64_t)payload_len);
      memcpy(buffer + offset, &len64, 8);
      offset += 8;
    }

    // 3. Mask Key
    if (mask) {
      // Generate simple mask key using cycle counter for speed
      // (Security is not a primary concern for this HFT link, but protocol
      // compliance is)
      uint32_t mask_key_u32 = (uint32_t)rte_get_timer_cycles();
      uint8_t mask_key[4];
      memcpy(mask_key, &mask_key_u32, 4);
      memcpy(buffer + offset, mask_key, 4);
      offset += 4;

      // 4. Payload (Masked)
      const uint8_t *input = (const uint8_t *)payload.c_str();
      for (size_t i = 0; i < payload_len; i++) {
        buffer[offset + i] = input[i] ^ mask_key[i % 4];
      }
    } else {
      // 4. Payload (Unmasked)
      memcpy(buffer + offset, payload.c_str(), payload_len);
    }

    return header_len + payload_len;
  }
};

} // namespace aero

#endif // _WEBSOCKET_FRAMER_H_
