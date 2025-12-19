#include "boost_websocket_client.h"
#include "core/logging.h"
#include <iostream>

BoostWebSocketClient::BoostWebSocketClient() {
  retry_enabled_ = app_config.ws_retry_enabled;
  retry_max_attempts_ = app_config.ws_retry_max_attempts;
  retry_initial_delay_ms_ = app_config.ws_retry_initial_delay_ms;
  retry_max_delay_ms_ = app_config.ws_retry_max_delay_ms;
  retry_backoff_multiplier_ = app_config.ws_retry_backoff_multiplier;

  retry_timer_ = std::make_unique<net::steady_timer>(ioc_);
}

void BoostWebSocketClient::set_on_reconnect(std::function<void()> cb) {
  on_reconnect_ = cb;
}

BoostWebSocketClient::~BoostWebSocketClient() { close(); }

bool BoostWebSocketClient::connect(const std::string &host,
                                   const std::string &port,
                                   const std::string &target) {
  host_ = host;
  port_ = port;
  target_ = target;
  connection_state_ = ConnectionState::Connecting;
  connected_ = false;

  // Ensure IO Loop is running for retries
  if (!io_thread_.joinable()) {
    io_thread_ = std::thread([this]() { run_io_context(); });
  }

  // Create new stream
  try {
    // Init SSL context
#ifdef NDEBUG
    ssl_ctx_.set_default_verify_paths();
    ssl_ctx_.set_verify_mode(ssl::verify_peer);
#else
    ssl_ctx_.set_verify_mode(ssl::verify_none);
#endif

    ws_ = std::make_unique<
        websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(ioc_,
                                                                 ssl_ctx_);

    if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(),
                                  host.c_str())) {
      beast::error_code ec{static_cast<int>(::ERR_get_error()),
                           net::error::get_ssl_category()};
      throw beast::system_error{ec};
    }

    tcp::resolver resolver(ioc_);
    auto const results = resolver.resolve(host, port);

    beast::get_lowest_layer(*ws_).connect(results);
    ws_->next_layer().handshake(ssl::stream_base::client);
    ws_->handshake(host, target);

    connected_ = true;
    connection_state_ = ConnectionState::Connected;
    retry_count_ = 0;

    do_read();

  } catch (std::exception const &e) {
    std::cerr << "BoostWebSocketClient Connect Error: " << e.what()
              << std::endl;

    // Schedule retry if enabled
    if (retry_enabled_) {
      schedule_reconnect();
    } else {
      connection_state_ = ConnectionState::Disconnected;
    }

    return false;
  }

  return true;
}

void BoostWebSocketClient::send(const std::string &message) {
  if (!connected_)
    return;

  net::post(ioc_, [this, message]() {
    if (!connected_)
      return;
    try {
      ws_->write(net::buffer(message));
    } catch (std::exception const &e) {
      std::cerr << "BoostWebSocketClient Send Error: " << e.what() << std::endl;
      if (retry_enabled_) {
        connected_ = false;
        schedule_reconnect();
      } else {
        close();
      }
    }
  });
}

std::optional<std::string> BoostWebSocketClient::get_next_message() {
  std::string msg;
  if (incoming_queue_.try_dequeue(msg)) {
    return msg;
  }
  return std::nullopt;
}

void BoostWebSocketClient::do_read() {
  ws_->async_read(
      buffer_, [this](beast::error_code ec, std::size_t bytes_transferred) {
        if (ec) {
          std::cout << "BoostWebSocketClient Read Error: " << ec.message()
                    << std::endl;
          if (retry_enabled_) {
            connected_ = false;
            schedule_reconnect();
          } else {
            close();
          }
          return;
        }

        if (app_config.debug_log_enabled) {
          std::cout << "[Network] Received " << bytes_transferred << " bytes"
                    << std::endl;
        }
        // Convert buffer to string and enqueue
        // Limit queue size to prevent memory exhaustion
        if (incoming_queue_.size_approx() < MAX_INCOMING_QUEUE_SIZE) {
          incoming_queue_.enqueue(beast::buffers_to_string(buffer_.data()));
        } else {
          // Drop message and log warning (silently log or count)
          static uint64_t drop_count = 0;
          if (++drop_count % 1000 == 0) {
            LOG_SYSTEM("WARNING: WebSocket incoming queue full. Dropped "
                       << drop_count << " messages.");
          }
        }

        buffer_.consume(buffer_.size()); // Clear buffer

        // Continue reading
        do_read();
      });
}

void BoostWebSocketClient::run_io_context() {
  try {
    // Keep io_context running even if no work
    auto work_guard = net::make_work_guard(ioc_);
    ioc_.run();
  } catch (std::exception const &e) {
    std::cerr << "IO Context thread exception: " << e.what() << std::endl;
  }
}

void BoostWebSocketClient::close() {
  bool was_connected = connected_.exchange(false);
  if (was_connected && ws_) {
    // Post closing to IO thread to be thread safe
    net::post(ioc_, [this]() {
      if (ws_) {
        beast::error_code ec;
        ws_->close(websocket::close_code::normal, ec);
      }
    });
  }

  // Stop IO context and join thread
  ioc_.stop();
  if (io_thread_.joinable()) {
    io_thread_.join();
  }
}

void BoostWebSocketClient::simulate_network_failure() {
  if (ws_) {
    std::cout << "Simulating Network Failure: Closing TCP socket..."
              << std::endl;
    // Get the lowest layer (TCP stream) and close the socket
    beast::get_lowest_layer(*ws_).socket().close();
  }
}

bool BoostWebSocketClient::is_connected() const { return connected_; }

void BoostWebSocketClient::schedule_reconnect() {
  if (retry_count_ >= retry_max_attempts_) {
    std::cerr << "Max retry attempts reached. Giving up." << std::endl;
    connection_state_ = ConnectionState::Disconnected;
    return;
  }

  connection_state_ = ConnectionState::WaitingRetry;
  retry_count_++;
  stats_.reconnect_attempts++;

  // Calculate delay: initial * (multiplier ^ (retry - 1))
  double delay = retry_initial_delay_ms_;
  for (int i = 1; i < retry_count_; ++i)
    delay *= retry_backoff_multiplier_;
  if (delay > retry_max_delay_ms_)
    delay = retry_max_delay_ms_;

  std::cout << "Scheduling reconnect attempt " << retry_count_ << " in "
            << delay << "ms" << std::endl;

  retry_timer_->expires_after(std::chrono::milliseconds((long)delay));
  retry_timer_->async_wait([this](beast::error_code ec) {
    if (ec) {
      if (ec != net::error::operation_aborted)
        std::cerr << "Retry timer error: " << ec.message() << std::endl;
      return;
    }
    attempt_reconnect_async();
  });
}

void BoostWebSocketClient::attempt_reconnect_async() {
  connection_state_ = ConnectionState::Connecting;

  // Re-create stream
  ws_ =
      std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(
          ioc_, ssl_ctx_);

  if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(),
                                host_.c_str())) {
    std::cerr << "SSL SNI failed during reconnect" << std::endl;
    schedule_reconnect();
    return;
  }

  // Async Resolve
  auto resolver = std::make_shared<tcp::resolver>(ioc_);
  resolver->async_resolve(
      host_, port_,
      [this, resolver](beast::error_code ec,
                       tcp::resolver::results_type results) {
        on_resolve(ec, results);
      });
}

void BoostWebSocketClient::on_resolve(beast::error_code ec,
                                      tcp::resolver::results_type results) {
  if (ec) {
    std::cerr << "Resolve failed: " << ec.message() << std::endl;
    schedule_reconnect();
    return;
  }

  beast::get_lowest_layer(*ws_).async_connect(
      results, [this](beast::error_code ec,
                      tcp::resolver::results_type::endpoint_type ep) {
        on_connect(ec, ep);
      });
}

void BoostWebSocketClient::on_connect(
    beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
  if (ec) {
    std::cerr << "Connect failed: " << ec.message() << std::endl;
    schedule_reconnect();
    return;
  }

  ws_->next_layer().async_handshake(
      ssl::stream_base::client,
      [this](beast::error_code ec) { on_ssl_handshake(ec); });
}

void BoostWebSocketClient::on_ssl_handshake(beast::error_code ec) {
  if (ec) {
    std::cerr << "SSL Handshake failed: " << ec.message() << std::endl;
    schedule_reconnect();
    return;
  }

  ws_->async_handshake(host_, target_,
                       [this](beast::error_code ec) { on_handshake(ec); });
}

void BoostWebSocketClient::on_handshake(beast::error_code ec) {
  if (ec) {
    std::cerr << "WebSocket Handshake failed: " << ec.message() << std::endl;
    schedule_reconnect();
    return;
  }

  std::cout << "Reconnected successfully!" << std::endl;
  connected_ = true;
  connection_state_ = ConnectionState::Connected;
  retry_count_ = 0;
  stats_.reconnect_success++;

  if (on_reconnect_) {
    on_reconnect_();
  }

  do_read();
}

// Unused callbacks (using lambdas in do_read/send instead)
void BoostWebSocketClient::on_read(beast::error_code, std::size_t) {}
void BoostWebSocketClient::on_write(beast::error_code, std::size_t) {}
