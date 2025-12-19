#ifndef _AERO_TYPES_H_
#define _AERO_TYPES_H_

#include <cstdint>

namespace aero {

/**
 * @brief Exchange identifiers
 */
enum class ExchangeId : uint8_t {
  OKX = 0,
  BYBIT = 1,
  BINANCE = 2,
  GATE = 3,
  BITGET = 4,
  MEXC = 5,
  UNKNOWN = 255
};

} // namespace aero

#endif // _AERO_TYPES_H_
