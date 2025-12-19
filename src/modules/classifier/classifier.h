#pragma once

#include <cstdint>
#include <span>
#include <rte_mbuf.h>
#include "core/types.h"

class HftClassifier {
public:
    // Initialize with specific criteria (e.g. target port for Market Data)
    explicit HftClassifier(uint16_t target_port);

    // Main classification logic
    // Returns:
    //   TRAFFIC_TYPE_HFT:      Matches target port (IPv4/UDP|TCP)
    //   TRAFFIC_TYPE_STANDARD: Non-matching valid traffic (ARP, SSH, etc.)
    //   TRAFFIC_TYPE_IGNORE:   Invalid/Malformed (Optional)
    [[nodiscard]] TrafficType classify(const rte_mbuf* m) const;

private:
    uint16_t target_port_;
};
