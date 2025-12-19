/**
 * @file okx_adapter.h
 * @brief OKX exchange adapter implementation
 */

#ifndef _OKX_ADAPTER_H_
#define _OKX_ADAPTER_H_

#include "exchange_adapter.h"
#include <simdjson.h>

namespace aero {

/**
 * @brief OKX exchange adapter
 *
 * Handles:
 * - books-l2-tbt order book parsing
 * - Subscription message generation
 * - Ping/pong heartbeat
 */
class OkxAdapter : public IExchangeAdapter {
public:
  OkxAdapter() = default;
  ~OkxAdapter() override = default;

  ExchangeId get_exchange_id() const override { return ExchangeId::OKX; }

  const char *get_exchange_name() const override { return "OKX"; }

  std::string get_ws_endpoint() const override {
    return "wss://ws.okx.com:8443/ws/v5/public";
  }

  bool parse_orderbook_message(const char *json_data, size_t len,
                               ParsedOrderBook &out_book) override;

  std::string
  generate_subscribe_message(const std::string &instrument,
                             const std::string &channel) const override;

  std::string
  generate_unsubscribe_message(const std::string &instrument,
                               const std::string &channel) const override;

  std::string
  generate_pong_message(const std::string &ping_data = "") const override;

  bool is_ping_message(const char *json_data, size_t len) const override;

  bool is_subscription_response(const char *json_data,
                                size_t len) const override;

private:
  simdjson::dom::parser parser_;
  static constexpr uint64_t PRICE_SCALE = 100000000ULL; // 10^8
};

} // namespace aero

#endif // _OKX_ADAPTER_H_
