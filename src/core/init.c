#include "init.h"
#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_string_fns.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t phy_port_id = RTE_MAX_ETHPORTS;
uint16_t virt_port_id = RTE_MAX_ETHPORTS;

void init_port_mapping(void) {
  uint16_t pid;
  bool found_phy = false;
  bool found_virt = false;

  fprintf(stderr, "DEBUG: Starting RTE_ETH_FOREACH_DEV loop\n");
  RTE_ETH_FOREACH_DEV(pid) {
    fprintf(stderr, "DEBUG: Iterating pid %u\n", pid);
    struct rte_eth_dev_info dev_info;
    rte_eth_dev_info_get(pid, &dev_info);
    fprintf(stderr, "DEBUG: Got dev_info for pid %u: %s\n", pid,
            dev_info.driver_name);

    if (strstr(dev_info.driver_name, "net_virtio_user")) {
      virt_port_id = pid;
      found_virt = true;
      printf("Found Virtio-User Port: %u (Driver: %s)\n", pid,
             dev_info.driver_name);
    } else {
      phy_port_id = pid;
      found_phy = true;
      printf("Found Physical Port: %u (Driver: %s)\n", pid,
             dev_info.driver_name);
    }
  }
  fprintf(stderr, "DEBUG: Finished RTE_ETH_FOREACH_DEV loop\n");

  if (!found_phy) {
    rte_exit(EXIT_FAILURE, "Error: No Physical Port found.\n");
  }
  if (!found_virt) {
    printf("WARNING: No Virtio-User Port found. Exception path disabled.\n");
  }
}

void configure_ports(struct rte_mempool *mbuf_pool) {
  int ret;
  struct rte_eth_conf port_conf = {0};
  uint16_t nb_rxd = 1024;
  uint16_t nb_txd = 1024;

  /* Configure Physical Port */
  printf("Configuring Physical Port %u...\n", phy_port_id);
  ret = rte_eth_dev_configure(phy_port_id, 1, 1, &port_conf);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "Cannot configure physical port\n");

  ret = rte_eth_dev_adjust_nb_rx_tx_desc(phy_port_id, &nb_rxd, &nb_txd);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "Cannot adjust number of descriptors\n");

  ret = rte_eth_rx_queue_setup(phy_port_id, 0, nb_rxd,
                               rte_eth_dev_socket_id(phy_port_id), NULL,
                               mbuf_pool);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup: err=%d, port=%u\n", ret,
             phy_port_id);

  ret = rte_eth_tx_queue_setup(phy_port_id, 0, nb_txd,
                               rte_eth_dev_socket_id(phy_port_id), NULL);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup: err=%d, port=%u\n", ret,
             phy_port_id);

  /* Configure Virtual Port */
  if (virt_port_id != RTE_MAX_ETHPORTS) {
    printf("Configuring Virtio-User Port %u...\n", virt_port_id);
    ret = rte_eth_dev_configure(virt_port_id, 1, 1, &port_conf);
    if (ret < 0)
      rte_exit(EXIT_FAILURE, "Cannot configure virtio port\n");

    ret = rte_eth_rx_queue_setup(virt_port_id, 0, 1024,
                                 rte_eth_dev_socket_id(virt_port_id), NULL,
                                 mbuf_pool);
    if (ret < 0)
      rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup: err=%d, port=%u\n", ret,
               virt_port_id);

    ret = rte_eth_tx_queue_setup(virt_port_id, 0, 1024,
                                 rte_eth_dev_socket_id(virt_port_id), NULL);
    if (ret < 0)
      rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup: err=%d, port=%u\n", ret,
               virt_port_id);
  }

  /* Start Ports */
  ret = rte_eth_dev_start(phy_port_id);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "rte_eth_dev_start: err=%d, port=%u\n", ret,
             phy_port_id);

  if (virt_port_id != RTE_MAX_ETHPORTS) {
    ret = rte_eth_dev_start(virt_port_id);
    if (ret < 0)
      rte_exit(EXIT_FAILURE, "rte_eth_dev_start: err=%d, port=%u\n", ret,
               virt_port_id);
  }

  /* Enable Promiscuous Mode on Physical Port */
  ret = rte_eth_promiscuous_enable(phy_port_id);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "rte_eth_promiscuous_enable: err=%d, port=%u\n", ret,
             phy_port_id);
  printf("Promiscuous mode enabled on Physical Port %u\n", phy_port_id);
}

void close_ports(void) {
  printf("Closing ports...\n");
  if (phy_port_id != RTE_MAX_ETHPORTS) {
    rte_eth_dev_stop(phy_port_id);
    rte_eth_dev_close(phy_port_id);
    printf("Physical port %u closed.\n", phy_port_id);
  }
  if (virt_port_id != RTE_MAX_ETHPORTS) {
    rte_eth_dev_stop(virt_port_id);
    rte_eth_dev_close(virt_port_id);
    printf("Virtio port %u closed.\n", virt_port_id);
  }
}
