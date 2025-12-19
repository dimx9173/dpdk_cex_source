#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRAFFIC_TYPE_STANDARD = 0, // Slow path
    TRAFFIC_TYPE_HFT      = 1, // Fast path
    TRAFFIC_TYPE_IGNORE   = 2  // Drop
} TrafficType;

#ifdef __cplusplus
}
#endif

#endif /* CORE_TYPES_H */
