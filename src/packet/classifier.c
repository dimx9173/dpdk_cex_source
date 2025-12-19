#include "classifier.h"

bool classify_packet(struct rte_mbuf *m) {
    /* 
     * Mock logic: Return false (Slow Path) for everything initially.
     * This ensures all traffic goes to the Kernel, allowing SSH/Ping 
     * to work immediately for the Exception Path verification.
     * 
     * Future HFT logic will go here.
     */
    return false;
}
