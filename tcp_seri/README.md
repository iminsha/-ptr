# tcp_seri

Python tool for forwarding physical serial data from Windows to Linux via TCP.

Current phase implemented:
- One-way forwarding (Windows serial -> TCP -> Linux client)
- TCP server default port: 8888

## Project layout

- `src/serial_tcp_server.py`: Windows side serial-to-TCP server
- `src/tcp_recv_client.py`: Linux side receive client for verification
- `tools/list_serial_ports.py`: List available serial ports
- `requirements.txt`: Python dependencies

## 1) Install dependencies

On both Windows and Linux:

```bash
python -m pip install -r requirements.txt
```

## 2) Windows side: find serial port

```bash
python tools/list_serial_ports.py
```

Example output:

```text
COM3    USB-SERIAL CH340
```

## 3) Windows side: run forwarding server (port 8888)

```bash
python src/serial_tcp_server.py --serial-port COM3 --baudrate 115200 --bind-port 8888
```

Common optional params:

- `--parity N|E|O|M|S` (default `N`)
- `--stopbits 1|1.5|2` (default `1`)
- `--bytesize 5|6|7|8` (default `8`)
- `--log-level DEBUG|INFO|WARNING|ERROR`

## 4) Linux side: connect and verify data

Text view:

```bash
python3 src/tcp_recv_client.py --server-ip <WINDOWS_IP> --server-port 8888
```

Hex view:

```bash
python3 src/tcp_recv_client.py --server-ip <WINDOWS_IP> --server-port 8888 --show-hex
```

Save binary stream:

```bash
python3 src/tcp_recv_client.py --server-ip <WINDOWS_IP> --server-port 8888 --save serial_dump.bin
```

## 5) Quick network checks

From Linux, verify TCP reachability:

```bash
nc -vz <WINDOWS_IP> 8888
```

If failed:
- ensure Windows firewall allows inbound TCP 8888
- ensure Linux and Windows are in reachable network/subnet

## Notes

- Server accepts one TCP client at a time.
- If client disconnects, server keeps running and waits for next client.
- Next phase (not yet implemented): bidirectional forwarding (TCP -> serial command write-back).
