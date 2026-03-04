#!/usr/bin/env python3
"""Simple TCP receive client for Linux-side verification."""

from __future__ import annotations

import argparse
import socket
import sys


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Receive forwarded serial bytes from tcp_seri server")
    p.add_argument("--server-ip", required=True, help="Windows host IP")
    p.add_argument("--server-port", type=int, default=8888)
    p.add_argument("--chunk-size", type=int, default=4096)
    p.add_argument("--save", default="", help="Optional output file to append binary data")
    p.add_argument("--show-hex", action="store_true", help="Print hex dump lines to stdout")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    out = open(args.save, "ab") if args.save else None

    try:
        with socket.create_connection((args.server_ip, args.server_port), timeout=10) as sock:
            print(f"Connected to {args.server_ip}:{args.server_port}")
            while True:
                data = sock.recv(args.chunk_size)
                if not data:
                    print("Server disconnected")
                    break

                if out:
                    out.write(data)
                    out.flush()

                if args.show_hex:
                    print(data.hex(" "))
                else:
                    try:
                        print(data.decode("utf-8", errors="replace"), end="")
                    except Exception:
                        print(data)
    finally:
        if out:
            out.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
