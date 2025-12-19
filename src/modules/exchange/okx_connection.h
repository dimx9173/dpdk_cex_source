#pragma once

#include "../network/boost_websocket_client.h"
#include "../network/udp_publisher.h"
#include "okx_adapter.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aero {

class OkxConnection {
public:
  OkxConnection(UdpPublisher *udp_publisher = nullptr);
  ~OkxConnection();

  OkxConnection(const OkxConnection &) = delete;
  OkxConnection &operator=(const OkxConnection &) = delete;

  /**
   * @brief Connects to the OKX WebSocket server.
   * @return true on success, false on failure
   */
  bool connect();

  /**
   * @brief Subscribes to the specified order book channels.
   * @param instruments List of instruments to subscribe to (e.g.,
   * "BTC-USDT-SWAP")
   * @param channel The channel name (default: "books-l2-tbt")
   */
  void subscribe(const std::vector<std::string> &instruments,
                 const std::string &channel = "books-l2-tbt");

  /**
   * @brief Polls for new messages and processes them.
   * This should be called periodically by the main thread.
   * @param on_orderbook_callback Callback function for parsed order book data
   */
  void poll(std::function<void(const ParsedOrderBook &)> on_orderbook_callback);

  /**
   * @brief Sends a heartbeat ping message to the exchange.
   */
  void send_heartbeat();

  /**
   * @brief Checks connection status.
   * @return true if connected.
   */
  bool is_connected() const;

  /**
   * @brief Sends an order message to the exchange.
   * @param json_msg The JSON string of the order.
   */
  void send_order(const std::string &json_msg);

private:
  std::unique_ptr<BoostWebSocketClient> ws_client_;
  std::unique_ptr<OkxAdapter> adapter_;
  UdpPublisher *udp_publisher_; // Non-owning pointer

  // Internal helper to process a single message string
  void process_message(const std::string &msg,
                       std::function<void(const ParsedOrderBook &)> &callback);

  struct Subscription {
    std::vector<std::string> instruments;
    std::string channel;
  };
  std::vector<Subscription> active_subscriptions_;
  void resubscribe();

  // For testing only
public:
  void simulate_disconnect() {
    if (ws_client_)
      ws_client_->simulate_network_failure();
  }
};

} // namespace aero
