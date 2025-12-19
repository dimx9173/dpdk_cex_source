/**
 * @file okx_adapter.cpp
 * @brief OKX exchange adapter implementation
 */

#include "okx_adapter.h"
#include "config/config.h"
#include "core/logging.h"
#include <cmath>
#include <sstream>

namespace aero {

bool OkxAdapter::parse_orderbook_message(const char *json_data, size_t len,
                                         ParsedOrderBook &out_book) {
  try {
    simdjson::dom::element doc = parser_.parse(json_data, len);

    // Check if this is an order book message
    std::string_view arg_channel;
    auto arg = doc["arg"];
    if (arg["channel"].get(arg_channel) != simdjson::SUCCESS) {
      return false;
    }

    if (arg_channel != "books-l2-tbt" && arg_channel != "books5" &&
        arg_channel != "books") {
      return false;
    }

    LOG_PRICE("Parsing " << std::string(arg_channel) << " message");

    // Get instrument ID
    std::string_view inst_id;
    if (arg["instId"].get(inst_id) != simdjson::SUCCESS) {
      return false;
    }
    out_book.instrument = std::string(inst_id);

    // Check action (snapshot or update)
    // "books5" is always a full snapshot of top 5 levels
    std::string_view action;
    if (doc["action"].get(action) == simdjson::SUCCESS) {
      out_book.is_snapshot = (action == "snapshot");
    } else {
      // If action is missing, default to false UNLESS it's books5
      if (arg_channel == "books5") {
        out_book.is_snapshot = true;
      } else {
        out_book.is_snapshot = false;
      }
    }

    // Parse data array
    auto data = doc["data"];
    if (data.error()) {
      return false;
    }

    for (auto item : data) {
      // Parse bids
      auto bids = item["bids"];
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

      // Parse asks
      auto asks = item["asks"];
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
      std::string_view ts_str;
      if (item["ts"].get(ts_str) == simdjson::SUCCESS) {
        out_book.timestamp_ms = std::stoull(std::string(ts_str));
      }

      break; // Only process first data item
    }

    return true;
  } catch (...) {
    return false;
  }
}

std::string
OkxAdapter::generate_subscribe_message(const std::string &instrument,
                                       const std::string &channel) const {
  std::ostringstream oss;
  oss << R"({"op":"subscribe","args":[{"channel":")" << channel
      << R"(","instId":")" << instrument << R"("}]})";
  return oss.str();
}

std::string
OkxAdapter::generate_unsubscribe_message(const std::string &instrument,
                                         const std::string &channel) const {
  std::ostringstream oss;
  oss << R"({"op":"unsubscribe","args":[{"channel":")" << channel
      << R"(","instId":")" << instrument << R"("}]})";
  return oss.str();
}

std::string OkxAdapter::generate_pong_message(const std::string &) const {
  return "pong";
}

bool OkxAdapter::is_ping_message(const char *json_data, size_t len) const {
  // OKX sends "ping" as plain text
  return len == 4 && std::string_view(json_data, len) == "ping";
}

bool OkxAdapter::is_subscription_response(const char *json_data,
                                          size_t len) const {
  try {
    simdjson::dom::parser temp_parser;
    simdjson::dom::element doc = temp_parser.parse(json_data, len);

    std::string_view event, op;
    if (doc["event"].get(event) == simdjson::SUCCESS) {
      return event == "subscribe" || event == "unsubscribe" || event == "error";
    }
    if (doc["op"].get(op) == simdjson::SUCCESS && op == "subscribe") {
      return true;
    }
    return false;
  } catch (...) {
    return false;
  }
}

} // namespace aero
