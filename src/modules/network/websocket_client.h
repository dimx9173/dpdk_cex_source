#ifndef _WEBSOCKET_CLIENT_H_
#define _WEBSOCKET_CLIENT_H_

#include "micro_tcp.h"
#include "modules/market_data/order_book.h" // Include OrderBookManager
#include "proto.h"                          // For websocket_hdr_t
#include "tls_socket.h"

#include <chrono>
#include <functional> // For std::function
#include <map>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Subscription state for a channel
 */
enum class SubscriptionState : uint8_t {
  PENDING,   // Request sent, waiting for confirmation
  CONFIRMED, // Exchange confirmed subscription
  FAILED     // Exchange rejected subscription
};

/**
 * @brief Subscription entry tracking
 */
struct Subscription {
  aero::ExchangeId exchange;
  std::string instrument;
  std::string channel;
  SubscriptionState state;
};

class WebSocketClient {
public:
  enum WsState {
    WS_DISCONNECTED,
    WS_CONNECTING_TCP,
    WS_CONNECTING_TLS,
    WS_HANDSHAKE_SENT,
    WS_CONNECTED
  };

  using MessageCallback = std::function<void(const std::string &)>;
  using SubscriptionCallback =
      std::function<void(const Subscription &, bool success)>;
  using StateChangeCallback =
      std::function<void(WsState old_state, WsState new_state)>;

  WebSocketClient(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip,
                  uint16_t dst_port, const rte_ether_addr &src_mac,
                  const rte_ether_addr &dst_mac, struct rte_mempool *mbuf_pool,
                  const std::string &host, const std::string &path,
                  MessageCallback on_message_cb,
                  aero::OrderBookManager &order_book_manager);

  WsState get_state() const { return state_; }

  // Initiate connection and handshake
  std::vector<rte_mbuf *> connect();

  // Process incoming mbufs, return outgoing mbufs
  std::vector<rte_mbuf *> process_rx(rte_mbuf *rx_mbuf);

  // Send a text message over WebSocket
  std::vector<rte_mbuf *> send_text_message(const std::string &message);

  // --- Subscription API ---

  /**
   * @brief Subscribe to a market data channel
   * @param exchange Exchange ID (OKX or BYBIT)
   * @param instrument Instrument ID (e.g., "BTC-USDT" for OKX, "BTCUSDT" for
   * Bybit)
   * @param channel Channel name (e.g., "books-l2-tbt" for OKX, "orderbook.50"
   * for Bybit)
   * @return Outgoing mbufs containing the subscription request
   */
  std::vector<rte_mbuf *> subscribe(aero::ExchangeId exchange,
                                    const std::string &instrument,
                                    const std::string &channel);

  /**
   * @brief Unsubscribe from a market data channel
   */
  std::vector<rte_mbuf *> unsubscribe(aero::ExchangeId exchange,
                                      const std::string &instrument,
                                      const std::string &channel);

  /**
   * @brief Set callback for subscription state changes
   */
  void set_subscription_callback(SubscriptionCallback cb) {
    on_subscription_cb_ = std::move(cb);
  }

  /**
   * @brief Set callback for connection state changes
   */
  void set_state_change_callback(StateChangeCallback cb) {
    on_state_change_cb_ = std::move(cb);
  }

  /**
   * @brief Check if heartbeat is due and generate pong if needed
   * @return Outgoing mbufs (empty if no action needed)
   */
  std::vector<rte_mbuf *> check_heartbeat();

  /**
   * @brief Gracefully disconnect
   */
  std::vector<rte_mbuf *> disconnect();

private:
  WsState state_;
  MicroTcp tcp_client_;
  TlsSocket tls_socket_;
  std::string ws_host_;
  std::string ws_path_;
  std::string ws_key_; // WebSocket-Key header value
  MessageCallback on_message_callback_;
  SubscriptionCallback on_subscription_cb_;
  StateChangeCallback on_state_change_cb_;
  aero::OrderBookManager &order_book_manager_;

  // Subscription tracking: key = "exchange:instrument:channel"
  std::map<std::string, Subscription> subscriptions_;

  // Heartbeat tracking
  std::chrono::steady_clock::time_point last_ping_received_;
  bool pending_pong_ = false;

  // Reconnection state
  uint32_t reconnect_attempts_ = 0;
  static constexpr uint32_t MAX_RECONNECT_ATTEMPTS = 10;
  static constexpr uint32_t BASE_RECONNECT_DELAY_MS = 1000; // 1 second
  std::chrono::steady_clock::time_point next_reconnect_time_;
  bool reconnect_pending_ = false;

  // Saved subscriptions for restoration after reconnect
  std::vector<Subscription> saved_subscriptions_;

  // Buffers for TLS and WebSocket data
  std::vector<uint8_t> tls_rx_buffer_;
  std::vector<uint8_t> tls_tx_buffer_;
  std::vector<uint8_t> websocket_rx_buffer_; // For fragmented WS messages

  struct rte_mempool *mbuf_pool_;

  // Internal helpers
  void set_state(WsState new_state);
  std::string make_subscription_key(aero::ExchangeId exchange,
                                    const std::string &instrument,
                                    const std::string &channel) const;

  std::vector<rte_mbuf *>
  handle_tcp_data(const std::vector<uint8_t> &tcp_payload);
  std::vector<rte_mbuf *>
  handle_tls_data(const std::vector<uint8_t> &tls_payload);
  std::vector<rte_mbuf *> generate_websocket_handshake();
  std::vector<rte_mbuf *> generate_websocket_frame(const std::string &payload,
                                                   uint8_t opcode, bool fin);
  std::string generate_websocket_key();
  std::string sha1_base64(const std::string &input);
  void process_websocket_frame(const uint8_t *data, size_t len);

  // Subscription message generators
  std::string generate_okx_subscribe_message(const std::string &instrument,
                                             const std::string &channel) const;
  std::string
  generate_bybit_subscribe_message(const std::string &instrument,
                                   const std::string &channel) const;

  // Ping/pong handlers
  void handle_ping(aero::ExchangeId exchange, const std::string &ping_data);
  std::vector<rte_mbuf *> generate_pong(aero::ExchangeId exchange,
                                        const std::string &ping_data);

  // Subscription confirmation parsing
  void parse_subscription_response(const std::string &json_msg);

  // Reconnection helpers
  void initiate_reconnect();
  std::vector<rte_mbuf *> restore_subscriptions();
  uint32_t calculate_backoff_ms() const;

public:
  /**
   * @brief Attempt reconnection if pending
   * @return Outgoing mbufs for connection attempt
   */
  std::vector<rte_mbuf *> try_reconnect();

  /**
   * @brief Check if reconnection is pending
   */
  bool is_reconnect_pending() const { return reconnect_pending_; }

  /**
   * @brief Get number of reconnection attempts
   */
  uint32_t get_reconnect_attempts() const { return reconnect_attempts_; }
};

#endif // _WEBSOCKET_CLIENT_H_