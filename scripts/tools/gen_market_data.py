from scapy.all import Ether, IP, UDP, Raw
import struct
import socket
import time

# --- Configuration ---
DEST_IP = "127.0.0.1"
DEST_PORT = 12345
IFACE = "lo" # Loopback interface for local testing

# --- Protocol Definitions (Python equivalents) ---
# Little Endian for x86/ARM simplicity (as per design.md)
# MdHeader: magic (H), msg_type (H), seq_num (Q), timestamp (Q)
# MdBookUpdate: symbol (16s), price (d), quantity (d), side (B), padding (7s)

MD_HEADER_FORMAT = "<HHQQ" # Little-endian, 2xuint16, 2xuint64
MD_BOOK_UPDATE_FORMAT = "<16sddB7s" # Little-endian, 16-char string, 2xdouble, uint8, 7-char string

MAGIC = 0xAABB
MSG_TYPE_BOOK_UPDATE = 0x0001
SIDE_BID = 0
SIDE_ASK = 1

def generate_book_update_packet(
    seq_num: int,
    symbol: str,
    price: float,
    quantity: float,
    side: int,
    dst_ip: str = DEST_IP,
    dst_port: int = DEST_PORT,
    invalid_magic: bool = False,
    truncated: bool = False,
) -> bytes:
    """
    Generates a raw UDP packet byte string for a market data book update.
    """
    current_time_ns = int(time.time_ns())

    # Pack MdHeader
    header_magic = MAGIC if not invalid_magic else 0xDEAD
    md_header = struct.pack(
        MD_HEADER_FORMAT,
        header_magic,
        MSG_TYPE_BOOK_UPDATE,
        seq_num,
        current_time_ns,
    )

    # Pack MdBookUpdate payload
    padded_symbol = symbol.encode('ascii') + b'\0' * (16 - len(symbol))
    md_book_update = struct.pack(
        MD_BOOK_UPDATE_FORMAT,
        padded_symbol,
        price,
        quantity,
        side,
        b'\0' * 7, # Padding
    )

    full_payload = md_header + md_book_update

    if truncated:
        # Simulate truncation, e.g., by sending only part of the payload
        full_payload = full_payload[:len(full_payload) - 10] # Truncate 10 bytes from end

    # Construct the full packet using Scapy
    packet = Ether() / IP(dst=dst_ip) / UDP(dport=dst_port, sport=50000) / Raw(load=full_payload)
    return bytes(packet)

def send_packet(packet_bytes: bytes, iface: str = IFACE):
    """
    Sends a raw packet bytes over the specified interface.
    """
    # Using a raw socket to send pre-built Ethernet frames
    # Need CAP_NET_RAW capability or run as root
    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
    s.bind((iface, 0))
    s.sendall(packet_bytes)
    s.close()

if __name__ == "__main__":
    print(f"Generating and sending market data packets to {DEST_IP}:{DEST_PORT} via {IFACE}...")

    # Example 1: Valid Bid Book Update
    print("\n--- Sending Valid Bid Update (BTC-USDT) ---")
    packet1 = generate_book_update_packet(1, "BTC-USDT", 60000.50, 1.23, SIDE_BID)
    # send_packet(packet1) # Commented out for now, as it requires root/CAP_NET_RAW

    # Example 2: Valid Ask Book Update
    print("\n--- Sending Valid Ask Update (ETH-USDT) ---")
    packet2 = generate_book_update_packet(2, "ETH-USDT", 3000.75, 5.67, SIDE_ASK)
    # send_packet(packet2)

    # Example 3: Invalid Magic
    print("\n--- Sending Invalid Magic Packet ---")
    packet3 = generate_book_update_packet(3, "XRP-USDT", 0.50, 1000.0, SIDE_BID, invalid_magic=True)
    # send_packet(packet3)

    # Example 4: Truncated Packet
    print("\n--- Sending Truncated Packet ---")
    packet4 = generate_book_update_packet(4, "LTC-USDT", 150.0, 10.0, SIDE_ASK, truncated=True)
    # send_packet(packet4)

    print("\nScript finished. Note: Packets are commented out to prevent errors if not run as root or without scapy installed.")
    print("To send packets, uncomment 'send_packet()' calls and ensure Scapy is installed (`pip install scapy`).")
    print("You might need to run this script with `sudo python3 gen_market_data.py`.")
