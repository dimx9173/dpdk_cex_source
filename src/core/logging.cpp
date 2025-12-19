#include "logging.h"
#include <chrono>
#include <iomanip>

#include <fstream>
#include <iostream>
#include <mutex>
#include <sys/stat.h>

static std::ofstream price_log_file;
static std::ofstream system_log_file;
static std::ofstream trade_log_file;

static std::mutex price_log_mutex;
static std::mutex system_log_mutex;
static std::mutex trade_log_mutex;

static void create_log_dir_if_needed(const char *path) {
  if (!path)
    return;
  std::string p(path);
  size_t last_slash = p.find_last_of('/');
  if (last_slash != std::string::npos) {
    std::string dir = p.substr(0, last_slash);
    struct stat st = {0};
    if (stat(dir.c_str(), &st) == -1) {
      mkdir(dir.c_str(), 0755);
    }
  }
}

void logging_init(void) {
  if (app_config.log_price_file) {
    create_log_dir_if_needed(app_config.log_price_file);
    price_log_file.open(app_config.log_price_file, std::ios::app);
  }
  if (app_config.log_system_file) {
    create_log_dir_if_needed(app_config.log_system_file);
    system_log_file.open(app_config.log_system_file, std::ios::app);
  }
  if (app_config.log_trade_file) {
    create_log_dir_if_needed(app_config.log_trade_file);
    trade_log_file.open(app_config.log_trade_file, std::ios::app);
  }
}

void logging_shutdown(void) {
  if (price_log_file.is_open())
    price_log_file.close();
  if (system_log_file.is_open())
    system_log_file.close();
  if (trade_log_file.is_open())
    trade_log_file.close();
}

std::ostream &get_price_log_stream() {
  if (price_log_file.is_open())
    return price_log_file;
  return std::cout;
}

std::ostream &get_system_log_stream() {
  if (system_log_file.is_open())
    return system_log_file;
  return std::cout;
}

std::ostream &get_trade_log_stream() {
  if (trade_log_file.is_open())
    return trade_log_file;
  return std::cout;
}

std::ostream &log_timestamp(std::ostream &os) {
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  struct tm tm_now;
  localtime_r(&time_t_now, &tm_now);
  return os << "[" << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") << "]";
}
