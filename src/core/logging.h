#ifndef AERO_CORE_LOGGING_H
#define AERO_CORE_LOGGING_H

#include "../config/config.h"
#include <iostream>
#include <string>

// Helper to print timestamp
std::ostream &log_timestamp(std::ostream &os);

// Initialize logging subsystem (open files)
void logging_init(void);

// Shutdown logging subsystem (close files)
void logging_shutdown(void);

// Get stream for specific log type (or cout if file not configured)
std::ostream &get_price_log_stream();
std::ostream &get_system_log_stream();
std::ostream &get_trade_log_stream();

#define LOG_PRICE(msg)                                                         \
  do {                                                                         \
    if (app_config.log_price_enabled) {                                        \
      log_timestamp(get_price_log_stream())                                    \
          << " [PRICE] " << msg << std::endl;                                  \
    }                                                                          \
  } while (0)

#define LOG_SYSTEM(msg)                                                        \
  do {                                                                         \
    if (app_config.log_system_enabled) {                                       \
      log_timestamp(get_system_log_stream())                                   \
          << " [SYSTEM] " << msg << std::endl;                                 \
    }                                                                          \
  } while (0)

#define LOG_TRADE(msg)                                                         \
  do {                                                                         \
    if (app_config.log_trade_enabled) {                                        \
      log_timestamp(get_trade_log_stream())                                    \
          << " [TRADE] " << msg << std::endl;                                  \
    }                                                                          \
  } while (0)

#endif // AERO_CORE_LOGGING_H
