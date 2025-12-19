#ifndef _TLS_SOCKET_H_
#define _TLS_SOCKET_H_

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <string>
#include <vector>

class TlsSocket {
public:
  TlsSocket();
  ~TlsSocket();

  // Perform TLS handshake
  int do_handshake();

  // Set hostname for SNI (Server Name Indication) - required for most HTTPS
  // servers
  void set_hostname(const std::string &hostname);

  // Encrypt data to be sent over the wire
  int encrypt(const uint8_t *in_data, size_t in_len,
              std::vector<uint8_t> &out_data);

  // Decrypt data received from the wire
  int decrypt(const uint8_t *in_data, size_t in_len,
              std::vector<uint8_t> &out_data);

  // Read encrypted data from the internal BIO to send over TCP
  int read_encrypted(std::vector<uint8_t> &out_data);

  // Write encrypted data from TCP to the internal BIO for decryption
  int write_encrypted(const uint8_t *in_data, size_t in_len);

  bool is_handshake_complete() const;

private:
  SSL_CTX *ctx_;
  SSL *ssl_;
  BIO *rbio_; // Read BIO for encrypted data
  BIO *wbio_; // Write BIO for encrypted data
};

#endif // _TLS_SOCKET_H_