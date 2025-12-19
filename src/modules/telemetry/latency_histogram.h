#ifndef AERO_LATENCY_HISTOGRAM_H
#define AERO_LATENCY_HISTOGRAM_H

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <rte_cycles.h>
#include <stdint.h>
#include <vector>

namespace aero {

// A simple fixed-bucket linear/log histogram for latency tracking
// Focusing on performance (minimal locking, atomics where possible)
class LatencyHistogram {
public:
  static const int NUM_BUCKETS = 256;
  static const int MAX_LATENCY_US =
      1000; // Track up to 1000us specifically well

  LatencyHistogram() {
    for (int i = 0; i < NUM_BUCKETS; ++i) {
      buckets_[i].store(0, std::memory_order_relaxed);
    }
    total_count_.store(0, std::memory_order_relaxed);
  }

  // Record latency in CPU cycles
  inline void record(uint64_t cycles) {
    // Convert to microseconds approximately for bucketing,
    // to keep bucket logic simple independent of TSC frequency for now,
    // or just use raw cycles?
    // Better to use cycles for speed, but need conversion for human readability
    // later. Let's use raw cycles for the buckets to be super fast! But TSC hz
    // varies. Let's pre-calculate a scale factor or just use microseconds.
    // rte_get_timer_hz() is relatively expensive to call every time? No, it's
    // variable. Let's assume we want microseconds.

    // Fast approximation: assume 2GHz+ CPU, so 2000 cycles = 1us.
    // To be precise, let's just bucket by "units" where unit is somewhat
    // arbitrary but stable. Or we can just do:

    static uint64_t tsc_hz = rte_get_timer_hz();
    double us = (double)cycles * 1000000.0 / tsc_hz;

    int bucket_idx = 0;
    if (us < 1.0) {
      // sub-microsecond: bucket 0 to 9 (0.0 - 0.9)
      bucket_idx = (int)(us * 10);
    } else if (us < 100.0) {
      // 1us - 99us: bucket 10 to 109
      bucket_idx = 10 + (int)us;
    } else {
      // >= 100us: Log scale or clamped
      // Simple clamp for HFT context (if > 150us we largely failed)
      bucket_idx = 110 + (int)(us / 10.0);
    }

    if (bucket_idx >= NUM_BUCKETS)
      bucket_idx = NUM_BUCKETS - 1;
    if (bucket_idx < 0)
      bucket_idx = 0;

    buckets_[bucket_idx].fetch_add(1, std::memory_order_relaxed);
    total_count_.fetch_add(1, std::memory_order_relaxed);
  }

  void print_stats() {
    uint64_t total = total_count_.load(std::memory_order_relaxed);
    if (total == 0)
      return;

    printf("Latency Stats (Total Samples: %lu)\n", total);
    // Simplified P50/P99 estimation by traversing buckets

    uint64_t current_count = 0;
    uint64_t p50_threshold = total * 0.5;
    uint64_t p99_threshold = total * 0.99;

    int p50_bucket = -1;
    int p99_bucket = -1;

    for (int i = 0; i < NUM_BUCKETS; ++i) {
      current_count += buckets_[i].load(std::memory_order_relaxed);
      if (p50_bucket == -1 && current_count >= p50_threshold)
        p50_bucket = i;
      if (p99_bucket == -1 && current_count >= p99_threshold)
        p99_bucket = i;
    }

    printf("  P50 ~ %s\n", bucket_to_string(p50_bucket).c_str());
    printf("  P99 ~ %s\n", bucket_to_string(p99_bucket).c_str());
    fflush(stdout);
  }

  // Global instance
  static LatencyHistogram &instance() {
    static LatencyHistogram instance;
    return instance;
  }

private:
  std::atomic<uint64_t> buckets_[NUM_BUCKETS];
  std::atomic<uint64_t> total_count_;

  std::string bucket_to_string(int idx) {
    char buf[64];
    if (idx < 10) {
      snprintf(buf, sizeof(buf), "%.1f us", idx / 10.0);
    } else if (idx < 110) {
      snprintf(buf, sizeof(buf), "%d us", idx - 10);
    } else {
      snprintf(buf, sizeof(buf), "> %d us", (idx - 110) * 10 + 100);
    }
    return std::string(buf);
  }
};

} // namespace aero

#endif // AERO_LATENCY_HISTOGRAM_H
