# HFT Gateway (Project AERO)

A high-frequency trading gateway using DPDK 23.11 with a Virtio-user + TAP architecture for ultra-low latency cryptocurrency arbitrage.

## Features

| Category                 | Description                                    |
| ------------------------ | ---------------------------------------------- |
| **Zero-Copy Forwarding** | Direct NIC access via DPDK                     |
| **Exception Path**       | Kernel fallback using Virtio-user + TAP        |
| **Exchange Support**     | OKX, Bybit (Binance, Gate, Bitget, MEXC ready) |
| **Optimization**         | AVX2, simdjson parsing, lock-free ring buffers |
| **Market Data**          | Real-time WebSocket feeds, UDP broadcast       |

📖 **[Architecture Documentation](docs/ARCHITECTURE.md)** | 📝 **[Daily Logs](docs/daily/)**

---

## Quick Start

### Prerequisites

- **System**: Linux with kernel 5.x+
- **Build**: Meson + Ninja
- **Library**: DPDK 23.11+
- **Compiler**: GCC 13+ / Clang 17+ (C++23)
- **Dependencies**: simdjson, OpenSSL, GTest, Boost (Asio, Beast, SSL, Thread)

```bash
# Install all dependencies
sudo ./scripts/install_deps.sh
```

### Build

```bash
meson setup build
meson compile -C build
```

### Run

#### Standard Mode (Dedicated NIC)
```bash
sudo ./build/src/hft-app -a 0000:01:00.0 --vdev=net_virtio_user0,iface=tap0,path=/dev/vhost-net
```

#### Single-NIC Mode (VM/Testing)
```bash
# Uses mock physical device for testing without dedicated NIC
# Note: Loads .env, preserves vars for sudo, and runs in background
sudo -v ; source .env ; sudo -E nohup ./scripts/deploy.sh --single-nic &
```

> **Note**: Create `.env` with API credentials before running (see `.env.example`).

---

## Configuration

All configuration is done via environment variables. Create a `.env` file in the project root.

### Exchange Credentials (Required)

```bash
# OKX
export OKX_API_KEY="your-key"
export OKX_API_SECRET="your-secret"
export OKX_PASSPHRASE="your-passphrase"

# Bybit
export BYBIT_API_KEY="your-key"
export BYBIT_API_SECRET="your-secret"
```

### Trading Symbols

```bash
# Comma-separated symbol lists (optional)
TRADING_SYMBOLS_OKX=ETH-USDT-SWAP,XRP-USDT-SWAP,SOL-USDT-SWAP
TRADING_SYMBOLS_BYBIT=ETHUSDT,XRPUSDT,SOLUSDT
```

**Default Pairs** (if not specified):
- **OKX**: ETH, XRP, SOL, TRX, DOGE (USDT-SWAP)
- **Bybit**: ETH, XRP, SOL, TRX, DOGE (USDT)

### Logging

Structured logging with automatic file output:

| Variable             | Default           | Description          |
| -------------------- | ----------------- | -------------------- |
| `LOG_PRICE_ENABLED`  | `true`            | Order book updates   |
| `LOG_SYSTEM_ENABLED` | `true`            | Startup, connections |
| `LOG_TRADE_ENABLED`  | `true`            | Order execution      |
| `LOG_PRICE_FILE`     | `logs/price.log`  | Price log path       |
| `LOG_SYSTEM_FILE`    | `logs/system.log` | System log path      |
| `LOG_TRADE_FILE`     | `logs/trade.log`  | Trade log path       |

> Logs are written to `logs/` directory by default. The directory is created automatically.

### UDP Market Data Feed

Broadcast order book updates via UDP for low-latency consumption:

```bash
UDP_FEED_ENABLED=true
UDP_FEED_ADDRESS=127.0.0.1
UDP_FEED_PORT=13988
```

Example clients: `examples/python/udp_receiver.py`, `examples/cpp/udp_sender.cpp`

### WebSocket Retry

```bash
WS_RETRY_ENABLED=true           # Enable auto-reconnect
WS_RETRY_MAX_ATTEMPTS=10        # Max retry attempts
WS_RETRY_INITIAL_DELAY_MS=1000  # Initial delay
WS_RETRY_MAX_DELAY_MS=30000     # Max delay (exponential backoff)
```

---

## Testing

```bash
# Integration test (creates TAP interface with simulated port)
sudo ./scripts/tests/test_integration.sh

# Unit tests
meson test -C build
```

---

## Project Structure

```
├── src/
│   ├── core/           # DPDK init, forwarding, logging
│   ├── config/         # Environment-based configuration
│   ├── modules/
│   │   ├── exchange/   # OKX, Bybit adapters
│   │   ├── network/    # WebSocket, UDP, TCP
│   │   ├── strategy/   # Arbitrage engine
│   │   └── parser/     # simdjson wrapper
├── scripts/            # Deployment & utilities
├── docs/               # Architecture, daily logs
├── examples/           # UDP client examples
└── tests/              # Unit tests
```

---

## License

BSD-3-Clause
