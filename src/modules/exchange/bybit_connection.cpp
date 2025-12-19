#include "bybit_connection.h"
#include "config/config.h"
#include "core/logging.h"
#include <iostream>

namespace aero {

BybitConnection::BybitConnection(UdpPublisher *udp_publisher)
    : ws_client_(std::make_unique<BoostWebSocketClient>()),
      adapter_(std::make_unique<BybitAdapter>()),
      udp_publisher_(udp_publisher) {}

BybitConnection::~BybitConnection() {}

bool BybitConnection::connect() {
  // Bybit Public WebSocket endpoint
  // Spot: wss://stream.bybit.com/v5/public/spot
  // Linear (Perpetual): wss://stream.bybit.com/v5/public/linear
  // We assume Linear (SWAP) for HFT usually, or parameterize it.
  // For now, let's use Linear as default for "BTC-USDT-SWAP" equivalent
  // (BTCUSDT)

  std::string host = "stream.bybit.com";
  std::string port = "443";
  std::string path = "/v5/public/linear";

  LOG_SYSTEM("BybitConnection: Connecting to " << host << ":" << port << path
                                               << "...");

  ws_client_->set_on_reconnect([this]() {
    LOG_SYSTEM("BybitConnection: Reconnection detected. Resubscribing...");
    this->resubscribe();
  });

  bool success = ws_client_->connect(host, port, path);
  if (success) {
    this->resubscribe();
  }
  return success;
}

void BybitConnection::subscribe(const std::vector<std::string> &instruments,
                                const std::string &channel) {
  // Always save subscriptions first (so they can be restored on reconnect)
  active_subscriptions_.push_back({instruments, channel});
  LOG_SYSTEM("BybitConnection: Registered subscription for channel: "
             << channel << " with " << instruments.size() << " instruments");

  // Only send if currently connected
  if (!ws_client_->is_connected()) {
    LOG_SYSTEM("BybitConnection: Not connected yet. Will send subscription on "
               "connect.");
    return;
  }

  // Send subscription messages
  for (const auto &inst : instruments) {
    std::string sub_msg = adapter_->generate_subscribe_message(inst, channel);
    ws_client_->send(sub_msg);
    LOG_SYSTEM("BybitConnection: Sent subscription: " << sub_msg);
  }
}

void BybitConnection::resubscribe() {
  for (const auto &sub : active_subscriptions_) {
    for (const auto &inst : sub.instruments) {
      std::string sub_msg =
          adapter_->generate_subscribe_message(inst, sub.channel);
      ws_client_->send(sub_msg);
      LOG_SYSTEM("BybitConnection: Resent subscription: " << sub_msg);
    }
  }
}

void BybitConnection::poll(
    std::function<void(const ParsedOrderBook &)> on_orderbook_callback) {
  while (true) {
    auto msg_opt = ws_client_->get_next_message();
    if (!msg_opt) {
      break;
    }
    process_message(*msg_opt, on_orderbook_callback);
  }
}

void BybitConnection::process_message(
    const std::string &msg,
    std::function<void(const ParsedOrderBook &)> &callback) {
  // DEBUG: Log all incoming messages (controlled by DEBUG_LOG_ENABLED)
  if (app_config.debug_log_enabled) {
    LOG_SYSTEM("DEBUG Bybit Message: " << msg);
  }

  // 1. Check for Ping/Pong handling
  if (adapter_->is_ping_message(msg.c_str(), msg.length())) {
    // Bybit usually expects a pong or just keeps alive.
    // Note: Bybit sends {"op":"ping"}? Or we send ping?
    // Adapter logic should handle `is_ping_message`.
    // If server sends ping, we reply pong.
    std::string pong = adapter_->generate_pong_message();
    if (!pong.empty()) {
      ws_client_->send(pong);
    }
    return;
  }

  // 2. Check for Subscription Response
  if (adapter_->is_subscription_response(msg.c_str(), msg.length())) {
    LOG_SYSTEM("BybitConnection: Subscription response: " << msg);
    return;
  }

  // 3. Try parsing OrderBook
  ParsedOrderBook book;
  if (adapter_->parse_orderbook_message(msg.c_str(), msg.length(), book)) {
    // Broadcast via UDP if enabled
    if (udp_publisher_ && udp_publisher_->is_initialized()) {
      udp_publisher_->publish(book, ExchangeId::BYBIT);
    }

    if (callback) {
      callback(book);
    }
  }
}

void BybitConnection::send_heartbeat() {
  if (ws_client_ && ws_client_->is_connected()) {
    ws_client_->send(R"({"op":"ping"})");
  }
}

void BybitConnection::send_order(const std::string &json_msg) {
  if (ws_client_) {
    ws_client_->send(json_msg);
  }
}

bool BybitConnection::is_connected() const {
  return ws_client_ && ws_client_->is_connected();
}

} // namespace aero
