#ifndef APP_CLASSIFIER_H
#define APP_CLASSIFIER_H

#include <rte_mbuf.h>
#include <stdbool.h>

/**
 * Classify packet as HFT (Fast Path) or Standard (Slow Path).
 * Returns true if HFT, false otherwise.
 */
bool classify_packet(struct rte_mbuf *m);

#endif /* APP_CLASSIFIER_H */
