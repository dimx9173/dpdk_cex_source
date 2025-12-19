#include "tls_socket.h"
#include <iostream>
#include <stdexcept>

TlsSocket::TlsSocket()
    : ctx_(nullptr), ssl_(nullptr), rbio_(nullptr), wbio_(nullptr) {
  // Initialize OpenSSL
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();

  ctx_ = SSL_CTX_new(TLS_client_method());
  if (!ctx_) {
    ERR_print_errors_fp(stderr);
    throw std::runtime_error("Failed to create SSL_CTX");
  }

  // 允許 TLS 1.2 和 TLS 1.3（OKX 伺服器傾向 TLS 1.3）
  SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
  // 不限制最大版本，允許 TLS 1.3
  // Disable verification for now (easier testing). TODO: re-enable with proper
  // CA setup.
  SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);
  // SSL_CTX_set_default_verify_paths(ctx_); // Load default CA certificates

  ssl_ = SSL_new(ctx_);
  if (!ssl_) {
    ERR_print_errors_fp(stderr);
    throw std::runtime_error("Failed to create SSL object");
  }

  // Create BIOs for in-memory encryption/decryption
  rbio_ = BIO_new(BIO_s_mem());
  wbio_ = BIO_new(BIO_s_mem());
  if (!rbio_ || !wbio_) {
    ERR_print_errors_fp(stderr);
    throw std::runtime_error("Failed to create BIOs");
  }

  SSL_set_bio(ssl_, rbio_, wbio_);
  SSL_set_connect_state(ssl_); // Set for client-side connection
}

TlsSocket::~TlsSocket() {
  if (ssl_)
    SSL_free(ssl_);
  if (ctx_)
    SSL_CTX_free(ctx_);
  // BIOs are freed when SSL is freed if set with SSL_set_bio
}

void TlsSocket::set_hostname(const std::string &hostname) {
  // DISABLED: SSL_clear and BIO_reset may disrupt TLS 1.3 key derivation
  // These are only needed if reusing the SSL object across multiple connections
  // For single connection per object lifetime, skip these:
  // SSL_clear(ssl_);
  // BIO_reset(rbio_);
  // BIO_reset(wbio_);

  // Set SNI (Server Name Indication) - required by Cloudflare and most CDNs
  SSL_set_tlsext_host_name(ssl_, hostname.c_str());
  // Also set hostname for certificate verification (if re-enabled later)
  SSL_set1_host(ssl_, hostname.c_str());

  // SSL_set_connect_state already called in constructor - no need to reset
}

int TlsSocket::do_handshake() {
  int ret = SSL_do_handshake(ssl_);
  if (ret <= 0) {
    int err = SSL_get_error(ssl_, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      // Handshake is not complete, needs more I/O
      return 0; // Indicate needs more I/O
    } else {
      ERR_print_errors_fp(stderr);
      std::cerr << "TLS Handshake failed: " << err << std::endl;
      return -1; // Indicate error
    }
  }
  return 1; // Handshake complete
}

int TlsSocket::encrypt(const uint8_t *in_data, size_t in_len,
                       std::vector<uint8_t> &out_data) {
  if (!SSL_is_init_finished(ssl_)) {
    std::cerr << "TLS not initialized. Handshake not complete." << std::endl;
    return -1;
  }

  int ret = SSL_write(ssl_, in_data, in_len);
  if (ret <= 0) {
    int err = SSL_get_error(ssl_, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      // Needs more I/O to encrypt (unlikely for SSL_write unless BIO is full)
      return 0;
    } else {
      ERR_print_errors_fp(stderr);
      std::cerr << "SSL_write failed: " << err << std::endl;
      return -1;
    }
  }
  return ret; // Number of bytes encrypted
}

int TlsSocket::decrypt(const uint8_t *in_data, size_t in_len,
                       std::vector<uint8_t> &out_data) {
  // Write encrypted data to BIO for SSL_read to process
  int write_ret = BIO_write(rbio_, in_data, in_len);
  if (write_ret <= 0) {
    ERR_print_errors_fp(stderr);
    std::cerr << "BIO_write to rbio_ failed." << std::endl;
    return -1;
  }

  out_data.resize(in_len); // Max possible decrypted size is in_len
  int ret = SSL_read(ssl_, out_data.data(), out_data.size());
  if (ret <= 0) {
    int err = SSL_get_error(ssl_, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      out_data.clear(); // No decrypted data yet
      return 0;         // Indicate needs more I/O
    } else {
      ERR_print_errors_fp(stderr);
      std::cerr << "SSL_read failed: " << err << std::endl;
      return -1; // Indicate error
    }
  }
  out_data.resize(ret); // Adjust size to actual decrypted bytes
  return ret;           // Number of bytes decrypted
}

int TlsSocket::read_encrypted(std::vector<uint8_t> &out_data) {
  // Read encrypted data from wbio_ to send over TCP
  size_t pending = BIO_pending(wbio_);
  if (pending > 0) {
    out_data.resize(pending);
    int ret = BIO_read(wbio_, out_data.data(), pending);
    if (ret <= 0) {
      ERR_print_errors_fp(stderr);
      std::cerr << "BIO_read from wbio_ failed." << std::endl;
      return -1;
    }
    out_data.resize(ret);
    return ret;
  }
  return 0; // No pending encrypted data
}

int TlsSocket::write_encrypted(const uint8_t *in_data, size_t in_len) {
  // Write encrypted data from TCP to rbio_ for decryption
  int ret = BIO_write(rbio_, in_data, in_len);
  if (ret <= 0) {
    ERR_print_errors_fp(stderr);
    std::cerr << "BIO_write to rbio_ failed." << std::endl;
    return -1;
  }
  return ret;
}

bool TlsSocket::is_handshake_complete() const {
  return SSL_is_init_finished(ssl_);
}
