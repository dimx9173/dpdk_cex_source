/**
 * @file bybit_adapter.cpp
 * @brief Bybit exchange adapter implementation
 */

#include "bybit_adapter.h"
#include "config/config.h"
#include "core/logging.h"
#include <cmath>
#include <sstream>

namespace aero {

bool BybitAdapter::parse_orderbook_message(const char *json_data, size_t len,
                                           ParsedOrderBook &out_book) {
  try {
    simdjson::dom::element doc = parser_.parse(json_data, len);

    // Check topic for order book
    std::string_view topic;
    if (doc["topic"].get(topic) != simdjson::SUCCESS) {
      return false;
    }

    if (topic.find("orderbook") == std::string_view::npos) {
      return false;
    }

    // Extract instrument from topic (e.g., "orderbook.50.BTCUSDT")
    size_t last_dot = topic.rfind('.');
    if (last_dot == std::string_view::npos) {
      return false;
    }
    out_book.instrument = std::string(topic.substr(last_dot + 1));

    LOG_PRICE("Parsing " << std::string(topic) << " message for "
                         << out_book.instrument);

    // Check type (snapshot or delta)
    std::string_view type;
    if (doc["type"].get(type) == simdjson::SUCCESS) {
      out_book.is_snapshot = (type == "snapshot");
    } else {
      out_book.is_snapshot = false;
    }

    // Parse data
    auto data = doc["data"];
    if (data.error()) {
      return false;
    }

    // Parse bids (b array)
    auto bids = data["b"];
    if (!bids.error()) {
      for (auto bid : bids) {
        std::string_view price_str = bid.at(0).get_string().value();
        std::string_view size_str = bid.at(1).get_string().value();

        double price = std::stod(std::string(price_str));
        double size = std::stod(std::string(size_str));

        out_book.bids.push_back(
            {static_cast<uint64_t>(std::round(price * PRICE_SCALE)), size});
      }
    }

    // Parse asks (a array)
    auto asks = data["a"];
    if (!asks.error()) {
      for (auto ask : asks) {
        std::string_view price_str = ask.at(0).get_string().value();
        std::string_view size_str = ask.at(1).get_string().value();

        double price = std::stod(std::string(price_str));
        double size = std::stod(std::string(size_str));

        out_book.asks.push_back(
            {static_cast<uint64_t>(std::round(price * PRICE_SCALE)), size});
      }
    }

    // Parse timestamp
    uint64_t ts;
    if (data["ts"].get(ts) == simdjson::SUCCESS) {
      out_book.timestamp_ms = ts;
    }

    return true;
  } catch (...) {
    return false;
  }
}

std::string
BybitAdapter::generate_subscribe_message(const std::string &instrument,
                                         const std::string &channel) const {
  std::ostringstream oss;
  oss << R"({"op":"subscribe","args":[")" << channel << "." << instrument
      << R"("]})";
  return oss.str();
}

std::string
BybitAdapter::generate_unsubscribe_message(const std::string &instrument,
                                           const std::string &channel) const {
  std::ostringstream oss;
  oss << R"({"op":"unsubscribe","args":[")" << channel << "." << instrument
      << R"("]})";
  return oss.str();
}

std::string BybitAdapter::generate_pong_message(const std::string &) const {
  return R"({"op":"pong"})";
}

bool BybitAdapter::is_ping_message(const char *json_data, size_t len) const {
  try {
    simdjson::dom::parser temp_parser;
    simdjson::dom::element doc = temp_parser.parse(json_data, len);

    std::string_view op;
    if (doc["op"].get(op) == simdjson::SUCCESS) {
      // It's a ping only if op is ping AND it's not a success response
      bool has_success = doc["success"].get_bool().error() == simdjson::SUCCESS;
      return op == "ping" && !has_success;
    }
    return false;
  } catch (...) {
    return false;
  }
}

bool BybitAdapter::is_subscription_response(const char *json_data,
                                            size_t len) const {
  try {
    simdjson::dom::parser temp_parser;
    simdjson::dom::element doc = temp_parser.parse(json_data, len);

    bool success;
    std::string_view op;
    if (doc["success"].get(success) == simdjson::SUCCESS &&
        doc["op"].get(op) == simdjson::SUCCESS) {
      return op == "subscribe" || op == "unsubscribe";
    }
    return false;
  } catch (...) {
    return false;
  }
}

} // namespace aero
