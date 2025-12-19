/**
 * @file json_parser.h
 * @brief High-performance JSON parser for OKX/Bybit market data using simdjson
 *
 * This parser uses simdjson's On-Demand API for zero-copy parsing of
 * WebSocket market data messages in the DPDK fast path.
 */

#ifndef _JSON_PARSER_H_
#define _JSON_PARSER_H_

#include <cstddef>
#include <cstdint>
#include <simdjson.h>
#include <string_view>
#include <vector>

#include "../common/aero_types.h"

namespace aero {

// ExchangeId defined in aero_types.h

/**
 * @brief Price scaling factor for fixed-point representation
 *
 * Prices are stored as uint64_t with 8 decimal places of precision.
 * Example: $95000.12345678 -> 9500012345678
 */
constexpr uint64_t PRICE_SCALE = 100000000ULL; // 10^8

/**
 * @brief Message type from exchange
 */
enum class MessageType : uint8_t {
  SNAPSHOT = 0, // Full orderbook snapshot
  UPDATE = 1,   // Incremental update (OKX) / Delta (Bybit)
  UNKNOWN = 255
};

/**
 * @brief Side of the order book
 */
enum class Side : uint8_t { BID = 0, ASK = 1 };

/**
 * @brief Single order book level update
 */
struct OrderBookUpdate {
  uint64_t price_int; // Price as integer (scaled by 1e8 for precision)
  double quantity;    // Quantity at this level
  Side side;          // Bid or Ask
  bool is_delete;     // True if quantity is 0 (delete this level)
};

/**
 * @brief Parsed market data result
 */
struct ParsedMarketData {
  ExchangeId exchange;
  MessageType msg_type;
  char symbol[32];       // Null-terminated symbol string
  uint64_t timestamp_ns; // Unix timestamp in nanoseconds
  std::vector<OrderBookUpdate> updates;
  bool valid;            // True if parsing succeeded
  const char *error_msg; // Error message if parsing failed
};

/**
 * @brief High-performance JSON parser for market data
 *
 * Thread-local usage recommended to avoid parser state sharing.
 */
class JsonParser {
public:
  JsonParser();
  ~JsonParser() = default;

  // Non-copyable, non-movable (parser state is expensive)
  JsonParser(const JsonParser &) = delete;
  JsonParser &operator=(const JsonParser &) = delete;

  /**
   * @brief Parse a JSON packet from exchange
   *
   * @param payload Pointer to JSON string (must have SIMDJSON_PADDING extra
   * bytes)
   * @param len Length of the JSON string (excluding padding)
   * @param exchange Exchange identifier
   * @return ParsedMarketData Result of parsing
   */
  ParsedMarketData parse_packet(const char *payload, size_t len,
                                ExchangeId exchange);

  /**
   * @brief Parse with automatic padding (copies data to internal buffer)
   *
   * Use this when the source buffer doesn't have SIMDJSON_PADDING.
   * @param payload Pointer to JSON string
   * @param len Length of the JSON string
   * @param exchange Exchange identifier
   * @return ParsedMarketData Result of parsing
   */
  ParsedMarketData parse_packet_safe(const char *payload, size_t len,
                                     ExchangeId exchange);

private:
  simdjson::ondemand::parser parser_;
  simdjson::padded_string padded_buffer_; // For safe parsing

  ParsedMarketData parse_okx_message(simdjson::padded_string_view json);
  ParsedMarketData parse_bybit_message(simdjson::padded_string_view json);

  // Helper to convert price string to integer
  static uint64_t parse_price_to_int(std::string_view price_str);
  // Helper to convert quantity string to double
  static double parse_quantity(std::string_view qty_str);
};

} // namespace aero

#endif // _JSON_PARSER_H_
