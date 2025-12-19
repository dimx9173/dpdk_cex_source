/**
 * @file json_parser.cpp
 * @brief Implementation of high-performance JSON parser for OKX/Bybit market
 * data
 */

#include "json_parser.h"
#include <cstdlib>
#include <cstring>

namespace aero {

JsonParser::JsonParser() : parser_(), padded_buffer_() {}

ParsedMarketData JsonParser::parse_packet(const char *payload, size_t len,
                                          ExchangeId exchange) {
  // Create a padded_string_view (assumes caller provided SIMDJSON_PADDING)
  simdjson::padded_string_view json(payload, len,
                                    len + simdjson::SIMDJSON_PADDING);

  switch (exchange) {
  case ExchangeId::OKX:
    return parse_okx_message(json);
  case ExchangeId::BYBIT:
    return parse_bybit_message(json);
  default:
    return ParsedMarketData{.exchange = exchange,
                            .msg_type = MessageType::UNKNOWN,
                            .symbol = {},
                            .timestamp_ns = 0,
                            .updates = {},
                            .valid = false,
                            .error_msg = "Unknown exchange"};
  }
}

ParsedMarketData JsonParser::parse_packet_safe(const char *payload, size_t len,
                                               ExchangeId exchange) {
  // Copy to padded buffer for safety
  padded_buffer_ = simdjson::padded_string(payload, len);

  switch (exchange) {
  case ExchangeId::OKX:
    return parse_okx_message(padded_buffer_);
  case ExchangeId::BYBIT:
    return parse_bybit_message(padded_buffer_);
  default:
    return ParsedMarketData{.exchange = exchange,
                            .msg_type = MessageType::UNKNOWN,
                            .symbol = {},
                            .timestamp_ns = 0,
                            .updates = {},
                            .valid = false,
                            .error_msg = "Unknown exchange"};
  }
}

uint64_t JsonParser::parse_price_to_int(std::string_view price_str) {
  // Convert price string to integer (multiply by 1e8 for precision)
  // Use null-terminated copy for strtod
  char buf[64];
  size_t len = std::min(price_str.size(), sizeof(buf) - 1);
  std::memcpy(buf, price_str.data(), len);
  buf[len] = '\0';

  double price = std::strtod(buf, nullptr);
  return static_cast<uint64_t>(price * 100000000.0);
}

double JsonParser::parse_quantity(std::string_view qty_str) {
  // Use null-terminated copy for strtod
  char buf[64];
  size_t len = std::min(qty_str.size(), sizeof(buf) - 1);
  std::memcpy(buf, qty_str.data(), len);
  buf[len] = '\0';

  return std::strtod(buf, nullptr);
}

/**
 * Parse OKX books-l2-tbt message format:
 * {
 *   "arg": {"channel": "books-l2-tbt", "instId": "BTC-USDT"},
 *   "action": "update",  // or "snapshot"
 *   "data": [{
 *     "bids": [["price", "size", "0", "numOrders"], ...],
 *     "asks": [["price", "size", "0", "numOrders"], ...],
 *     "ts": "1597026383085",
 *     "checksum": -1893385749
 *   }]
 * }
 */
ParsedMarketData
JsonParser::parse_okx_message(simdjson::padded_string_view json) {
  ParsedMarketData result{};
  result.exchange = ExchangeId::OKX;
  result.valid = false;

  try {
    simdjson::ondemand::document doc = parser_.iterate(json);

    // Get action type
    std::string_view action = doc["action"].get_string().value();
    if (action == "snapshot") {
      result.msg_type = MessageType::SNAPSHOT;
    } else if (action == "update") {
      result.msg_type = MessageType::UPDATE;
    } else {
      result.error_msg = "Unknown action type";
      return result;
    }

    // Get symbol from arg.instId
    std::string_view inst_id = doc["arg"]["instId"].get_string().value();
    size_t copy_len = std::min(inst_id.size(), sizeof(result.symbol) - 1);
    std::memcpy(result.symbol, inst_id.data(), copy_len);
    result.symbol[copy_len] = '\0';

    // Get data array (usually single element)
    auto data_arr = doc["data"].get_array().value();
    for (auto data_elem : data_arr) {
      // Get timestamp
      std::string_view ts_str = data_elem["ts"].get_string().value();
      char ts_buf[32];
      size_t ts_len = std::min(ts_str.size(), sizeof(ts_buf) - 1);
      std::memcpy(ts_buf, ts_str.data(), ts_len);
      ts_buf[ts_len] = '\0';
      uint64_t ts_ms = std::strtoull(ts_buf, nullptr, 10);
      result.timestamp_ns = ts_ms * 1000000ULL; // ms to ns

      // Parse bids
      auto bids = data_elem["bids"].get_array();
      if (!bids.error()) {
        for (auto bid : bids.value()) {
          auto bid_arr = bid.get_array().value();
          auto it = bid_arr.begin();

          std::string_view price_str = (*it).get_string().value();
          ++it;
          std::string_view size_str = (*it).get_string().value();

          OrderBookUpdate update{};
          update.price_int = parse_price_to_int(price_str);
          update.quantity = parse_quantity(size_str);
          update.side = Side::BID;
          update.is_delete = (update.quantity == 0.0);

          result.updates.push_back(update);
        }
      }

      // Parse asks
      auto asks = data_elem["asks"].get_array();
      if (!asks.error()) {
        for (auto ask : asks.value()) {
          auto ask_arr = ask.get_array().value();
          auto it = ask_arr.begin();

          std::string_view price_str = (*it).get_string().value();
          ++it;
          std::string_view size_str = (*it).get_string().value();

          OrderBookUpdate update{};
          update.price_int = parse_price_to_int(price_str);
          update.quantity = parse_quantity(size_str);
          update.side = Side::ASK;
          update.is_delete = (update.quantity == 0.0);

          result.updates.push_back(update);
        }
      }
    }

    result.valid = true;
    result.error_msg = nullptr;

  } catch (simdjson::simdjson_error &e) {
    result.error_msg = e.what();
    result.valid = false;
  }

  return result;
}

/**
 * Parse Bybit orderbook.50 message format:
 * {
 *   "topic": "orderbook.50.BTCUSDT",
 *   "type": "snapshot",  // or "delta"
 *   "ts": 1672304484978,
 *   "data": {
 *     "s": "BTCUSDT",
 *     "b": [["price", "size"], ...],  // bids
 *     "a": [["price", "size"], ...],  // asks
 *     "u": 123456,  // update id
 *     "seq": 7894561
 *   }
 * }
 */
ParsedMarketData
JsonParser::parse_bybit_message(simdjson::padded_string_view json) {
  ParsedMarketData result{};
  result.exchange = ExchangeId::BYBIT;
  result.valid = false;

  try {
    simdjson::ondemand::document doc = parser_.iterate(json);

    // Get type
    std::string_view type_str = doc["type"].get_string().value();
    if (type_str == "snapshot") {
      result.msg_type = MessageType::SNAPSHOT;
    } else if (type_str == "delta") {
      result.msg_type = MessageType::UPDATE;
    } else {
      result.error_msg = "Unknown type";
      return result;
    }

    // Get timestamp (integer in Bybit)
    uint64_t ts_ms = doc["ts"].get_uint64().value();
    result.timestamp_ns = ts_ms * 1000000ULL; // ms to ns

    // Get data object
    auto data = doc["data"].get_object().value();

    // Get symbol
    std::string_view symbol = data["s"].get_string().value();
    size_t copy_len = std::min(symbol.size(), sizeof(result.symbol) - 1);
    std::memcpy(result.symbol, symbol.data(), copy_len);
    result.symbol[copy_len] = '\0';

    // Parse bids (b)
    auto bids = data["b"].get_array();
    if (!bids.error()) {
      for (auto bid : bids.value()) {
        auto bid_arr = bid.get_array().value();
        auto it = bid_arr.begin();

        std::string_view price_str = (*it).get_string().value();
        ++it;
        std::string_view size_str = (*it).get_string().value();

        OrderBookUpdate update{};
        update.price_int = parse_price_to_int(price_str);
        update.quantity = parse_quantity(size_str);
        update.side = Side::BID;
        update.is_delete = (update.quantity == 0.0);

        result.updates.push_back(update);
      }
    }

    // Parse asks (a)
    auto asks = data["a"].get_array();
    if (!asks.error()) {
      for (auto ask : asks.value()) {
        auto ask_arr = ask.get_array().value();
        auto it = ask_arr.begin();

        std::string_view price_str = (*it).get_string().value();
        ++it;
        std::string_view size_str = (*it).get_string().value();

        OrderBookUpdate update{};
        update.price_int = parse_price_to_int(price_str);
        update.quantity = parse_quantity(size_str);
        update.side = Side::ASK;
        update.is_delete = (update.quantity == 0.0);

        result.updates.push_back(update);
      }
    }

    result.valid = true;
    result.error_msg = nullptr;

  } catch (simdjson::simdjson_error &e) {
    result.error_msg = e.what();
    result.valid = false;
  }

  return result;
}

} // namespace aero
