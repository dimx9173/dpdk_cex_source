#pragma once

#include "concurrentqueue.h"
#include "config.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

class BoostWebSocketClient {
public:
  explicit BoostWebSocketClient();
  ~BoostWebSocketClient();

  // Prevent copying
  BoostWebSocketClient(const BoostWebSocketClient &) = delete;
  BoostWebSocketClient &operator=(const BoostWebSocketClient &) = delete;

  /**
   * @brief Connects to the WebSocket server.
   * @param host The hostname (e.g., "ws.okx.com")
   * @param port The port (e.g., "443")
   * @param target The target path (e.g., "/ws/v5/public")
   * @return true on success, false on failure
   */
  bool connect(const std::string &host, const std::string &port,
               const std::string &target);

  /**
   * @brief Sends a message to the WebSocket server.
   * @param message The message to send
   */
  void send(const std::string &message);

  /**
   * @brief Checks if the client is currently connected.
   * @return true if connected, false otherwise
   */
  bool is_connected() const;

  /**
   * @brief Retrieves the next received message from the queue.
   * @return The message string, or std::nullopt if queue is empty.
   */
  std::optional<std::string> get_next_message();

  /**
   * @brief Sets the callback to be invoked after a successful reconnection.
   */
  void set_on_reconnect(std::function<void()> cb);

  /**
   * @brief Closes the connection and stops the I/O thread.
   */
  void close();

  /**
   * @brief Simulates a network failure by closing the underlying TCP socket.
   * This is for testing purposes to trigger the reconnection logic.
   */
  void simulate_network_failure();

private:
  void on_connect(beast::error_code ec,
                  tcp::resolver::results_type::endpoint_type ep);
  void on_ssl_handshake(beast::error_code ec);
  void on_handshake(beast::error_code ec);
  void on_write(beast::error_code ec, std::size_t bytes_transferred);
  void on_read(beast::error_code ec, std::size_t bytes_transferred);
  void do_read();

  void run_io_context();

  // I/O Context and SSL Context
  net::io_context ioc_;
  ssl::context ssl_ctx_{ssl::context::tlsv12_client};
  std::thread io_thread_;

  // WebSocket Stream
  // We use a unique_ptr to manage the stream's lifetime, allowing
  // reconstruction on reconnect
  std::unique_ptr<websocket::stream<beast::ssl_stream<beast::tcp_stream>>> ws_;

  // Connection State
  std::atomic<bool> connected_{false};
  std::string host_;

  // Message Queues
  // Queue for outgoing messages (to be sent) - NOT USED YET, we use blocking
  // send for now or post() Queue for incoming messages (received)
  moodycamel::ConcurrentQueue<std::string> incoming_queue_;
  static constexpr size_t MAX_INCOMING_QUEUE_SIZE =
      10000; // Limit to 10k messages

  // Buffer for reading
  beast::flat_buffer buffer_;

  // Reconnection Logic
  enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    WaitingRetry
  };

  struct Stats {
    uint64_t reconnect_attempts = 0;
    uint64_t reconnect_success = 0;
  };
  Stats stats_;
  Stats get_stats() const { return stats_; }

  std::atomic<ConnectionState> connection_state_{ConnectionState::Disconnected};

  // Stored connection params for retry
  std::string port_;
  std::string target_;

  // Retry Config
  bool retry_enabled_ = true;
  int retry_max_attempts_ = 10;
  int retry_initial_delay_ms_ = 1000;
  int retry_max_delay_ms_ = 30000;
  double retry_backoff_multiplier_ = 2.0;

  int retry_count_ = 0;
  std::unique_ptr<net::steady_timer> retry_timer_;
  std::function<void()> on_reconnect_;

  void schedule_reconnect();
  void attempt_reconnect_async();

  // Async handlers for reconnection chain
  void on_resolve(beast::error_code ec, tcp::resolver::results_type results);
};
