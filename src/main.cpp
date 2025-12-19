/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Project AERO.
 */

#include "core/logging.h"
#include "modules/network/network_utils.h"
#include <iostream>
#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ring.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "classifier/classifier.h"
#include "config.h"
#include "forwarding.h"
#include "init.h"
#include "modules/exchange/bybit_connection.h"
#include "modules/exchange/okx_connection.h"

#include "modules/market_data/order_book.h"
#include "modules/network/udp_publisher.h"
#include <arpa/inet.h>

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define RING_SIZE 2048
#define HFT_TARGET_PORT_OKX 8443
#define HFT_TARGET_PORT_BYBIT 443

// Global Ring Buffer for Fast Path
struct rte_ring *hft_ring = NULL;

volatile bool force_quit = false;

static void signal_handler(int signum) {
  if (signum == SIGINT || signum == SIGTERM) {
    LOG_SYSTEM("Signal " << signum << " received, preparing to exit...");
    force_quit = true;
  }
}

// Simple dummy strategy placeholder or just a logger if needed.
// For the source repo, we might just run the forwarding loop and logging.
static int run_logger(void *arg) {
  (void)arg;
  LOG_SYSTEM("Reference Logger running on core " << rte_lcore_id());
  while (!force_quit) {
    // In a real source-only app, you might consume market data here
    // or just let the main loop handle the network stuff.
    // For now, we just sleep/yield.
    rte_delay_us_sleep(1000 * 1000);
  }
  return 0;
}

int main(int argc, char *argv[]) {
  int ret;
  struct rte_mempool *mbuf_pool;

  /* Load Configuration */
  if (config_load() < 0) {
    printf(
        "Configuration loading failed. Check .env or environment variables.\n");
    exit(EXIT_FAILURE);
  }

  /* Initialize Logging Subsystem */
  logging_init();
  LOG_SYSTEM("HFT Gateway (Source Only) launching...");

  /* Initialize the Environment Abstraction Layer (EAL). */
  ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    LOG_SYSTEM("Error with EAL initialization");
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
  }

  /* Register Signal Handler */
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  /* Creates a new mempool in memory to hold the mbufs. */
  mbuf_pool =
      rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * 2, MBUF_CACHE_SIZE, 0,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

  /* Create Ring Buffer for Fast Path */
  hft_ring = rte_ring_create("hft_ring", RING_SIZE, rte_socket_id(), 0);
  if (hft_ring == NULL)
    rte_exit(EXIT_FAILURE, "Cannot create hft_ring\n");
  LOG_SYSTEM("HFT Ring Buffer created successfully.");

  /* Initialize and Configure Ports */
  LOG_SYSTEM("Calling init_port_mapping");
  init_port_mapping();
  LOG_SYSTEM("Calling configure_ports");
  configure_ports(mbuf_pool);
  LOG_SYSTEM("Ports configured");

  LOG_SYSTEM("DPDK EAL Initialized and Ports Configured successfully.");

  // Resolve Network Dependencies (IPs and Gateway MAC)
  LOG_SYSTEM("Resolving network dependencies...");

  // Resolve OKX IP
  auto okx_ip_opt = aero::NetworkUtils::resolve_hostname("ws.okx.com");
  uint32_t okx_ip_u32 = 0;
  if (okx_ip_opt) {
    okx_ip_u32 = *okx_ip_opt;
    LOG_SYSTEM(
        "Resolved OKX IP: " << aero::NetworkUtils::ip_to_string(okx_ip_u32));
  } else {
    LOG_SYSTEM("Failed to resolve OKX hostname");
  }

  // Resolve Bybit IP (just for logging/verification)
  auto bybit_ip_opt = aero::NetworkUtils::resolve_hostname("stream.bybit.com");
  if (bybit_ip_opt) {
    LOG_SYSTEM("Resolved Bybit IP: "
               << aero::NetworkUtils::ip_to_string(*bybit_ip_opt));
  }

  LOG_SYSTEM("Instantiating OrderBookManager");
  aero::OrderBookManager order_book_manager;

  // UDP Market Data Publisher
  auto udp_publisher = std::make_unique<aero::UdpPublisher>();
  if (app_config.udp_feed_enabled) {
    if (udp_publisher->init(app_config.udp_feed_address,
                            app_config.udp_feed_port)) {
      LOG_SYSTEM("UDP Publisher initialized on " << app_config.udp_feed_address
                                                 << ":"
                                                 << app_config.udp_feed_port);
    } else {
      LOG_SYSTEM("Failed to initialize UDP Publisher");
    }
  }

  // Connections
  LOG_SYSTEM("Instantiating OkxConnection");
  aero::OkxConnection okx_conn(udp_publisher.get());
  LOG_SYSTEM("Instantiating BybitConnection");
  aero::BybitConnection bybit_conn(udp_publisher.get());

  // Restore HftClassifier
  LOG_SYSTEM("Instantiating HftClassifier");
  HftClassifier classifier(0);

  // Strategy Engine REMOVED for Source Only Release

  // Register subscriptions
  // OKX Subscriptions
  std::vector<std::string> okx_instruments;
  for (int i = 0; i < app_config.okx_symbol_count; i++) {
    if (app_config.okx_symbols[i]) {
      okx_instruments.push_back(app_config.okx_symbols[i]);
      LOG_SYSTEM("Configured OKX Symbol: " << app_config.okx_symbols[i]);
    }
  }
  okx_conn.subscribe(okx_instruments, "books5");

  // Bybit Subscriptions
  std::vector<std::string> bybit_instruments;
  for (int i = 0; i < app_config.bybit_symbol_count; i++) {
    if (app_config.bybit_symbols[i]) {
      bybit_instruments.push_back(app_config.bybit_symbols[i]);
      LOG_SYSTEM("Configured Bybit Symbol: " << app_config.bybit_symbols[i]);
    }
  }
  bybit_conn.subscribe(bybit_instruments, "orderbook.50");

  // Initiate connections
  if (okx_conn.connect()) {
    LOG_SYSTEM("Initiated OKX connection.");
  } else {
    LOG_SYSTEM("Failed to initiate OKX connection (will retry).");
  }

  if (bybit_conn.connect()) {
    LOG_SYSTEM("Initiated Bybit connection.");
  }

  /* Launch Dummy/Logger on a worker core */
  unsigned int worker_core_id = rte_get_next_lcore(rte_lcore_id(), 1, 0);
  if (worker_core_id == RTE_MAX_LCORE) {
    LOG_SYSTEM("Warning: No worker core available for logger. Running purely "
               "in forwarding loop.");
  } else {
    LOG_SYSTEM("Launching Reference Logger on core " << worker_core_id);
    rte_eal_remote_launch(run_logger, NULL, worker_core_id);
  }

  /* Start Forwarding Loop on Main Core (NIC <-> TAP Bridge) */
  LOG_SYSTEM("Starting lcore_forward_loop");
  lcore_forward_loop(classifier);

  /* Wait for Worker Core */
  if (worker_core_id != RTE_MAX_LCORE) {
    rte_eal_wait_lcore(worker_core_id);
  }

  /* Clean up ports */
  close_ports();
  logging_shutdown();
  rte_eal_cleanup();

  return 0;
}
