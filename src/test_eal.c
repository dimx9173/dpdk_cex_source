#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_mbuf.h> // Corrected include
#include <stdlib.h> // Added for EXIT_FAILURE
#include <stdio.h>

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

int main(int argc, char *argv[]) {
    int ret;
    struct rte_mempool *mbuf_pool;

    // Initialize the Environment Abstraction Layer (EAL).
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    }

    printf("EAL initialized successfully.\n");

    // Creates a new mempool in memory to hold the mbufs.
    mbuf_pool = rte_pktmbuf_pool_create("TEST_MBUF_POOL", NUM_MBUFS * 2,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    if (mbuf_pool == NULL) {
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
    }

    printf("Mbuf pool created successfully.\n");

    // Clean up EAL.
    rte_eal_cleanup();

    return 0;
}
