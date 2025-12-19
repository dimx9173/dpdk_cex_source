#include "forwarding.h"
#include "../modules/classifier/classifier.h"
#include "init.h"
#include "types.h"
#include <iostream>
#include <rte_branch_prediction.h>
#include <rte_cycles.h> // Added for timestamping
#include <rte_ethdev.h>
#include <rte_lcore.h> // Added for rte_lcore_id
#include <rte_mbuf.h>
#include <rte_prefetch.h>
#include <vector>

#include <rte_ring.h>
#define BURST_SIZE 32

extern struct rte_ring *hft_ring; // Declare hft_ring as extern

void lcore_forward_loop(HftClassifier &classifier) {
  fprintf(stderr, "DEBUG: Entered lcore_forward_loop (Hybrid Path)\n");
  struct rte_mbuf *pkts_burst[BURST_SIZE];
  struct rte_mbuf *kernel_tx_burst[BURST_SIZE];

  uint16_t nb_rx, i;
  uint16_t k_idx; // Kernel TX index

  // Packet counters for debugging
  static uint64_t rx_phy_total = 0;
  static uint64_t tx_virt_total = 0;
  static uint64_t rx_virt_total = 0;
  static uint64_t tx_phy_total = 0;
  static uint64_t last_stats_time = 0;
  static const uint64_t STATS_INTERVAL_CYCLES =
      5 * rte_get_timer_hz(); // 5 seconds

  printf("HFT Forwarding Engine Running on Core %u\n", rte_lcore_id());
  fflush(stdout);
  fprintf(stderr, "DEBUG: Entering main forward loop (Pure Exception Path)\n");

  // Check link state before starting
  struct rte_eth_link phy_link, virt_link;
  rte_eth_link_get_nowait(phy_port_id, &phy_link);
  printf("[Forwarding] Physical Port %u Link: %s, Speed: %u Mbps, Duplex: %s\n",
         phy_port_id, phy_link.link_status ? "UP" : "DOWN", phy_link.link_speed,
         phy_link.link_duplex ? "Full" : "Half");
  fflush(stdout);
  if (virt_port_id != RTE_MAX_ETHPORTS) {
    rte_eth_link_get_nowait(virt_port_id, &virt_link);
    printf("[Forwarding] Virtio Port %u Link: %s\n", virt_port_id,
           virt_link.link_status ? "UP" : "DOWN");
    fflush(stdout);
  }

  while (!force_quit) {
    // ==========================================
    // 0. Egress: Strategy -> Physical (Legacy Ring Removed)
    // ==========================================
    // Order execution is now handled by Boost via OkxConnection/BybitConnection
    // which use net::post to the IO thread, writing to the SSL socket
    // (Kernel/TAP).

    // ==========================================
    // 1. Ingress: Physical -> Classifier -> Kernel (Virtio)
    // ==========================================
    nb_rx = rte_eth_rx_burst(phy_port_id, 0, pkts_burst, BURST_SIZE);
    rx_phy_total += nb_rx;

    if (likely(nb_rx > 0)) {
      uint64_t rx_timestamp =
          rte_get_timer_cycles(); // Capture timestamp batch-wise or per-packet?
      // Batch ts is slightly less accurate but much faster.
      // For high precision, better strictly per packet or just once for the
      // burst? Since it's a burst, the arrival times are close. Assigning same
      // TS to burst is acceptable for "system latency" measurement. But let's
      // check if we iterate.
      k_idx = 0;
      for (i = 0; i < nb_rx; i++) {
        // Store timestamp in udata64 (user data field)
        pkts_burst[i]->dynfield1[0] = rx_timestamp;

        // Classification
        TrafficType type = classifier.classify(pkts_burst[i]);

        if (type == TRAFFIC_TYPE_HFT) {
          // Fast Path: Enqueue to Ring
          // CRITICAL: We must "Tea" (Duplicate) the packet so Kernel also gets
          // it for TCP State Machine (ACKs).
          // Increments refcount
          rte_pktmbuf_refcnt_update(pkts_burst[i], 1);

          // Enqueue to Ring
          // Enqueue to Ring
          if (rte_ring_sp_enqueue(hft_ring, pkts_burst[i]) < 0) {
            // Always print failure (throttled slightly to avoid 100% CPU log
            // but ensure visibility)
            static int fail_count = 0;
            if (fail_count++ % 100 == 0) {
              printf("[Forwarding] Ring Enqueue FAILED (Full?) Count=%d\n",
                     fail_count);
            }
            // Enqueue failed
            rte_pktmbuf_free(pkts_burst[i]); // Free our copy
          }

          // Forward to Kernel (original copy)
          kernel_tx_burst[k_idx++] = pkts_burst[i];
        } else {
          // Standard Path: Send to Kernel
          kernel_tx_burst[k_idx++] = pkts_burst[i];
        }
      }

      // Batch TX to Virtio (Kernel)
      if (k_idx > 0) {
        if (virt_port_id != RTE_MAX_ETHPORTS) {
          uint16_t nb_tx =
              rte_eth_tx_burst(virt_port_id, 0, kernel_tx_burst, k_idx);
          tx_virt_total += nb_tx;
          if (unlikely(nb_tx < k_idx)) {
            for (i = nb_tx; i < k_idx; i++)
              rte_pktmbuf_free(kernel_tx_burst[i]);
          }
        } else {
          for (i = 0; i < k_idx; i++)
            rte_pktmbuf_free(kernel_tx_burst[i]);
        }
      }
    }

    // ==========================================
    // 2. Egress: Kernel -> Physical
    // ==========================================
    if (virt_port_id != RTE_MAX_ETHPORTS) {
      struct rte_mbuf *kernel_rx_burst_from_virtio[BURST_SIZE];
      nb_rx = rte_eth_rx_burst(virt_port_id, 0, kernel_rx_burst_from_virtio,
                               BURST_SIZE);

      rx_virt_total += nb_rx;
      if (likely(nb_rx > 0)) {
        uint16_t nb_tx = rte_eth_tx_burst(phy_port_id, 0,
                                          kernel_rx_burst_from_virtio, nb_rx);
        tx_phy_total += nb_tx;
        if (unlikely(nb_tx < nb_rx)) {
          for (i = nb_tx; i < nb_rx; i++)
            rte_pktmbuf_free(kernel_rx_burst_from_virtio[i]);
        }
      }
    }

    // Periodic stats output
    uint64_t now = rte_get_timer_cycles();
    if (now - last_stats_time > STATS_INTERVAL_CYCLES) {
      printf("[Forwarding Stats] RX_PHY: %lu, TX_VIRT: %lu, RX_VIRT: %lu, "
             "TX_PHY: %lu\n",
             rx_phy_total, tx_virt_total, rx_virt_total, tx_phy_total);
      last_stats_time = now;
    }
  }
}
