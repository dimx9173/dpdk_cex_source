#ifndef AERO_MODULES_NETWORK_UDP_PUBLISHER_H
#define AERO_MODULES_NETWORK_UDP_PUBLISHER_H

#include "modules/common/aero_types.h"
#include "modules/exchange/exchange_adapter.h"
#include <cstdint>
#include <string>
#include <vector>

namespace aero {

// Binary Protocol Constants
constexpr uint32_t UDP_FEED_MAGIC = 0x48465444; // "HFTD"
constexpr uint16_t UDP_FEED_VERSION = 1;

// Packet Header Structure (packed)
struct __attribute__((packed)) UdpMarketHeader {
  uint32_t magic;
  uint16_t version;
  uint8_t msg_type;    // 1=Snapshot, 2=Delta
  uint8_t exchange_id; // See ExchangeId
  uint64_t timestamp_ns;
  uint32_t symbol_len;
  uint16_t bid_count;
  uint16_t ask_count;
};

// Price Level Structure (packed)
struct __attribute__((packed)) UdpPriceLevel {
  uint64_t price_int; // Scaled by 1e8
  double quantity;    // Floating point size
};

class UdpPublisher {
public:
  UdpPublisher();
  ~UdpPublisher();

  /**
   * @brief Initialize the UDP socket
   *
   * @param address IP address to bind/send to (e.g., "127.0.0.1")
   * @param port Port to send to (e.g., 13988)
   * @return true on success, false on failure
   */
  bool init(const std::string &address, int port);

  /**
   * @brief Broadcast an OrderBook update
   *
   * Serializes the book into the binary format and sends it via UDP.
   * This method is non-blocking.
   *
   * @param book The parsed order book data
   * @param exchange_id The exchange this book belongs to
   */
  void publish(const ParsedOrderBook &book, ExchangeId exchange_id);

  /**
   * @brief Close the socket
   */
  void close();

  // Helper to check if initialized
  bool is_initialized() const { return socket_fd_ >= 0; }

  friend class UdpPublisherTest;

private:
  int socket_fd_;
  std::string target_address_;
  int target_port_;

  void serialize_and_send(const ParsedOrderBook &book, ExchangeId exchange_id);
};

} // namespace aero

#endif // AERO_MODULES_NETWORK_UDP_PUBLISHER_H
