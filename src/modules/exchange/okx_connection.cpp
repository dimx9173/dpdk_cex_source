#include "okx_connection.h"
#include "config/config.h"
#include "core/logging.h"
#include <iostream>

namespace aero {

OkxConnection::OkxConnection(UdpPublisher *udp_publisher)
    : ws_client_(std::make_unique<BoostWebSocketClient>()),
      adapter_(std::make_unique<OkxAdapter>()), udp_publisher_(udp_publisher) {}

OkxConnection::~OkxConnection() {
  // Unique pointers auto-clean
}

bool OkxConnection::connect() {
  // Parse the endpoint URL to get host, port, and path
  // Default OKX public endpoint: wss://ws.okx.com:8443/ws/v5/public
  // For simplicity, hardcoding typical values or using adapter's hint
  // In a robust implementation, we'd use a URL parser

  std::string host = "ws.okx.com";
  std::string port = "8443";
  std::string path = "/ws/v5/public";

  LOG_SYSTEM("OkxConnection: Connecting to " << host << ":" << port << path
                                             << "...");

  ws_client_->set_on_reconnect([this]() {
    LOG_SYSTEM("OkxConnection: Reconnection detected. Resubscribing...");
    this->resubscribe();
  });

  bool success = ws_client_->connect(host, port, path);
  if (success) {
    this->resubscribe();
  }
  return success;
}

void OkxConnection::subscribe(const std::vector<std::string> &instruments,
                              const std::string &channel) {
  // Always save subscriptions first (so they can be restored on reconnect)
  active_subscriptions_.push_back({instruments, channel});
  LOG_SYSTEM("OkxConnection: Registered subscription for channel: "
             << channel << " with " << instruments.size() << " instruments");

  // Only send if currently connected
  if (!ws_client_->is_connected()) {
    LOG_SYSTEM(
        "OkxConnection: Not connected yet. Will send subscription on connect.");
    return;
  }

  // Send subscription messages
  for (const auto &inst : instruments) {
    std::string sub_msg = adapter_->generate_subscribe_message(inst, channel);
    ws_client_->send(sub_msg);
    LOG_SYSTEM("OkxConnection: Sent subscription: " << sub_msg);
  }
}

void OkxConnection::resubscribe() {
  for (const auto &sub : active_subscriptions_) {
    for (const auto &inst : sub.instruments) {
      std::string sub_msg =
          adapter_->generate_subscribe_message(inst, sub.channel);
      ws_client_->send(sub_msg);
      LOG_SYSTEM("OkxConnection: Resent subscription: " << sub_msg);
    }
  }
}

void OkxConnection::poll(
    std::function<void(const ParsedOrderBook &)> on_orderbook_callback) {
  while (true) {
    auto msg_opt = ws_client_->get_next_message();
    if (!msg_opt) {
      break; // Queue empty
    }

    process_message(*msg_opt, on_orderbook_callback);
  }
}

void OkxConnection::process_message(
    const std::string &msg,
    std::function<void(const ParsedOrderBook &)> &callback) {
  // DEBUG: Log all incoming messages (controlled by DEBUG_LOG_ENABLED)
  if (app_config.debug_log_enabled) {
    LOG_SYSTEM("DEBUG OKX Message: " << msg);
  }

  // 1. Check for Ping
  if (adapter_->is_ping_message(msg.c_str(), msg.length())) {
    std::string pong = adapter_->generate_pong_message();
    ws_client_->send(pong);
    return;
  }

  // 2. Check for Subscription Response
  if (adapter_->is_subscription_response(msg.c_str(), msg.length())) {
    // Log subscription success/fail
    LOG_SYSTEM("OkxConnection: Subscription response: " << msg);
    return;
  }

  // 3. Try to parse as OrderBook
  ParsedOrderBook book;
  if (adapter_->parse_orderbook_message(msg.c_str(), msg.length(), book)) {
    // Broadcast via UDP if enabled
    if (udp_publisher_ && udp_publisher_->is_initialized()) {
      udp_publisher_->publish(book, ExchangeId::OKX);
    }

    if (callback) {
      callback(book);
    }
  } else {
    // Unknown message or parsing failure
    LOG_SYSTEM(
        "OkxConnection: Failed to parse message or unknown type: " << msg);
  }
}

void OkxConnection::send_heartbeat() {
  if (ws_client_ && ws_client_->is_connected()) {
    ws_client_->send("ping");
  }
}

void OkxConnection::send_order(const std::string &json_msg) {
  if (ws_client_) {
    ws_client_->send(json_msg);
  }
}

bool OkxConnection::is_connected() const {
  return ws_client_ && ws_client_->is_connected();
}

} // namespace aero
