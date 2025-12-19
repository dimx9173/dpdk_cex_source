/**
 * @file exchange_adapter.h
 * @brief Abstract interface for exchange-specific logic
 *
 * This interface enables modular support for multiple exchanges
 * (OKX, Bybit, Binance, Gate, Bitget, MEXC) without modifying core modules.
 */

#ifndef _EXCHANGE_ADAPTER_H_
#define _EXCHANGE_ADAPTER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "../common/aero_types.h"

namespace aero {

// ExchangeId defined in aero_types.h

/**
 * @brief Price level in order book
 */
struct PriceLevel {
  uint64_t price_int; // Price scaled by PRICE_SCALE (10^8)
  double size;
};

/**
 * @brief Parsed order book data from any exchange
 */
struct ParsedOrderBook {
  std::string instrument;
  std::vector<PriceLevel> bids;
  std::vector<PriceLevel> asks;
  bool is_snapshot;
  uint64_t timestamp_ms;
};

/**
 * @brief Abstract interface for exchange-specific logic
 *
 * Each exchange adapter implements this interface to handle:
 * - Order book message parsing
 * - Subscription message generation
 * - Heartbeat (ping/pong) handling
 */
class IExchangeAdapter {
public:
  virtual ~IExchangeAdapter() = default;

  /**
   * @brief Get the exchange ID this adapter handles
   */
  virtual ExchangeId get_exchange_id() const = 0;

  /**
   * @brief Get the exchange name (for logging)
   */
  virtual const char *get_exchange_name() const = 0;

  /**
   * @brief Get the WebSocket endpoint URL for public market data
   */
  virtual std::string get_ws_endpoint() const = 0;

  /**
   * @brief Parse order book message from JSON
   * @param json_data Raw JSON string from WebSocket
   * @param out_book Output parsed order book
   * @return true if parsing successful, false otherwise
   */
  virtual bool parse_orderbook_message(const char *json_data, size_t len,
                                       ParsedOrderBook &out_book) = 0;

  /**
   * @brief Generate subscription message for a channel
   * @param instrument Instrument ID (exchange-specific format)
   * @param channel Channel name (e.g., "books-l2-tbt" for OKX)
   * @return JSON subscription message
   */
  virtual std::string
  generate_subscribe_message(const std::string &instrument,
                             const std::string &channel) const = 0;

  /**
   * @brief Generate unsubscription message
   */
  virtual std::string
  generate_unsubscribe_message(const std::string &instrument,
                               const std::string &channel) const = 0;

  /**
   * @brief Generate pong response for heartbeat
   * @param ping_data Original ping data (if needed for echo)
   * @return Pong response message
   */
  virtual std::string
  generate_pong_message(const std::string &ping_data = "") const = 0;

  /**
   * @brief Check if a message is a ping request
   * @param json_data Message to check
   * @return true if this is a ping message
   */
  virtual bool is_ping_message(const char *json_data, size_t len) const = 0;

  /**
   * @brief Check if a message is a subscription confirmation
   */
  virtual bool is_subscription_response(const char *json_data,
                                        size_t len) const = 0;
};

} // namespace aero

#endif // _EXCHANGE_ADAPTER_H_
