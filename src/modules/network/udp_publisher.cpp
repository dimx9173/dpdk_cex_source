#include "modules/network/udp_publisher.h"
#include "core/logging.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <rte_byteorder.h>
#include <sys/socket.h>
#include <unistd.h>

namespace aero {

UdpPublisher::UdpPublisher() : socket_fd_(-1), target_port_(0) {}

UdpPublisher::~UdpPublisher() { close(); }

bool UdpPublisher::init(const std::string &address, int port) {
  target_address_ = address;
  target_port_ = port;

  // Create UDP socket
  socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd_ < 0) {
    LOG_SYSTEM("UdpPublisher: Failed to create socket: " << strerror(errno));
    return false;
  }

  // Set Non-Blocking
  int flags = fcntl(socket_fd_, F_GETFL, 0);
  if (flags == -1) {
    LOG_SYSTEM("UdpPublisher: Failed to get socket flags");
    ::close(socket_fd_);
    socket_fd_ = -1;
    return false;
  }
  if (fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) == -1) {
    LOG_SYSTEM("UdpPublisher: Failed to set non-blocking");
    ::close(socket_fd_);
    socket_fd_ = -1;
    return false;
  }

  LOG_SYSTEM("UdpPublisher: Initialized broadcasting to " << address << ":"
                                                          << port);
  return true;
}

void UdpPublisher::close() {
  if (socket_fd_ >= 0) {
    ::close(socket_fd_);
    socket_fd_ = -1;
  }
}

void UdpPublisher::publish(const ParsedOrderBook &book,
                           ExchangeId exchange_id) {
  if (socket_fd_ < 0)
    return;

  serialize_and_send(book, exchange_id);
}

void UdpPublisher::serialize_and_send(const ParsedOrderBook &book,
                                      ExchangeId exchange_id) {
  // Use thread_local buffer to avoid per-call allocation and make it
  // thread-safe across multiple threads (e.g., separate exchange threads)
  thread_local std::vector<uint8_t> buffer;
  buffer.clear();
  buffer.reserve(1024); // Sufficient for typical OrderBook update

  // 1. Prepare Header
  UdpMarketHeader header;
  header.magic = htonl(UDP_FEED_MAGIC);
  header.version = htons(UDP_FEED_VERSION);
  header.msg_type = book.is_snapshot ? 1 : 2; // Snapshot=1, Delta=2
  header.exchange_id = static_cast<uint8_t>(exchange_id);

  // Use current monotonic time for gateway timestamp
  auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
  header.timestamp_ns = rte_cpu_to_be_64(static_cast<uint64_t>(now_ns));

  const std::string &symbol = book.instrument;

  header.symbol_len = htonl(static_cast<uint32_t>(symbol.length()));
  header.bid_count = htons(static_cast<uint16_t>(book.bids.size()));
  header.ask_count = htons(static_cast<uint16_t>(book.asks.size()));

  // 2. Push Header
  const uint8_t *header_ptr = reinterpret_cast<const uint8_t *>(&header);
  buffer.insert(buffer.end(), header_ptr, header_ptr + sizeof(header));

  // 3. Push Symbol
  if (!symbol.empty()) {
    buffer.insert(buffer.end(), symbol.begin(), symbol.end());
  }

  // 4. Push Bids
  for (const auto &level : book.bids) {
    UdpPriceLevel p_level;
    p_level.price_int = rte_cpu_to_be_64(level.price_int);

    // Swap double
    uint64_t qty_bits;
    std::memcpy(&qty_bits, &level.size, sizeof(uint64_t));
    qty_bits = rte_cpu_to_be_64(qty_bits);
    std::memcpy(&p_level.quantity, &qty_bits, sizeof(uint64_t));

    const uint8_t *level_ptr = reinterpret_cast<const uint8_t *>(&p_level);
    buffer.insert(buffer.end(), level_ptr, level_ptr + sizeof(p_level));
  }

  // 5. Push Asks
  for (const auto &level : book.asks) {
    UdpPriceLevel p_level;
    p_level.price_int = rte_cpu_to_be_64(level.price_int);

    // Swap double
    uint64_t qty_bits;
    std::memcpy(&qty_bits, &level.size, sizeof(uint64_t));
    qty_bits = rte_cpu_to_be_64(qty_bits);
    std::memcpy(&p_level.quantity, &qty_bits, sizeof(uint64_t));

    const uint8_t *level_ptr = reinterpret_cast<const uint8_t *>(&p_level);
    buffer.insert(buffer.end(), level_ptr, level_ptr + sizeof(p_level));
  }

  // 6. Send
  struct sockaddr_in dest_addr;
  memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(target_port_);

  // Note: inet_pton is relatively fast, but ideally we cache the sockaddr.
  // Given target_address_ might change on re-init, parsing here is safer.
  if (inet_pton(AF_INET, target_address_.c_str(), &dest_addr.sin_addr) <= 0) {
    return;
  }

  ssize_t sent = sendto(socket_fd_, buffer.data(), buffer.size(), 0,
                        (struct sockaddr *)&dest_addr, sizeof(dest_addr));

  if (sent < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      // Error handling (omitted for hot-path)
    }
  }
}

} // namespace aero
