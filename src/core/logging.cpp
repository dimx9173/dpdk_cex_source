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

static void perform_log_rotation_check();

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

  // Check for rotation on startup
  perform_log_rotation_check();
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

// --- Log Rotation Implementation ---

#include <algorithm>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <vector>

static void prune_old_logs(const char *filepath) {
  if (!app_config.log_max_files || app_config.log_max_files <= 0)
    return;

  std::string full_path(filepath);
  size_t last_slash = full_path.find_last_of('/');
  std::string dir_path =
      (last_slash == std::string::npos) ? "." : full_path.substr(0, last_slash);
  std::string filename = (last_slash == std::string::npos)
                             ? full_path
                             : full_path.substr(last_slash + 1);

  DIR *dir = opendir(dir_path.c_str());
  if (!dir)
    return;

  struct LogFile {
    std::string name;
    time_t mtime;
  };
  std::vector<LogFile> rotated_files;

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    std::string d_name(ent->d_name);
    // Check if file starts with base filename and has a hyphen (rotated)
    // Format: <name>-<YYYY-MM-DD-HH>.log
    if (d_name.find(filename + "-") == 0) {
      struct stat st;
      std::string full_name = dir_path + "/" + d_name;
      if (stat(full_name.c_str(), &st) == 0) {
        rotated_files.push_back({full_name, st.st_mtime});
      }
    }
  }
  closedir(dir);

  // Sort by modification time (oldest first)
  std::sort(
      rotated_files.begin(), rotated_files.end(),
      [](const LogFile &a, const LogFile &b) { return a.mtime < b.mtime; });

  // Delete oldest if we have more than max_files
  if (rotated_files.size() > (size_t)app_config.log_max_files) {
    size_t to_delete = rotated_files.size() - app_config.log_max_files;
    for (size_t i = 0; i < to_delete; i++) {
      remove(rotated_files[i].name.c_str());
      std::cout << "[System] Rotated log deleted: " << rotated_files[i].name
                << std::endl;
    }
  }
}

static void check_and_rotate_log(std::ofstream &file_stream,
                                 const char *filepath, std::mutex &mtx) {
  if (!app_config.log_rotation_enabled || !filepath)
    return;

  struct stat st;
  if (stat(filepath, &st) != 0)
    return; // File doesn't exist yet

  auto now = std::chrono::system_clock::now();
  auto mtime = std::chrono::system_clock::from_time_t(st.st_mtime);
  auto duration = std::chrono::duration_cast<std::chrono::hours>(now - mtime);

  if (duration.count() >= app_config.log_rotation_hours) {
    std::lock_guard<std::mutex> lock(mtx);

    file_stream.close();

    // Generate timestamp for rotation
    auto time_t_mtime = st.st_mtime;
    struct tm tm_mtime;
    localtime_r(&time_t_mtime, &tm_mtime);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "-%Y-%m-%d-%H", &tm_mtime);

    std::string new_name = std::string(filepath) + timestamp + ".log";
    rename(filepath, new_name.c_str());

    file_stream.open(filepath, std::ios::app);

    std::cout << "[System] Log rotated: " << new_name << std::endl;

    prune_old_logs(filepath);
  }
}

static void perform_log_rotation_check() {
  if (app_config.log_price_file)
    check_and_rotate_log(price_log_file, app_config.log_price_file,
                         price_log_mutex);
  if (app_config.log_system_file)
    check_and_rotate_log(system_log_file, app_config.log_system_file,
                         system_log_mutex);
  if (app_config.log_trade_file)
    check_and_rotate_log(trade_log_file, app_config.log_trade_file,
                         trade_log_mutex);
}
