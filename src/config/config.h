#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *okx_api_key;
  const char *okx_api_secret;
  const char *okx_passphrase;

  // Trading Symbols (Multi-symbol support)
  char **okx_symbols;
  int okx_symbol_count;

  const char *bybit_api_key;
  const char *bybit_api_secret;

  char **bybit_symbols;
  int bybit_symbol_count;

  /* WebSocket Retry Configuration */
  bool ws_retry_enabled;
  int ws_retry_max_attempts;
  int ws_retry_initial_delay_ms;
  int ws_retry_max_delay_ms;
  double ws_retry_backoff_multiplier;

  /* Debug Logging */
  bool debug_log_enabled;

  /* Execution Control */
  bool enable_execution; // Set to true to enable order placement

  /* Logging Configuration */
  bool log_price_enabled;
  bool log_system_enabled;
  bool log_trade_enabled;

  /* UDP Market Data Feed */
  bool udp_feed_enabled;
  int udp_feed_port;
  char udp_feed_address[64];

  /* Log File Paths (Optional) */
  const char *log_price_file;
  const char *log_system_file;
  const char *log_trade_file;
} app_config_t;

extern app_config_t app_config;

/**
 * Load configuration from environment variables.
 * Returns 0 on success, -1 on missing required keys.
 */
int config_load(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */
