# scripts/mock_exchange.py
import asyncio
import websockets
import json
import base64
import hashlib
import time
from datetime import datetime

# Configuration
MOCK_EXCHANGE_HOST = "0.0.0.0"
MOCK_EXCHANGE_PORT = 8765 # Use a non-standard port to avoid conflicts
OKX_WS_PATH = "/ws/v5/public"
BYBIT_WS_PATH = "/realtime" # Example path, adjust if needed

print(f"Mock Exchange will listen on ws://{MOCK_EXCHANGE_HOST}:{MOCK_EXCHANGE_PORT}{OKX_WS_PATH} and {BYBIT_WS_PATH}")

async def handle_websocket_connection(websocket, path):
    print(f"Client connected from {websocket.remote_address} to path: {path}")

    # Perform WebSocket Handshake (server side, websockets library handles this automatically)
    # The client (DPDK app) will send a HTTP GET request.
    # The websockets library will handle the 101 Switching Protocols response.

    if path == OKX_WS_PATH:
        exchange_name = "OKX"
        await send_okx_mock_data(websocket)
    elif path == BYBIT_WS_PATH:
        exchange_name = "BYBIT"
        await send_bybit_mock_data(websocket)
    else:
        print(f"Unsupported path: {path}. Closing connection.")
        await websocket.close()
        return

    try:
        async for message in websocket:
            print(f"[{exchange_name}] Received message from client: {message}")
            # Here you would parse the client's message (e.g., order, subscription)
            # For now, just printing it.
            
    except websockets.exceptions.ConnectionClosedOK:
        print(f"[{exchange_name}] Client {websocket.remote_address} disconnected cleanly.")
    except websockets.exceptions.ConnectionClosedError as e:
        print(f"[{exchange_name}] Client {websocket.remote_address} disconnected with error: {e}")
    except Exception as e:
        print(f"[{exchange_name}] An unexpected error occurred: {e}")
    finally:
        print(f"[{exchange_name}] Connection closed for {websocket.remote_address}.")

async def send_okx_mock_data(websocket):
    count = 0
    while websocket.open:
        count += 1
        # Mock OKX books-l2-tbt snapshot/update
        # Alternate between snapshot and update for testing
        action = "snapshot" if count == 1 else "update"
        
        okx_data = {
            "event": "update",
            "arg": {
                "channel": "books-l2-tbt",
                "instId": "BTC-USDT"
            },
            "action": action,
            "data": [
                {
                    "ts": str(int(time.time() * 1000)),
                    "bids": [
                        ["60000.5", "1.5", "0", "1"],
                        ["60000.0", "2.0", "0", "1"]
                    ],
                    "asks": [
                        ["60001.0", "0.5", "0", "1"],
                        ["60001.5", "1.0", "0", "1"]
                    ],
                    "instId": "BTC-USDT",
                    "checksum": "12345678" # Mock checksum
                }
            ]
        }
        if action == "update":
            # Simulate a price change
            okx_data["data"][0]["bids"][0][0] = str(60000.5 + count * 0.1)
            okx_data["data"][0]["asks"][0][0] = str(60001.0 + count * 0.1)

        try:
            await websocket.send(json.dumps(okx_data))
            # print(f"[OKX] Sent mock data: {json.dumps(okx_data)}")
        except websockets.exceptions.ConnectionClosed:
            break
        await asyncio.sleep(1) # Send every 1 second

async def send_bybit_mock_data(websocket):
    count = 0
    while websocket.open:
        count += 1
        # Mock Bybit orderbook.50 snapshot/delta
        # Alternate between snapshot and delta for testing
        msg_type = "snapshot" if count == 1 else "delta"
        
        bybit_data = {
            "topic": "orderbook.50.BTCUSDT",
            "type": msg_type,
            "data": {
                "s": "BTCUSDT",
                "b": [ # bids
                    ["59999.5", "1.0"],
                    ["59999.0", "2.0"]
                ],
                "a": [ # asks
                    ["60000.0", "0.5"],
                    ["60000.5", "1.0"]
                ],
                "u": count, # update id
                "ts": int(time.time() * 1000)
            }
        }
        if msg_type == "delta":
            # Simulate a price change
            bybit_data["data"]["b"][0][0] = str(59999.5 + count * 0.1)
            bybit_data["data"]["a"][0][0] = str(60000.0 + count * 0.1)
            # Add a zero quantity to simulate removal
            if count % 5 == 0:
                 bybit_data["data"]["b"].append(["59990.0", "0.0"])

        try:
            await websocket.send(json.dumps(bybit_data))
            # print(f"[BYBIT] Sent mock data: {json.dumps(bybit_data)}")
        except websockets.exceptions.ConnectionClosed:
            break
        await asyncio.sleep(1) # Send every 1 second

async def main():
    stop_event = asyncio.Event()

    # Start servers for both paths
    okx_server = websockets.serve(
        handle_websocket_connection,
        MOCK_EXCHANGE_HOST,
        MOCK_EXCHANGE_PORT,
        subprotocols=["websocket"], # Necessary for some clients to connect
        process_request=lambda path, headers: (path, headers) # Allow dynamic path handling
    )
    # The `websockets` library handles paths dynamically. No need for separate serve calls unless different ports.

    print(f"Mock WebSocket server started on ws://{MOCK_EXCHANGE_HOST}:{MOCK_EXCHANGE_PORT}")
    
    # Run indefinitely
    async with okx_server:
        await stop_event.wait()

if __name__ == "__main__":
    asyncio.run(main())
