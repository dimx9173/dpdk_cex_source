#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

app_config_t app_config = {0};

static const char *get_required_env(const char *name, bool *missing) {
  const char *val = getenv(name);
  if (!val) {
    fprintf(stderr, "Error: Missing required environment variable: %s\n", name);
    if (missing)
      *missing = true;
  }
  return val;
}

static const char *get_optional_env(const char *name, const char *default_val) {
  const char *val = getenv(name);
  return val ? val : default_val;
}

// Helper to parse comma-separated symbols
static int parse_csv_symbols(const char *env_val, char ***out_symbols,
                             int *out_count) {
  if (!env_val || strlen(env_val) == 0) {
    return 0;
  }

  // First pass: count delimiters
  int count = 1;
  const char *p = env_val;
  while (*p) {
    if (*p == ',')
      count++;
    p++;
  }

  *out_symbols = (char **)malloc(sizeof(char *) * count);
  if (!*out_symbols)
    return -1;

  char *copy = strdup(env_val);
  if (!copy) {
    free(*out_symbols);
    return -1;
  }

  int idx = 0;
  char *token = strtok(copy, ",");
  while (token) {
    (*out_symbols)[idx++] = strdup(token);
    token = strtok(NULL, ",");
  }

  free(copy);
  *out_count = count;
  return 0;
}

int config_load(void) {
  bool missing = false;

  app_config.okx_api_key = get_required_env("OKX_API_KEY", &missing);
  app_config.okx_api_secret = get_required_env("OKX_API_SECRET", &missing);
  app_config.okx_passphrase = get_required_env("OKX_PASSPHRASE", &missing);
  app_config.bybit_api_key = get_required_env("BYBIT_API_KEY", &missing);
  app_config.bybit_api_secret = get_required_env("BYBIT_API_SECRET", &missing);

  // Load Symbols
  // Load Symbols
  const char *okx_syms_env = get_optional_env("TRADING_SYMBOLS_OKX", NULL);
  if (okx_syms_env) {
    parse_csv_symbols(okx_syms_env, &app_config.okx_symbols,
                      &app_config.okx_symbol_count);
  } else {
    // Default OKX Symbols
    const char *defaults[] = {"ETH-USDT-SWAP", "XRP-USDT-SWAP", "SOL-USDT-SWAP",
                              "TRX-USDT-SWAP", "DOGE-USDT-SWAP"};
    int count = 5;
    app_config.okx_symbols = (char **)malloc(sizeof(char *) * count);
    for (int i = 0; i < count; i++) {
      app_config.okx_symbols[i] = strdup(defaults[i]);
    }
    app_config.okx_symbol_count = count;
  }

  const char *bybit_syms_env = get_optional_env("TRADING_SYMBOLS_BYBIT", NULL);
  if (bybit_syms_env) {
    parse_csv_symbols(bybit_syms_env, &app_config.bybit_symbols,
                      &app_config.bybit_symbol_count);
  } else {
    // Default Bybit Symbols
    const char *defaults[] = {"ETHUSDT", "XRPUSDT", "SOLUSDT", "TRXUSDT",
                              "DOGEUSDT"};
    int count = 5;
    app_config.bybit_symbols = (char **)malloc(sizeof(char *) * count);
    for (int i = 0; i < count; i++) {
      app_config.bybit_symbols[i] = strdup(defaults[i]);
    }
    app_config.bybit_symbol_count = count;
  }

  // WebSocket Retry Configuration
  const char *retry_enabled_str = get_optional_env("WS_RETRY_ENABLED", "true");
  app_config.ws_retry_enabled = (strcasecmp(retry_enabled_str, "true") == 0 ||
                                 strcmp(retry_enabled_str, "1") == 0);

  const char *max_attempts_str =
      get_optional_env("WS_RETRY_MAX_ATTEMPTS", "10");
  app_config.ws_retry_max_attempts = atoi(max_attempts_str);

  const char *init_delay_str =
      get_optional_env("WS_RETRY_INITIAL_DELAY_MS", "1000");
  app_config.ws_retry_initial_delay_ms = atoi(init_delay_str);

  const char *max_delay_str =
      get_optional_env("WS_RETRY_MAX_DELAY_MS", "30000");
  app_config.ws_retry_max_delay_ms = atoi(max_delay_str);

  const char *multiplier_str =
      get_optional_env("WS_RETRY_BACKOFF_MULTIPLIER", "2.0");
  app_config.ws_retry_backoff_multiplier = strtod(multiplier_str, NULL);

  if (missing) {
    fprintf(stderr, "Configuration failed: one or more required environment "
                    "variables are missing.\n");
    return -1;
  }

  printf("Configuration loaded (relaxed check).\n");

  // Debug Logging
  const char *debug_log_str = get_optional_env("DEBUG_LOG_ENABLED", "false");
  app_config.debug_log_enabled = (strcasecmp(debug_log_str, "true") == 0 ||
                                  strcmp(debug_log_str, "1") == 0);

  // Execution Control (default: disabled)
  const char *exec_str = get_optional_env("ENABLE_EXECUTION", "false");
  app_config.enable_execution =
      (strcasecmp(exec_str, "true") == 0 || strcmp(exec_str, "1") == 0);

  // Structured Logging Configuration (default: enabled)
  const char *log_price = get_optional_env("LOG_PRICE_ENABLED", "true");
  app_config.log_price_enabled =
      (strcasecmp(log_price, "true") == 0 || strcmp(log_price, "1") == 0);

  const char *log_system = get_optional_env("LOG_SYSTEM_ENABLED", "true");
  app_config.log_system_enabled =
      (strcasecmp(log_system, "true") == 0 || strcmp(log_system, "1") == 0);

  const char *log_trade = get_optional_env("LOG_TRADE_ENABLED", "true");
  app_config.log_trade_enabled =
      (strcasecmp(log_trade, "true") == 0 || strcmp(log_trade, "1") == 0);

  // UDP Feed Configuration
  const char *udp_enabled_str = get_optional_env("UDP_FEED_ENABLED", "true");
  app_config.udp_feed_enabled = (strcasecmp(udp_enabled_str, "true") == 0 ||
                                 strcmp(udp_enabled_str, "1") == 0);

  const char *udp_port_str = get_optional_env("UDP_FEED_PORT", "13988");
  app_config.udp_feed_port = atoi(udp_port_str);

  const char *udp_addr_str = get_optional_env("UDP_FEED_ADDRESS", "127.0.0.1");
  strncpy(app_config.udp_feed_address, udp_addr_str,
          sizeof(app_config.udp_feed_address) - 1);
  app_config.udp_feed_address[sizeof(app_config.udp_feed_address) - 1] = '\0';

  // Log File Paths (default: logs/ directory)
  app_config.log_price_file =
      get_optional_env("LOG_PRICE_FILE", "logs/price.log");
  app_config.log_system_file =
      get_optional_env("LOG_SYSTEM_FILE", "logs/system.log");
  app_config.log_trade_file =
      get_optional_env("LOG_TRADE_FILE", "logs/trade.log");

  return 0;
}
