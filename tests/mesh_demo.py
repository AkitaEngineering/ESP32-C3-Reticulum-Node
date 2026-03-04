#!/usr/bin/env python3
"""
╔═══════════════════════════════════════════════════════════════════╗
║         ESP32 Reticulum Mesh Network — Live Demo                 ║
║                                                                   ║
║  Visualizes real-time ESP-NOW mesh communication between two      ║
║  ESP32-C3 nodes running the Reticulum firmware.                   ║
║                                                                   ║
║  Usage:  python3 mesh_demo.py [port0] [port1]                     ║
║  Default: /dev/ttyACM0 /dev/ttyACM1                               ║
╚═══════════════════════════════════════════════════════════════════╝
"""

import serial
import sys
import time
import threading
import os
import re
from collections import defaultdict

# ── ANSI Colors ──────────────────────────────────────────────────
RESET   = "\033[0m"
BOLD    = "\033[1m"
DIM     = "\033[2m"
BLINK   = "\033[5m"
RED     = "\033[31m"
GREEN   = "\033[32m"
YELLOW  = "\033[33m"
BLUE    = "\033[34m"
MAGENTA = "\033[35m"
CYAN    = "\033[36m"
WHITE   = "\033[37m"
BG_BLACK = "\033[40m"
BG_BLUE  = "\033[44m"
BG_GREEN = "\033[42m"
BG_RED   = "\033[41m"

NODE_COLORS = [CYAN, MAGENTA]
NODE_NAMES  = ["ALPHA ◆", "BRAVO ◇"]
NODE_SHORT  = ["α", "β"]

# ── State ────────────────────────────────────────────────────────
lock = threading.Lock()

stats = {
    0: {"tx": 0, "rx": 0, "announce": 0, "app_recv": 0, "mac": "unknown", "boot": False},
    1: {"tx": 0, "rx": 0, "announce": 0, "app_recv": 0, "mac": "unknown", "boot": False},
}

packets_log = []  # (timestamp, from_node, to_node, size, msg)
events_log  = []  # (timestamp, node, color, text)
start_time  = time.time()

MAX_LOG = 18
MAX_EVENTS = 6

def elapsed():
    return time.time() - start_time

def short_mac(mac_str):
    """Shorten a MAC like 10B41D6497AC -> 10:B4:..97:AC"""
    if len(mac_str) >= 12:
        return f"{mac_str[0:2]}:{mac_str[2:4]}:..{mac_str[8:10]}:{mac_str[10:12]}"
    return mac_str

# ── Terminal helpers ─────────────────────────────────────────────
def clear():
    print("\033[2J\033[H", end="")

def move(row, col):
    print(f"\033[{row};{col}H", end="")

def get_terminal_width():
    try:
        return os.get_terminal_size().columns
    except:
        return 80

def get_terminal_height():
    try:
        return os.get_terminal_size().lines
    except:
        return 24

def center(text, width, fill=" "):
    raw = re.sub(r'\033\[[0-9;]*m', '', text)
    pad = max(0, width - len(raw))
    left = pad // 2
    right = pad - left
    return fill * left + text + fill * right

# ── Drawing ──────────────────────────────────────────────────────
def draw_header(w):
    move(1, 1)
    print(f"{BG_BLUE}{BOLD}{WHITE}" + center("⚡ ESP32-C3 RETICULUM MESH — LIVE DEMO ⚡", w) + RESET)
    move(2, 1)
    elapsed_s = elapsed()
    mins = int(elapsed_s) // 60
    secs = int(elapsed_s) % 60
    info = f" Runtime: {mins:02d}:{secs:02d}  │  Press Ctrl+C to exit "
    print(f"{DIM}" + center(info, w) + RESET)

def draw_nodes(w):
    move(4, 1)
    col_w = w // 2 - 2

    # Node boxes
    for i in range(2):
        col_start = 1 + i * (w // 2)
        c = NODE_COLORS[i]
        s = stats[i]
        mac_display = short_mac(s["mac"]) if s["mac"] != "unknown" else "discovering..."
        status = f"{GREEN}● ONLINE{RESET}" if s["boot"] else f"{RED}○ WAITING{RESET}"

        move(4, col_start)
        print(f"{c}{BOLD}┌{'─' * (col_w - 2)}┐{RESET}")
        move(5, col_start)
        print(f"{c}{BOLD}│{RESET}" + center(f"{c}{BOLD} {NODE_NAMES[i]} {RESET}", col_w - 2 + len(c) + len(BOLD) + len(RESET) * 2) + f"{c}{BOLD}│{RESET}")
        move(6, col_start)
        print(f"{c}│{RESET}" + center(f"MAC: {mac_display}", col_w - 2) + f"{c}│{RESET}")
        move(7, col_start)
        print(f"{c}│{RESET}" + center(f"Status: {status}", col_w - 2 + len(GREEN) + len(RESET) + (len(RED) if not s["boot"] else 0)) + f"{c}│{RESET}")
        move(8, col_start)
        print(f"{c}│{RESET}" + center(f"TX: {s['tx']:>4}  │  RX: {s['rx']:>4}  │  App: {s['app_recv']:>3}", col_w - 2) + f"{c}│{RESET}")
        move(9, col_start)
        print(f"{c}{BOLD}└{'─' * (col_w - 2)}┘{RESET}")

    # Connection arrow between nodes
    mid = w // 2
    move(6, mid - 6)
    total_mesh = stats[0]["rx"] + stats[1]["rx"]
    if total_mesh > 0:
        print(f"{GREEN}{BOLD}◄══ ESP-NOW ══►{RESET}")
    else:
        print(f"{DIM}◄── ESP-NOW ──►{RESET}")

def draw_packet_log(w):
    move(11, 1)
    print(f"{BOLD}{WHITE}{'─' * w}{RESET}")
    move(12, 1)
    print(f"{BOLD} 📡 LIVE PACKET FLOW{RESET}")
    move(13, 1)
    print(f"{DIM} {'Time':>7}  {'From':>8}  {'':>3}  {'To':>8}  {'Size':>5}  Message{RESET}")
    move(14, 1)
    print(f"{DIM}{'─' * w}{RESET}")

    with lock:
        display_packets = packets_log[-MAX_LOG:]

    for idx, (ts, from_n, to_n, size, msg) in enumerate(display_packets):
        row = 15 + idx
        move(row, 1)
        fc = NODE_COLORS[from_n]
        tc = NODE_COLORS[to_n]
        arrow = f"{YELLOW}──►{RESET}"
        t_str = f"{ts:7.1f}s"
        line = f" {DIM}{t_str}{RESET}  {fc}{NODE_SHORT[from_n]:>8}{RESET}  {arrow}  {tc}{NODE_SHORT[to_n]:>8}{RESET}  {size:>4}B  {GREEN}{msg}{RESET}"
        # Pad to full width
        raw_len = 7 + 2 + 8 + 2 + 3 + 2 + 8 + 2 + 5 + 2 + len(msg)
        padding = max(0, w - raw_len - 2)
        print(line + " " * padding)

    # Clear remaining rows
    for idx in range(len(display_packets), MAX_LOG):
        row = 15 + idx
        move(row, 1)
        print(" " * w)

def draw_events(w):
    row_start = 15 + MAX_LOG + 1
    move(row_start, 1)
    print(f"{BOLD}{WHITE}{'─' * w}{RESET}")
    move(row_start + 1, 1)
    print(f"{BOLD} 📋 EVENTS{RESET}")

    with lock:
        display_events = events_log[-MAX_EVENTS:]

    for idx, (ts, node, color, text) in enumerate(display_events):
        row = row_start + 2 + idx
        move(row, 1)
        t_str = f"{ts:7.1f}s"
        n_str = f"{color}{NODE_SHORT[node]}{RESET}"
        line = f" {DIM}{t_str}{RESET}  {n_str}  {text}"
        raw_len = 7 + 2 + 1 + 2 + len(re.sub(r'\033\[[0-9;]*m', '', text))
        padding = max(0, w - raw_len - 2)
        print(line + " " * padding)

    for idx in range(len(display_events), MAX_EVENTS):
        row = row_start + 2 + idx
        move(row, 1)
        print(" " * w)

def draw_footer(w):
    h = get_terminal_height()
    move(h - 1, 1)
    total_packets = sum(s["tx"] + s["rx"] for s in stats.values())
    total_app = sum(s["app_recv"] for s in stats.values())
    footer = f" Total mesh packets: {total_packets}  │  App deliveries: {total_app}  │  github.com/AkitaEngineering/ESP32-C3-Reticulum-Node "
    print(f"{BG_BLACK}{DIM}" + center(footer, w) + RESET)

def render():
    w = get_terminal_width()
    draw_header(w)
    draw_nodes(w)
    draw_packet_log(w)
    draw_events(w)
    draw_footer(w)
    sys.stdout.flush()


# ── Serial Parser ────────────────────────────────────────────────
def parse_line(node_id, line):
    """Parse a debug serial line and update state."""
    other = 1 - node_id
    t = elapsed()
    c = NODE_COLORS[node_id]

    # Boot detection
    if "Reticulum Gateway" in line or "Setup Complete" in line:
        with lock:
            stats[node_id]["boot"] = True
            events_log.append((t, node_id, c, f"{GREEN}Node booted successfully{RESET}"))

    # Announce sent
    if "Sending periodic announce" in line:
        with lock:
            stats[node_id]["announce"] += 1
            stats[node_id]["tx"] += 1
            events_log.append((t, node_id, c, f"{YELLOW}Announce beacon sent{RESET}"))

    # Packet TX
    if "SENDING PACKET" in line:
        with lock:
            stats[node_id]["tx"] += 1

    # Message TX (capture the message content)
    m = re.search(r'Message:\s*(.*)', line)
    if m and "SENDING" not in line:
        msg = m.group(1).strip()

    # ESP-NOW RX
    rx_match = re.search(r'\[ESP-NOW RX\]\s*(\d+)B from (\w+)', line)
    if rx_match:
        size = int(rx_match.group(1))
        mac = rx_match.group(2)
        with lock:
            stats[node_id]["rx"] += 1
            # Try to figure out the sender's MAC
            for other_id in range(2):
                if other_id != node_id:
                    if stats[other_id]["mac"] == "unknown":
                        stats[other_id]["mac"] = mac
                    if stats[other_id]["mac"] == mac:
                        packets_log.append((t, other_id, node_id, size, f"ESP-NOW data ({size}B)"))
                        break
            else:
                packets_log.append((t, other, node_id, size, f"ESP-NOW data ({size}B)"))

    # PLAIN destination match
    if "subscribed PLAIN destination" in line:
        with lock:
            events_log.append((t, node_id, c, f"{GREEN}Matched PLAIN destination ✓{RESET}"))

    # App layer delivery
    app_match = re.search(r'App Layer Received (\d+) bytes.*?:\s*"(.*?)"', line)
    if app_match:
        nbytes = int(app_match.group(1))
        payload = app_match.group(2)
        with lock:
            stats[node_id]["app_recv"] += 1
            # Update/replace last packet log with actual message content
            for i in range(len(packets_log) - 1, max(-1, len(packets_log) - 5), -1):
                if packets_log[i][2] == node_id:
                    old = packets_log[i]
                    packets_log[i] = (old[0], old[1], old[2], nbytes, f'"{payload}"')
                    break

    # Self-packet info (extract payload text)
    sp_match = re.search(r'Payload:\s*\[(.+?)\]', line)
    if sp_match:
        pass  # handled by app_match

    # Loopback detection
    if "Dropping packet sourced from self" in line:
        with lock:
            events_log.append((t, node_id, c, f"{DIM}Dropped self-sourced packet (loop prevention){RESET}"))

    # Announce received from other node
    if "Processing Announce" in line:
        with lock:
            events_log.append((t, node_id, c, f"{CYAN}Received announce from neighbor{RESET}"))

    # Routing table update
    if "Route" in line and ("added" in line or "updated" in line):
        with lock:
            events_log.append((t, node_id, c, f"{BLUE}Routing table updated{RESET}"))

    # USB connection
    if "Host opened CDC" in line:
        with lock:
            if not stats[node_id]["boot"]:
                stats[node_id]["boot"] = True


def reader_thread(node_id, port, baud=115200):
    """Read serial lines from a node and parse them."""
    try:
        ser = serial.Serial(port, baud, timeout=0.5)
        while not stop_event.is_set():
            try:
                line = ser.readline()
                if line:
                    text = line.decode('utf-8', errors='replace').strip()
                    if text:
                        parse_line(node_id, text)
            except serial.SerialException:
                with lock:
                    stats[node_id]["boot"] = False
                    events_log.append((elapsed(), node_id, NODE_COLORS[node_id],
                                       f"{RED}Serial disconnected!{RESET}"))
                time.sleep(2)
                try:
                    ser.close()
                    ser = serial.Serial(port, baud, timeout=0.5)
                except:
                    pass
        ser.close()
    except Exception as e:
        with lock:
            events_log.append((elapsed(), node_id, NODE_COLORS[node_id],
                               f"{RED}ERROR: {e}{RESET}"))


# ── Main ─────────────────────────────────────────────────────────
stop_event = threading.Event()

def main():
    port0 = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    port1 = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM1"

    print(f"\n  Starting mesh demo on {port0} and {port1}...")
    print(f"  Make sure both ESP32-C3 boards are flashed with DEMO_TRAFFIC_ENABLED=1\n")
    time.sleep(1)

    # Hide cursor
    print("\033[?25l", end="")
    clear()

    t0 = threading.Thread(target=reader_thread, args=(0, port0), daemon=True)
    t1 = threading.Thread(target=reader_thread, args=(1, port1), daemon=True)
    t0.start()
    t1.start()

    try:
        while True:
            render()
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        # Show cursor, clear
        print("\033[?25h", end="")
        move(get_terminal_height(), 1)
        print(f"\n{BOLD}Demo ended.{RESET}")
        total_mesh = sum(s["rx"] for s in stats.values())
        total_app = sum(s["app_recv"] for s in stats.values())
        total_tx = sum(s["tx"] for s in stats.values())
        print(f"  Packets sent: {total_tx}  │  Packets received: {total_mesh}  │  App deliveries: {total_app}")
        print(f"  Node α MAC: {stats[0]['mac']}")
        print(f"  Node β MAC: {stats[1]['mac']}")
        print()

if __name__ == "__main__":
    main()
