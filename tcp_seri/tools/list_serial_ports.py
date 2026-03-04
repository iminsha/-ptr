#!/usr/bin/env python3
"""List available serial ports on Windows for quick setup."""

from __future__ import annotations

from serial.tools import list_ports


for p in list_ports.comports():
    print(f"{p.device}\t{p.description}")
