/**
 * @file order_book.h
 * @brief Local Order Book management for HFT Gateway
 */

#ifndef _ORDER_BOOK_H_
#define _ORDER_BOOK_H_

#include "modules/parser/json_parser.h" // For ExchangeId, OrderBookUpdate
#include <cstdint>
#include <map>
#include <shared_mutex>
#include <string>
#include <vector>

namespace aero {

/**
 * @brief Structure for accessing Best Bid and Offer efficiently
 */
struct BestBidOffer {
  uint64_t bid_price;
  double bid_qty;
  uint64_t ask_price;
  double ask_qty;
};

/**
 * @brief Simplified level structure used by WebSocketClient
 */
struct OrderBookLevel {
  uint64_t price_int;
  double size;
};

/**
 * @brief High-performance Local Order Book
 *
 * Maintains the state of Bids and Asks sorted by price.
 * Supports snapshots (reset) and incremental updates.
 */
class OrderBook {
public:
  OrderBook();
  ~OrderBook() = default;

  /**
   * @brief Apply a full snapshot to the order book
   *
   * clears existing state and populates with new data.
   * @param updates List of updates (snapshot data)
   */
  void apply_snapshot(const std::vector<OrderBookUpdate> &updates);

  /**
   * @brief Apply a batch of incremental updates
   *
   * Inserts, updates, or deletes price levels.
   * @param updates List of updates
   */
  void apply_updates(const std::vector<OrderBookUpdate> &updates);

  /**
   * @brief Apply a single incremental update
   *
   * @param update The update to apply
   */
  void apply_update(const OrderBookUpdate &update);

  /**
   * @brief Get the current Best Bid and Offer
   *
   * @param bbo Output parameter to store BBO
   * @return true if both Bid and Ask exist, false otherwise
   */
  bool get_bbo(BestBidOffer &bbo) const;

  /**
   * @brief Clear the order book
   */
  void clear();

private:
  mutable std::shared_mutex mutex_; // Thread-safe access

  // Internal update without locking (caller must hold lock)
  void apply_update_internal(const OrderBookUpdate &update);

  // Bids: Sorted Descending (Highest price first)
  std::map<uint64_t, double, std::greater<uint64_t>> bids_;

  // Asks: Sorted Ascending (Lowest price first)
  std::map<uint64_t, double, std::less<uint64_t>> asks_;
};

/**
 * @brief Manages multiple OrderBooks for different instruments and exchanges.
 */
class OrderBookManager {
public:
  OrderBookManager() = default;
  ~OrderBookManager() = default;

  /**
   * @brief Get the OrderBook for a specific exchange and instrument
   *
   * @param exchange Exchange ID
   * @param instrument Instrument ID
   * @return OrderBook& Reference to the order book (creates if not exists)
   */
  OrderBook &get_book(ExchangeId exchange, const std::string &instrument);

  /**
   * @brief Apply updates from WebSocketClient
   *
   * Adapts simpler vectors of OrderBookLevel to internal OrderBookUpdate
   * format.
   *
   * @param exchange Exchange ID
   * @param instrument Instrument ID
   * @param bids List of bid levels
   * @param asks List of ask levels
   * @param is_snapshot True if this is a full snapshot (clears existing book)
   */
  void apply_update(ExchangeId exchange, const std::string &instrument,
                    const std::vector<OrderBookLevel> &bids,
                    const std::vector<OrderBookLevel> &asks, bool is_snapshot);

  /**
   * @brief Apply pre-parsed updates from Fast Path JsonParser
   *
   * @param exchange Exchange ID
   * @param instrument Instrument ID
   * @param updates List of updates
   * @param is_snapshot True if this is a full snapshot
   */
  void apply_updates(ExchangeId exchange, const std::string &instrument,
                     const std::vector<OrderBookUpdate> &updates,
                     bool is_snapshot);

  /**
   * @brief Get best bid and ask prices for a specific instrument
   */
  bool get_best_prices(ExchangeId exchange, const std::string &instrument,
                       double &bid_price, double &bid_qty, double &ask_price,
                       double &ask_qty);

private:
  // Map: ExchangeId -> Map: Instrument -> OrderBook
  std::map<ExchangeId, std::map<std::string, OrderBook>> books_;
};

} // namespace aero

#endif // _ORDER_BOOK_H_