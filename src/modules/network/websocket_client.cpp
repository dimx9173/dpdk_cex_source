#include "websocket_client.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/sha.h>

#include <simdjson.h> // Include simdjson

using namespace aero;

// Static counter for message key for now, could be improved with better entropy
static std::random_device rd;
static std::mt19937_64 gen(rd());
static std::uniform_int_distribution<uint64_t> distrib;

WebSocketClient::WebSocketClient(
    uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
    const rte_ether_addr &src_mac, const rte_ether_addr &dst_mac,
    struct rte_mempool *mbuf_pool, const std::string &host,
    const std::string &path, MessageCallback on_message_cb,
    OrderBookManager &order_book_manager)
    : state_(WS_DISCONNECTED), tcp_client_(src_ip, src_port, dst_ip, dst_port,
                                           src_mac, dst_mac, mbuf_pool),
      ws_host_(host), ws_path_(path), on_message_callback_(on_message_cb),
      mbuf_pool_(mbuf_pool), order_book_manager_(order_book_manager) {
  fprintf(stderr, "DEBUG: Inside WebSocketClient constructor\n");
  ws_key_ = generate_websocket_key();
  fprintf(stderr, "DEBUG: WebSocketClient constructor finished\n");
}

std::vector<rte_mbuf *> WebSocketClient::connect() {
  std::vector<rte_mbuf *> out_mbufs;
  if (state_ == WS_DISCONNECTED) {
    state_ = WS_CONNECTING_TCP;
    std::cout << "WS: Initiating TCP connect." << std::endl;
    rte_mbuf *syn_pkt = tcp_client_.connect();
    if (syn_pkt) {
      out_mbufs.push_back(syn_pkt);
    } else {
      std::cerr << "WS: Failed to send SYN packet." << std::endl;
    }
  }
  return out_mbufs;
}

std::vector<rte_mbuf *> WebSocketClient::process_rx(rte_mbuf *rx_mbuf) {
  // fprintf(stderr, "DEBUG: WebSocketClient::process_rx\n");
  std::vector<rte_mbuf *> out_mbufs;

  // Delegate to TCP client first
  auto tcp_out = tcp_client_.process_rx(rx_mbuf);
  out_mbufs.insert(out_mbufs.end(), tcp_out.begin(), tcp_out.end());

  // Retrieve any encrypted data from TCP's receive buffer
  // Retrieve any encrypted data from TCP's receive buffer
  std::vector<uint8_t> tcp_rx_payload_data = tcp_client_.extract_rx_data();

  // State machine for WebSocket connection
  switch (state_) {
  case WS_CONNECTING_TCP:
    if (tcp_client_.get_state() == MicroTcp::ESTABLISHED) {
      state_ = WS_CONNECTING_TLS;
      std::cout << "WS: TCP Established. Initiating TLS handshake."
                << std::endl;
      // Move directly to TLS handshake
      // Fallthrough to WS_CONNECTING_TLS to immediately try TLS handshake
    } else {
      break; // Keep waiting if not established
    }
    // Fallthrough intended
    [[fallthrough]];

  case WS_CONNECTING_TLS: {
    // These are static to persist across poll cycles during a single handshake
    // but will be reset when connection goes to DISCONNECTED state
    static bool sni_set = false;
    static bool client_hello_sent = false;

    if (!sni_set) {
      tls_socket_.set_hostname(ws_host_);
      sni_set = true;
    }

    // On first poll: Generate ClientHello by calling do_handshake() without
    // feeding data
    if (!client_hello_sent) {
      int hs_ret = tls_socket_.do_handshake();
      // Read ClientHello from wbio and send over TCP
      std::vector<uint8_t> encrypted_to_send;
      while (tls_socket_.read_encrypted(encrypted_to_send) > 0) {
        fprintf(stderr, "\n=== CLIENTHELLO (%zu bytes) ===\n",
                encrypted_to_send.size());
        size_t dump_len = std::min((size_t)100, encrypted_to_send.size());
        for (size_t i = 0; i < dump_len; i++) {
          fprintf(stderr, "%02x ", encrypted_to_send[i]);
          if ((i + 1) % 16 == 0)
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "\n=== END CLIENTHELLO ===\n\n");

        rte_mbuf *data_pkt = tcp_client_.send_data(encrypted_to_send.data(),
                                                   encrypted_to_send.size());
        if (data_pkt)
          out_mbufs.push_back(data_pkt);
        encrypted_to_send.clear();
        client_hello_sent = true;
      }
      break; // Wait for next poll cycle to receive ServerHello
    }

    // Subsequent polls: Feed incoming data incrementally (per record) to handle
    // state transitions
    if (!tcp_rx_payload_data.empty()) {
      size_t offset = 0;
      size_t total_len = tcp_rx_payload_data.size();

      fprintf(stderr,
              "\n=== Processing %zu bytes of TLS data (Incremental Feed) ===\n",
              total_len);

      while (offset < total_len) {
        // Need at least 5 bytes for TLS header
        if (total_len - offset < 5) {
          fprintf(stderr,
                  "DEBUG: Incomplete TLS header at end of buffer. "
                  "Keeping %zu bytes.\n",
                  total_len - offset);
          // TODO: Buffer these bytes for next cycle. For now, we assume aligned
          // packets.
          break;
        }

        // Parse Header
        uint8_t type = tcp_rx_payload_data[offset];
        uint8_t v_major = tcp_rx_payload_data[offset + 1];
        uint8_t v_minor = tcp_rx_payload_data[offset + 2];
        uint16_t length = (tcp_rx_payload_data[offset + 3] << 8) |
                          tcp_rx_payload_data[offset + 4];

        // Validate TLS record type (valid: 20=CCS, 21=Alert, 22=Handshake,
        // 23=AppData)
        if (type < 20 || type > 23) {
          fprintf(stderr,
                  "DEBUG: Invalid TLS record type %02x at offset %zu. Stopping "
                  "parse.\n",
                  type, offset);
          break; // Stop parsing - reached end of valid TLS data
        }

        size_t record_size = 5 + length;

        fprintf(stderr,
                "DEBUG: Found Record at offset %zu: Type=%02x Ver=%02x%02x "
                "Len=%u (Total=%zu)\n",
                offset, type, v_major, v_minor, length, record_size);

        if (offset + record_size > total_len) {
          fprintf(stderr,
                  "DEBUG: Incomplete TLS record body. Needed %zu, have %zu. "
                  "Keeping buffer.\n",
                  record_size, total_len - offset);
          // TODO: Buffer for next cycle
          break;
        }

        // Extract single record
        const uint8_t *record_ptr = &tcp_rx_payload_data[offset];

        // CRITICAL FIX: Flush wbio BEFORE feeding next record
        // 根據 darrenjs/openssl_examples 和 Stack Overflow 研究結果：
        // 在餵送新資料到 rbio 前，必須先將 wbio 中的待發送資料發送出去
        {
          std::vector<uint8_t> pending_output;
          while (tls_socket_.read_encrypted(pending_output) > 0) {
            fprintf(stderr,
                    "DEBUG: Pre-feed flush: sending %zu bytes from wbio.\n",
                    pending_output.size());
            rte_mbuf *data_pkt = tcp_client_.send_data(pending_output.data(),
                                                       pending_output.size());
            if (data_pkt)
              out_mbufs.push_back(data_pkt);
            pending_output.clear();
          }
        }

        // Feed exact record to BIO
        fprintf(stderr, "DEBUG: Feeding record of %zu bytes to BIO...\n",
                record_size);
        tls_socket_.write_encrypted(record_ptr, record_size);

        // Advance offset
        offset += record_size;

        // PUMP HANDSHAKE AGGRESSIVELY
        // For TLS 1.3, key derivation happens after ServerHello is fully
        // processed. We must pump do_handshake() repeatedly until it stops
        // returning 0 (WANT_*).
        int hs_ret = 0;
        int pump_count = 0;
        const int MAX_PUMPS = 10; // Safety limit

        do {
          hs_ret = tls_socket_.do_handshake();
          pump_count++;
          fprintf(stderr, "DEBUG: do_handshake pump #%d returned %d\n",
                  pump_count, hs_ret);

          // CRITICAL: After each pump, flush any output (like Finished message)
          std::vector<uint8_t> encrypted_to_send;
          while (tls_socket_.read_encrypted(encrypted_to_send) > 0) {
            fprintf(stderr,
                    "DEBUG: Sending %zu bytes TLS response (pump #%d).\n",
                    encrypted_to_send.size(), pump_count);
            rte_mbuf *data_pkt = tcp_client_.send_data(
                encrypted_to_send.data(), encrypted_to_send.size());
            if (data_pkt)
              out_mbufs.push_back(data_pkt);
            encrypted_to_send.clear();
          }

          if (hs_ret == 1) {
            // Handshake complete!
            break;
          } else if (hs_ret < 0) {
            // Error - stop pumping for this record
            break;
          }
          // hs_ret == 0 means WANT_READ/WANT_WRITE, keep pumping within limits
        } while (hs_ret == 0 && pump_count < MAX_PUMPS);

        if (hs_ret == 1) {
          fprintf(stderr, "DEBUG: Handshake COMPLETE inside feed loop!\n");
          state_ = WS_HANDSHAKE_SENT;
          std::cout
              << "WS: TLS Handshake complete. Sending WebSocket handshake."
              << std::endl;
          std::vector<rte_mbuf *> ws_handshake_pkts =
              generate_websocket_handshake();
          out_mbufs.insert(out_mbufs.end(), ws_handshake_pkts.begin(),
                           ws_handshake_pkts.end());
          // Handshake done, stop processing handshake records?
          // We might have Application Data following, but for now assuming end
          // of handshake chain.
          break;
        } else if (hs_ret < 0) {
          // Error logging is done in do_handshake, but check for fatal
          // Ignoring WANT_READ/WRITE as 0 return covers them typically, or -1
          // usually means fatal Let's rely on do_handshake to print error. If
          // fatal error, might want to stop. fprintf(stderr, "DEBUG: Handshake
          // error? \n");
        }
      }
    }

    // Process handshake
    int hs_ret = tls_socket_.do_handshake();

    // Read any response (CKE, Finished, etc) from wbio
    std::vector<uint8_t> encrypted_to_send;
    while (tls_socket_.read_encrypted(encrypted_to_send) > 0) {
      fprintf(stderr, "DEBUG: Sending %zu bytes TLS handshake response.\n",
              encrypted_to_send.size());
      rte_mbuf *data_pkt = tcp_client_.send_data(encrypted_to_send.data(),
                                                 encrypted_to_send.size());
      if (data_pkt)
        out_mbufs.push_back(data_pkt);
      encrypted_to_send.clear();
    }

    if (hs_ret == 1) { // Handshake complete
      state_ = WS_HANDSHAKE_SENT;
      std::cout << "WS: TLS Handshake complete. Sending WebSocket handshake."
                << std::endl;
      std::vector<rte_mbuf *> ws_handshake_pkts =
          generate_websocket_handshake();
      out_mbufs.insert(out_mbufs.end(), ws_handshake_pkts.begin(),
                       ws_handshake_pkts.end());
    } else if (hs_ret == -1) {
      // 錯誤處理：區分致命錯誤和需要更多資料的情況
      // 如果這次沒有收到任何新資料，可能只是還在等待伺服器回應
      // 只有在收到資料後仍然失敗才報錯
      static int error_with_data_count = 0;
      if (tcp_rx_payload_data.empty()) {
        // 沒有資料，繼續等待下一個 poll cycle
        fprintf(stderr, "DEBUG: TLS handshake waiting for more data (no input "
                        "this cycle).\n");
        // 不改變狀態，繼續輪詢
      } else {
        // 有資料但仍然失敗，可能是致命錯誤
        error_with_data_count++;
        fprintf(stderr,
                "DEBUG: TLS handshake error with data present (count=%d).\n",
                error_with_data_count);
        if (error_with_data_count >= 3) {
          // 連續 3 次有資料但仍失敗，視為致命錯誤
          std::cerr << "WS: TLS Handshake fatal error after multiple attempts."
                    << std::endl;
          state_ = WS_DISCONNECTED;
          error_with_data_count = 0; // Reset for next connection attempt
        }
      }
    }
    break;
  }
  case WS_HANDSHAKE_SENT: {
    // Placeholder: need to parse HTTP response from tls_rx_buffer_
    // For now, assume success and transition
    state_ = WS_CONNECTED;
    std::cout << "WS: WebSocket handshake sent and assumed successful. State "
                 "CONNECTED."
              << std::endl;
    break;
  }
  case WS_CONNECTED: {
    if (!tcp_rx_payload_data.empty()) {
      std::vector<uint8_t> decrypted;
      int ret = tls_socket_.decrypt(tcp_rx_payload_data.data(),
                                    tcp_rx_payload_data.size(), decrypted);
      if (ret > 0 && !decrypted.empty()) {
        std::string s(decrypted.begin(), decrypted.end());
        std::cout << "DEBUG: WS RX Decrypted: " << s.substr(0, 100)
                  << (s.length() > 100 ? "..." : "") << std::endl;
        // Verify parsing later. For now, just prove we get data.
      }
    }
    break;
  }
  default:
    break;
  }
  return out_mbufs;
}

std::vector<rte_mbuf *>
WebSocketClient::send_text_message(const std::string &message) {
  std::vector<rte_mbuf *> out_mbufs;
  if (state_ != WS_CONNECTED) {
    std::cerr << "WS: Not connected, cannot send message." << std::endl;
    return out_mbufs;
  }

  // Generate WebSocket frame
  std::vector<rte_mbuf *> frames = generate_websocket_frame(
      message, 0x1, true); // Opcode 0x1 for text, FIN=true
  out_mbufs.insert(out_mbufs.end(), frames.begin(), frames.begin());
  return out_mbufs;
}

std::vector<rte_mbuf *> WebSocketClient::generate_websocket_handshake() {
  std::vector<rte_mbuf *> out_mbufs;
  std::ostringstream oss;
  oss << "GET " << ws_path_ << " HTTP/1.1\r\n"
      << "Host: " << ws_host_ << "\r\n"
      << "Upgrade: websocket\r\n"
      << "Connection: Upgrade\r\n"
      << "Sec-WebSocket-Key: " << ws_key_ << "\r\n"
      << "Sec-WebSocket-Version: 13\r\n"
      << "\r\n";

  std::string handshake_request = oss.str();

  std::vector<uint8_t> encrypted_data;
  if (tls_socket_.encrypt((const uint8_t *)handshake_request.data(),
                          handshake_request.length(), encrypted_data) <= 0) {
    std::cerr << "WS: Failed to encrypt WebSocket handshake." << std::endl;
    return out_mbufs;
  }

  rte_mbuf *data_pkt =
      tcp_client_.send_data(encrypted_data.data(), encrypted_data.size());
  if (data_pkt) {
    out_mbufs.push_back(data_pkt);
  } else {
    std::cerr << "WS: Failed to send TCP data for WebSocket handshake."
              << std::endl;
  }
  return out_mbufs;
}

std::string WebSocketClient::generate_websocket_key() {
  fprintf(stderr, "DEBUG: Generating WebSocket key\n");
  uint64_t rand_val = distrib(gen);
  fprintf(stderr, "DEBUG: Generated random value: %lu\n", rand_val);
  std::string key_bytes(reinterpret_cast<char *>(&rand_val), sizeof(rand_val));
  return sha1_base64(key_bytes);
}

std::string WebSocketClient::sha1_base64(const std::string &input) {
  uint8_t sha1_hash[SHA_DIGEST_LENGTH];
  SHA1((const uint8_t *)input.data(), input.size(), sha1_hash);

  BIO *b64 = BIO_new(BIO_f_base64());
  BIO *bio = BIO_new(BIO_s_mem());
  bio = BIO_push(b64, bio);

  BIO_write(bio, sha1_hash, SHA_DIGEST_LENGTH);
  BIO_flush(bio);

  BUF_MEM *bufferPtr;
  BIO_get_mem_ptr(bio, &bufferPtr);
  std::string result(bufferPtr->data, bufferPtr->length);

  BIO_free_all(bio);
  return result;
}

std::vector<rte_mbuf *>
WebSocketClient::generate_websocket_frame(const std::string &payload,
                                          uint8_t opcode, bool fin) {
  std::vector<rte_mbuf *> out_mbufs;
  std::vector<uint8_t> frame_data;

  uint8_t byte1 = (fin ? 0x80 : 0x00) | (opcode & 0x0F);
  frame_data.push_back(byte1);

  uint8_t byte2 = 0x80; // Masked (client always masks)
  uint64_t payload_len = payload.length();

  if (payload_len <= 125) {
    byte2 |= (uint8_t)payload_len;
    frame_data.push_back(byte2);
  } else if (payload_len <= 0xFFFF) {
    byte2 |= 126;
    frame_data.push_back(byte2);
    uint16_t len16 = rte_cpu_to_be_16((uint16_t)payload_len);
    frame_data.push_back((uint8_t)(len16 >> 8));
    frame_data.push_back((uint8_t)len16);
  } else {
    byte2 |= 127;
    frame_data.push_back(byte2);
    uint64_t len64 = rte_cpu_to_be_64(payload_len);
    for (int i = 7; i >= 0; --i) {
      frame_data.push_back((uint8_t)(len64 >> (i * 8)));
    }
  }

  // Masking key (4 bytes)
  uint8_t masking_key[4];
  for (int i = 0; i < 4; ++i) {
    masking_key[i] = distrib(gen); // Generate random masking key
    frame_data.push_back(masking_key[i]);
  }

  // Mask payload
  for (size_t i = 0; i < payload_len; ++i) {
    frame_data.push_back(payload[i] ^ masking_key[i % 4]);
  }

  std::vector<uint8_t> encrypted_data;
  if (tls_socket_.encrypt(frame_data.data(), frame_data.size(),
                          encrypted_data) <= 0) {
    std::cerr << "WS: Failed to encrypt WebSocket frame." << std::endl;
    return out_mbufs;
  }

  rte_mbuf *data_pkt =
      tcp_client_.send_data(encrypted_data.data(), encrypted_data.size());
  if (data_pkt) {
    out_mbufs.push_back(data_pkt);
  } else {
    std::cerr << "WS: Failed to send TCP data for WebSocket frame."
              << std::endl;
  }
  return out_mbufs;
}

void WebSocketClient::process_websocket_frame(const uint8_t *data, size_t len) {
  if (len < 2)
    return; // Minimum frame size

  websocket_hdr_t *ws_hdr = (websocket_hdr_t *)data;
  bool fin = (ws_hdr->fin_rsv_opcode & 0x80);
  uint8_t opcode = (ws_hdr->fin_rsv_opcode & 0x0F);
  bool masked = (ws_hdr->mask_payload_len & 0x80);
  uint8_t payload_len_byte = (ws_hdr->mask_payload_len & 0x7F);

  size_t offset = 2;
  uint64_t payload_len = 0;
  if (payload_len_byte <= 125) {
    payload_len = payload_len_byte;
  } else if (payload_len_byte == 126) {
    if (len < 4)
      return; // Not enough data for extended length
    payload_len = rte_be_to_cpu_16(*(uint16_t *)(data + offset));
    offset += 2;
  } else { // 127
    if (len < 10)
      return; // Not enough data for extended length
    payload_len = rte_be_to_cpu_64(*(uint64_t *)(data + offset));
    offset += 8;
  }

  if (len < offset + (masked ? 4 : 0) + payload_len)
    return; // Not enough data for full frame

  uint8_t masking_key[4];
  if (masked) {
    rte_memcpy(masking_key, data + offset, 4);
    offset += 4;
  }

  const uint8_t *payload_data = data + offset;
  std::string unmasked_payload;
  unmasked_payload.reserve(payload_len);

  for (size_t i = 0; i < payload_len; ++i) {
    unmasked_payload.push_back(payload_data[i] ^
                               (masked ? masking_key[i % 4] : 0));
  }

  if (opcode == 0x1) { // Text frame
    // Parse JSON using simdjson
    simdjson::dom::parser parser;
    simdjson::dom::element doc;

    try {
      doc = parser.parse(unmasked_payload);
      // std::cout << "WS: Received JSON: " << doc << std::endl; // Debug print

      // Generic JSON path for market data
      // Extract exchange, symbol, and update type (snapshot/delta)
      // This is a simplified example, real parsing would be more robust.

      std::string channel_type;
      std::string instrument_id;
      std::string message_type; // e.g., "snapshot", "update", "delta"

      try {
        // OKX specific parsing
        auto arg = doc["arg"];
        channel_type = arg["channel"].get_string().take_value();
        instrument_id = arg["instId"].get_string().take_value();
        message_type =
            doc["action"].get_string().take_value(); // "snapshot" or "update"

        auto data_array = doc["data"].get_array();
        // Use iteration to check if array is empty
        if (data_array.begin() == data_array.end()) {
          std::cerr << "WS: OKX data array is empty." << std::endl;
          return;
        }
        auto data_obj = data_array.at(0); // Take first element for simplicity

        std::vector<OrderBookLevel> new_bids;
        for (auto bid_level : data_obj["bids"].get_array()) {
          new_bids.push_back(
              {.price_int = static_cast<uint64_t>(
                   std::stod(std::string(bid_level.at(0))) * PRICE_SCALE),
               .size = std::stod(std::string(bid_level.at(1)))});
        }
        std::vector<OrderBookLevel> new_asks;
        for (auto ask_level : data_obj["asks"].get_array()) {
          new_asks.push_back(
              {.price_int = static_cast<uint64_t>(
                   std::stod(std::string(ask_level.at(0))) * PRICE_SCALE),
               .size = std::stod(std::string(ask_level.at(1)))});
        }

        bool is_snapshot = (message_type == "snapshot");
        order_book_manager_.apply_update(ExchangeId::OKX, instrument_id,
                                         new_bids, new_asks, is_snapshot);

        std::cout << "WS: OKX books-l2-tbt update for " << instrument_id
                  << std::endl;

      } catch (const simdjson::simdjson_error &e) {
        // Try Bybit parsing if OKX parsing fails
        try {
          auto data_array = doc["data"].get_array();
          // Use iteration to check if array is empty
          if (data_array.begin() == data_array.end()) {
            std::cerr << "WS: Bybit data array is empty." << std::endl;
            return;
          }
          auto data_obj = data_array.at(0); // Take first element for simplicity

          message_type =
              doc["type"].get_string().take_value(); // "snapshot" or "delta"
          instrument_id =
              doc["topic"]
                  .get_string()
                  .take_value(); // "orderbook.50.BTCUSDT" -> BTCUSDT

          std::vector<OrderBookLevel> new_bids;
          if (data_obj.at_key("b").is_array()) { // Bids (delta or snapshot)
            for (auto bid_level : data_obj["b"].get_array()) {
              new_bids.push_back(
                  {.price_int = static_cast<uint64_t>(
                       std::stod(std::string(bid_level.at(0))) * PRICE_SCALE),
                   .size = std::stod(std::string(bid_level.at(1)))});
            }
          }
          std::vector<OrderBookLevel> new_asks;
          if (data_obj.at_key("a").is_array()) { // Asks (delta or snapshot)
            for (auto ask_level : data_obj["a"].get_array()) {
              new_asks.push_back(
                  {.price_int = static_cast<uint64_t>(
                       std::stod(std::string(ask_level.at(0))) * PRICE_SCALE),
                   .size = std::stod(std::string(ask_level.at(1)))});
            }
          }

          bool is_snapshot = (message_type == "snapshot");
          order_book_manager_.apply_update(ExchangeId::BYBIT, instrument_id,
                                           new_bids, new_asks, is_snapshot);
          std::cout << "WS: Bybit orderbook.50 update for " << instrument_id
                    << std::endl;

        } catch (const simdjson::simdjson_error &bybit_e) {
          std::cerr << "WS: simdjson parsing error (OKX/Bybit): "
                    << bybit_e.what() << std::endl;
        }
      }
      on_message_callback_(unmasked_payload);
    } catch (const simdjson::simdjson_error &e) {
      std::cerr << "WS: simdjson parsing error: " << e.what() << std::endl;
    }

  } else if (opcode == 0x2) { // Binary frame
    // Handle binary if needed
    std::cerr << "WS: Received binary frame, not yet handled." << std::endl;
  } else if (opcode == 0x8) { // Close frame
    std::cout << "WS: Received close frame." << std::endl;
    state_ = WS_DISCONNECTED;
    // Respond with close frame
  } else if (opcode == 0x9) { // Ping frame
    std::cout << "WS: Received ping frame. Responding with pong." << std::endl;
    // Respond with pong frame
  } else if (opcode == 0xA) { // Pong frame
    std::cout << "WS: Received pong frame." << std::endl;
  } else {
    std::cerr << "WS: Received unknown opcode: " << (int)opcode << std::endl;
  }

  // Handle fragmented messages if fin is false (not implemented yet)
}

std::vector<rte_mbuf *>
WebSocketClient::handle_tcp_data(const std::vector<uint8_t> &tcp_payload) {
  std::vector<rte_mbuf *> out_mbufs;
  if (tcp_payload.empty())
    return out_mbufs;

  // Feed encrypted TCP data to TLS layer
  // DEBUG: Print bytes at offset 72 where second TLS record (Certificate)
  // should start
  if (tcp_payload.size() >= 82) {
    fprintf(stderr, "DEBUG: Bytes at offset 72 (Certificate record): ");
    for (size_t i = 72; i < 82; i++) {
      fprintf(stderr, "%02x ", tcp_payload[i]);
    }
    fprintf(stderr, "\n");
  }
  tls_socket_.write_encrypted(tcp_payload.data(), tcp_payload.size());

  // Try to perform TLS handshake/read decrypted data
  std::vector<rte_mbuf *> tls_mbufs =
      handle_tls_data({}); // Pass empty, as data is in internal BIO
  out_mbufs.insert(out_mbufs.end(), tls_mbufs.begin(), tls_mbufs.end());

  return out_mbufs;
}

std::vector<rte_mbuf *>
WebSocketClient::handle_tls_data(const std::vector<uint8_t> &tls_payload) {
  std::vector<rte_mbuf *> out_mbufs;

  // Decrypt incoming TLS data (from rbio_)
  std::vector<uint8_t> decrypted_data;
  if (tls_socket_.decrypt(tls_payload.data(), tls_payload.size(),
                          decrypted_data) > 0) {
    // If handshake is not complete, this might contain HTTP response for
    // handshake or just fragmented TLS records.
    if (!tls_socket_.is_handshake_complete()) {
      std::string http_response(decrypted_data.begin(), decrypted_data.end());
      if (http_response.find("101 Switching Protocols") != std::string::npos &&
          http_response.find("Sec-WebSocket-Accept:") != std::string::npos) {
        // Validate Sec-WebSocket-Accept header
        std::string accept_key =
            ws_key_ + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string expected_accept_hash = sha1_base64(accept_key);

        size_t accept_pos = http_response.find("Sec-WebSocket-Accept: ");
        if (accept_pos != std::string::npos) {
          accept_pos += std::string("Sec-WebSocket-Accept: ").length();
          size_t end_pos = http_response.find("\r\n", accept_pos);
          std::string received_accept_hash =
              http_response.substr(accept_pos, end_pos - accept_pos);
          if (received_accept_hash == expected_accept_hash) {
            state_ = WS_CONNECTED;
            std::cout << "WS: WebSocket handshake successful. State CONNECTED."
                      << std::endl;
          } else {
            std::cerr << "WS: WebSocket-Accept hash mismatch." << std::endl;
            state_ = WS_DISCONNECTED;
          }
        } else {
          std::cerr << "WS: Sec-WebSocket-Accept header not found."
                    << std::endl;
          state_ = WS_DISCONNECTED;
        }
      } else {
        std::cerr << "WS: Unexpected HTTP response during handshake: "
                  << http_response << std::endl;
        state_ = WS_DISCONNECTED;
      }
    } else {
      // Handshake complete, process as WebSocket frame
      process_websocket_frame(decrypted_data.data(), decrypted_data.size());
    }
  }

  // Read any encrypted data from TLS layer to send over TCP
  std::vector<uint8_t> encrypted_to_send;
  if (tls_socket_.read_encrypted(encrypted_to_send) > 0) {
    rte_mbuf *data_pkt = tcp_client_.send_data(encrypted_to_send.data(),
                                               encrypted_to_send.size());
    if (data_pkt)
      out_mbufs.push_back(data_pkt);
  }

  return out_mbufs;
}

// --- State Management ---

void WebSocketClient::set_state(WsState new_state) {
  WsState old_state = state_;
  state_ = new_state;
  if (on_state_change_cb_) {
    on_state_change_cb_(old_state, new_state);
  }
}

std::string
WebSocketClient::make_subscription_key(ExchangeId exchange,
                                       const std::string &instrument,
                                       const std::string &channel) const {
  std::ostringstream oss;
  oss << static_cast<int>(exchange) << ":" << instrument << ":" << channel;
  return oss.str();
}

// --- Subscription API Implementation ---

std::vector<rte_mbuf *>
WebSocketClient::subscribe(ExchangeId exchange, const std::string &instrument,
                           const std::string &channel) {
  std::vector<rte_mbuf *> out_mbufs;
  if (state_ != WS_CONNECTED) {
    std::cerr << "WS: Cannot subscribe - not connected." << std::endl;
    return out_mbufs;
  }

  std::string key = make_subscription_key(exchange, instrument, channel);
  if (subscriptions_.find(key) != subscriptions_.end()) {
    std::cerr << "WS: Already subscribed to " << key << std::endl;
    return out_mbufs;
  }

  std::string sub_msg;
  if (exchange == ExchangeId::OKX) {
    sub_msg = generate_okx_subscribe_message(instrument, channel);
  } else if (exchange == ExchangeId::BYBIT) {
    sub_msg = generate_bybit_subscribe_message(instrument, channel);
  } else {
    std::cerr << "WS: Unknown exchange for subscription." << std::endl;
    return out_mbufs;
  }

  // Track the subscription as pending
  subscriptions_[key] = {exchange, instrument, channel,
                         SubscriptionState::PENDING};

  std::cout << "WS: Sending subscription: " << sub_msg << std::endl;
  return send_text_message(sub_msg);
}

std::vector<rte_mbuf *>
WebSocketClient::unsubscribe(ExchangeId exchange, const std::string &instrument,
                             const std::string &channel) {
  std::vector<rte_mbuf *> out_mbufs;
  if (state_ != WS_CONNECTED) {
    return out_mbufs;
  }

  std::string key = make_subscription_key(exchange, instrument, channel);
  auto it = subscriptions_.find(key);
  if (it == subscriptions_.end()) {
    std::cerr << "WS: Not subscribed to " << key << std::endl;
    return out_mbufs;
  }

  std::ostringstream oss;
  if (exchange == ExchangeId::OKX) {
    oss << R"({"op":"unsubscribe","args":[{"channel":")" << channel
        << R"(","instId":")" << instrument << R"("}]})";
  } else if (exchange == ExchangeId::BYBIT) {
    oss << R"({"op":"unsubscribe","args":[")" << channel << "." << instrument
        << R"("]})";
  }

  subscriptions_.erase(it);
  return send_text_message(oss.str());
}

// --- Subscription Message Generators ---

std::string WebSocketClient::generate_okx_subscribe_message(
    const std::string &instrument, const std::string &channel) const {
  std::ostringstream oss;
  oss << R"({"op":"subscribe","args":[{"channel":")" << channel
      << R"(","instId":")" << instrument << R"("}]})";
  return oss.str();
}

std::string WebSocketClient::generate_bybit_subscribe_message(
    const std::string &instrument, const std::string &channel) const {
  std::ostringstream oss;
  oss << R"({"op":"subscribe","args":[")" << channel << "." << instrument
      << R"("]})";
  return oss.str();
}

// --- Heartbeat Management ---

void WebSocketClient::handle_ping(ExchangeId exchange,
                                  const std::string &ping_data) {
  last_ping_received_ = std::chrono::steady_clock::now();
  pending_pong_ = true;
  (void)ping_data; // May be used for Bybit timestamp echo
}

std::vector<rte_mbuf *>
WebSocketClient::generate_pong(ExchangeId exchange,
                               const std::string &ping_data) {
  std::string pong_msg;
  if (exchange == ExchangeId::OKX) {
    pong_msg = "pong";
  } else if (exchange == ExchangeId::BYBIT) {
    pong_msg = R"({"op":"pong"})";
  }
  pending_pong_ = false;
  return send_text_message(pong_msg);
}

std::vector<rte_mbuf *> WebSocketClient::check_heartbeat() {
  std::vector<rte_mbuf *> out_mbufs;
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                     now - last_ping_received_)
                     .count();

  // Check for stale connection (no ping in 60 seconds)
  if (state_ == WS_CONNECTED && elapsed > 60) {
    std::cerr << "WS: Connection stale - no ping received for " << elapsed
              << "s" << std::endl;
    // Could trigger reconnection here
  }
  return out_mbufs;
}

// --- Connection Lifecycle ---

std::vector<rte_mbuf *> WebSocketClient::disconnect() {
  std::vector<rte_mbuf *> out_mbufs;
  if (state_ == WS_DISCONNECTED) {
    return out_mbufs;
  }

  // Send WebSocket close frame (opcode 0x8)
  std::vector<rte_mbuf *> close_frame = generate_websocket_frame("", 0x8, true);
  out_mbufs.insert(out_mbufs.end(), close_frame.begin(), close_frame.end());

  subscriptions_.clear();
  set_state(WS_DISCONNECTED);

  std::cout << "WS: Gracefully disconnected." << std::endl;
  return out_mbufs;
}

// --- Subscription Response Parsing ---

void WebSocketClient::parse_subscription_response(const std::string &json_msg) {
  simdjson::dom::parser parser;
  try {
    simdjson::dom::element doc = parser.parse(json_msg);

    // OKX format: {"event":"subscribe","arg":{"channel":"...","instId":"..."}}
    // or error: {"event":"error","code":"...","msg":"..."}
    std::string_view event;
    if (doc["event"].get(event) == simdjson::SUCCESS) {
      if (event == "subscribe") {
        auto arg = doc["arg"];
        std::string_view channel_sv, instId_sv;
        arg["channel"].get(channel_sv);
        arg["instId"].get(instId_sv);
        std::string key = make_subscription_key(
            ExchangeId::OKX, std::string(instId_sv), std::string(channel_sv));
        auto it = subscriptions_.find(key);
        if (it != subscriptions_.end()) {
          it->second.state = SubscriptionState::CONFIRMED;
          if (on_subscription_cb_) {
            on_subscription_cb_(it->second, true);
          }
        }
      } else if (event == "error") {
        std::cerr << "WS: OKX subscription error: " << json_msg << std::endl;
      }
    }

    // Bybit format:
    // {"success":true,"ret_msg":"subscribe","op":"subscribe","conn_id":"..."}
    bool success = false;
    if (doc["success"].get(success) == simdjson::SUCCESS && success) {
      std::cout << "WS: Bybit subscription confirmed." << std::endl;
      // Note: Bybit doesn't echo the channel in response, need req_id for
      // matching
    }

  } catch (const simdjson::simdjson_error &e) {
    // Not a subscription response, ignore
  }
}

// --- Reconnection Implementation ---

void WebSocketClient::initiate_reconnect() {
  if (reconnect_pending_) {
    return; // Already pending
  }

  // Save current subscriptions for restoration
  saved_subscriptions_.clear();
  for (const auto &[key, sub] : subscriptions_) {
    if (sub.state == SubscriptionState::CONFIRMED) {
      saved_subscriptions_.push_back(sub);
    }
  }
  subscriptions_.clear();

  reconnect_pending_ = true;
  reconnect_attempts_++;

  uint32_t backoff_ms = calculate_backoff_ms();
  next_reconnect_time_ =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(backoff_ms);

  std::cout << "WS: Initiating reconnect (attempt " << reconnect_attempts_
            << ") in " << backoff_ms << "ms" << std::endl;
}

uint32_t WebSocketClient::calculate_backoff_ms() const {
  // Exponential backoff with jitter: base * 2^attempts + random(0, base)
  uint32_t backoff =
      BASE_RECONNECT_DELAY_MS * (1u << std::min(reconnect_attempts_, 10u));
  // Cap at 30 seconds
  backoff = std::min(backoff, 30000u);
  return backoff;
}

std::vector<rte_mbuf *> WebSocketClient::try_reconnect() {
  std::vector<rte_mbuf *> out_mbufs;

  if (!reconnect_pending_) {
    return out_mbufs;
  }

  auto now = std::chrono::steady_clock::now();
  if (now < next_reconnect_time_) {
    return out_mbufs; // Not time yet
  }

  if (reconnect_attempts_ > MAX_RECONNECT_ATTEMPTS) {
    std::cerr << "WS: Max reconnect attempts (" << MAX_RECONNECT_ATTEMPTS
              << ") reached. Giving up." << std::endl;
    reconnect_pending_ = false;
    return out_mbufs;
  }

  std::cout << "WS: Attempting reconnect..." << std::endl;
  reconnect_pending_ = false;

  // Clear state and attempt fresh connection
  set_state(WS_DISCONNECTED);
  ws_key_ = generate_websocket_key();

  return connect();
}

std::vector<rte_mbuf *> WebSocketClient::restore_subscriptions() {
  std::vector<rte_mbuf *> out_mbufs;

  if (state_ != WS_CONNECTED) {
    return out_mbufs;
  }

  if (saved_subscriptions_.empty()) {
    return out_mbufs;
  }

  std::cout << "WS: Restoring " << saved_subscriptions_.size()
            << " subscriptions after reconnect..." << std::endl;

  // Reset reconnect counters on successful reconnection
  reconnect_attempts_ = 0;

  for (const auto &sub : saved_subscriptions_) {
    std::vector<rte_mbuf *> sub_mbufs =
        subscribe(sub.exchange, sub.instrument, sub.channel);
    out_mbufs.insert(out_mbufs.end(), sub_mbufs.begin(), sub_mbufs.end());
  }

  saved_subscriptions_.clear();
  return out_mbufs;
}
