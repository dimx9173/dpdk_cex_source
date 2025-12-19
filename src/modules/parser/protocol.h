#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#include <stdint.h>

// Ensure packed alignment for network structures
#pragma pack(push, 1)

/**
 * @brief Common Market Data Header
 */
typedef struct MdHeader {
    uint16_t magic;     // 0xAABB (Sanity Check)
    uint16_t msg_type;  // 0x0001 = Book Update
    uint64_t seq_num;   // Sequence Number
    uint64_t timestamp; // Unix Nanoseconds
} __attribute__((packed)) MdHeader;

/**
 * @brief Market Data Book Update Payload (Type 0x0001)
 */
typedef struct MdBookUpdate {
    char     symbol[16]; // Null-padded (e.g., "BTC-USDT")
    double   price;      // Price level
    double   quantity;   // Quantity at level
    uint8_t  side;       // 0=Bid, 1=Ask
    uint8_t  padding[7]; // Align to 64 bytes
} __attribute__((packed)) MdBookUpdate;

#pragma pack(pop)

#endif // _PROTOCOL_H_