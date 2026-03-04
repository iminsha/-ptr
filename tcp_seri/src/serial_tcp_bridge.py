#!/usr/bin/env python3
"""
TCP <-> Serial bridge (Windows recommended).

Features:
- Opens a physical serial port (e.g. COM3).
- Runs a TCP server (default :8888), accepts one client at a time.
- Bidirectional forwarding: Serial <-> TCP (raw bytes).
- Keeps serial port open across client disconnects.
- Auto-reopen serial if the USB-serial device disconnects/reconnects.
- Graceful shutdown on Ctrl+C.

Notes:
- Package name: pyserial
  pip install pyserial -i https://pypi.tuna.tsinghua.edu.cn/simple
"""

from __future__ import annotations

import argparse
import logging
import socket
import sys
import threading
import time
from typing import Optional

import serial
from serial import SerialException


# ---------------------- CLI / Logging ---------------------- #

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Bidirectional TCP <-> Serial bridge")
    p.add_argument("--serial-port", default="COM3", help="Physical serial port, e.g. COM3")
    p.add_argument("--baudrate", type=int, default=4800)
    p.add_argument("--bytesize", type=int, choices=[5, 6, 7, 8], default=8)
    p.add_argument("--parity", choices=["N", "E", "O", "M", "S"], default="N")
    p.add_argument("--stopbits", type=float, choices=[1, 1.5, 2], default=1)
    p.add_argument("--serial-timeout", type=float, default=0.1, help="seconds (read timeout)")
    p.add_argument("--bind-host", default="0.0.0.0")
    p.add_argument("--bind-port", type=int, default=8888)
    p.add_argument("--reconnect-delay", type=float, default=1.0, help="seconds (serial reopen delay)")
    p.add_argument("--tcp-nodelay", action="store_true", help="Disable Nagle (lower latency)")
    p.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR"])
    return p.parse_args()


def setup_logging(level: str) -> None:
    logging.basicConfig(
        level=getattr(logging, level),
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )


# ---------------------- Serial Helpers ---------------------- #

def _map_bytesize(n: int) -> int:
    return {
        5: serial.FIVEBITS,
        6: serial.SIXBITS,
        7: serial.SEVENBITS,
        8: serial.EIGHTBITS,
    }[n]


def _map_parity(p: str) -> str:
    return {
        "N": serial.PARITY_NONE,
        "E": serial.PARITY_EVEN,
        "O": serial.PARITY_ODD,
        "M": serial.PARITY_MARK,
        "S": serial.PARITY_SPACE,
    }[p]


def _map_stopbits(s: float) -> float:
    return {
        1: serial.STOPBITS_ONE,
        1.5: serial.STOPBITS_ONE_POINT_FIVE,
        2: serial.STOPBITS_TWO,
    }[s]


def open_serial_with_retry(args: argparse.Namespace) -> serial.Serial:
    """Open serial and keep retrying until success."""
    while True:
        try:
            ser = serial.Serial(
                port=args.serial_port,
                baudrate=args.baudrate,
                bytesize=_map_bytesize(args.bytesize),
                parity=_map_parity(args.parity),
                stopbits=_map_stopbits(args.stopbits),
                timeout=args.serial_timeout,
                write_timeout=1.0,
            )
            # 不主动拉 DTR/RTS（有些板子连着复位线会抖动）
            try:
                ser.dtr = False
                ser.rts = False
            except Exception:
                pass

            logging.info("Serial opened: %s @ %d %d%s%.1f",
                         args.serial_port, args.baudrate, args.bytesize, args.parity, args.stopbits)
            return ser
        except SerialException as exc:
            logging.error("Open serial failed (%s). retry in %.1fs", exc, args.reconnect_delay)
            time.sleep(args.reconnect_delay)


# ---------------------- TCP Server ---------------------- #

def create_server(host: str, port: int) -> socket.socket:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(1)
    logging.info("TCP server listening on %s:%d", host, port)
    return srv


# ---------------------- Forwarding Threads ---------------------- #

class StopFlag:
    def __init__(self) -> None:
        self._evt = threading.Event()

    def stop(self) -> None:
        self._evt.set()

    def is_set(self) -> bool:
        return self._evt.is_set()

    def wait(self, t: float) -> bool:
        return self._evt.wait(t)


def pump_serial_to_tcp(ser: serial.Serial, client: socket.socket, stop: StopFlag) -> None:
    """Read bytes from serial and send to TCP client."""
    total = 0
    while not stop.is_set():
        try:
            data = ser.read(4096)
            if not data:
                continue

            log_bytes("S->T", data)
            client.sendall(data)
            total += len(data)
            logging.debug("S->T %d bytes (total=%d)", len(data), total)
        except (SerialException, OSError) as exc:
            logging.warning("Serial read/send error: %s", exc)
            stop.stop()
            return
        except (BrokenPipeError, ConnectionResetError) as exc:
            logging.info("Client disconnected (S->T): %s", exc)
            stop.stop()
            return


def pump_tcp_to_serial(ser: serial.Serial, client: socket.socket, stop: StopFlag) -> None:
    """Read bytes from TCP client and write to serial."""
    total = 0
    client.settimeout(0.5)  # 避免永久阻塞，方便响应 stop
    while not stop.is_set():
        try:
            try:
                data = client.recv(4096)
            except socket.timeout:
                continue

            if not data:
                logging.info("Client closed (T->S)")
                stop.stop()
                return
            log_bytes("T->S", data)
            ser.write(data)
            total += len(data)
            logging.debug("T->S %d bytes (total=%d)", len(data), total)
        except (SerialException, OSError) as exc:
            logging.warning("Serial write error: %s", exc)
            stop.stop()
            return
        except (BrokenPipeError, ConnectionResetError) as exc:
            logging.info("Client disconnected (T->S): %s", exc)
            stop.stop()
            return
def log_bytes(prefix: str, data: bytes) -> None:
    """Log bytes in HEX + ASCII for debugging."""
    if not data:
        return

    hex_str = data.hex(" ")
    ascii_str = "".join(chr(b) if 32 <= b <= 126 else "." for b in data)

    logging.info("%s | len=%d | HEX=[%s] | ASCII=[%s]",
                 prefix, len(data), hex_str, ascii_str)

# ---------------------- Main Loop ---------------------- #

def main() -> int:
    args = parse_args()
    setup_logging(args.log_level)

    srv: Optional[socket.socket] = None
    ser: Optional[serial.Serial] = None

    try:
        srv = create_server(args.bind_host, args.bind_port)

        # 串口一直保持打开；如果断开则自动重连
        ser = open_serial_with_retry(args)

        while True:
            logging.info("Waiting for TCP client...")
            client, addr = srv.accept()
            logging.info("Client connected: %s:%d", addr[0], addr[1])

            if args.tcp_nodelay:
                try:
                    client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                except OSError:
                    pass

            stop = StopFlag()

            t1 = threading.Thread(target=pump_serial_to_tcp, args=(ser, client, stop), daemon=True)
            t2 = threading.Thread(target=pump_tcp_to_serial, args=(ser, client, stop), daemon=True)
            t1.start()
            t2.start()

            # 等待任一方向停止
            while not stop.is_set():
                time.sleep(0.1)

            # 清理客户端
            try:
                client.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                client.close()
            except OSError:
                pass
            logging.info("Client closed")

            # 检查串口是否还活着：若 USB 串口掉线，pyserial 往往会抛异常并关闭
            # 这里做一次轻量探测：若已关闭则重新打开
            if ser is None or (hasattr(ser, "is_open") and not ser.is_open):
                logging.warning("Serial closed unexpectedly. Reopening...")
                try:
                    if ser is not None:
                        ser.close()
                except Exception:
                    pass
                ser = open_serial_with_retry(args)

    except KeyboardInterrupt:
        logging.info("Interrupted by user")
    finally:
        if ser is not None:
            try:
                if ser.is_open:
                    ser.close()
                    logging.info("Serial closed")
            except Exception:
                pass
        if srv is not None:
            try:
                srv.close()
                logging.info("Server closed")
            except OSError:
                pass

    return 0


if __name__ == "__main__":
    sys.exit(main())