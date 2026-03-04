# mySmart Qt UI Prototype

This folder contains a Qt Widgets prototype UI for your board project.
It now includes a simple serial test panel for host-board bring-up.

## Features in this prototype
- Runtime status: mode, RTC, temperature, actuator position, error state
- Action center buttons: MOVE/STOP/BEEP/CLEAR ERR
- Config panel (EEPROM mirror idea): TH/TL/cooldown/default mode
- Schedule table: add/remove demo tasks
- Log panel: all UI interactions appended with timestamps
- Serial panel: COM refresh/open/close, send text, RX log

## Build (Qt 5.14.2 + CMake)
1. Install Qt 5.14.2 modules: `Widgets` and `SerialPort`.
2. In this folder run:
   - `cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt5.14.2>/msvc2017_64`
   - `cmake --build build --config Release`
3. Run executable `mySmartHostUI` from the build output.

## Quick serial test
1. Open COM with 9600 baud.
2. Send commands:
   - `PING`
   - `BEEP1` / `BEEP3`
   - `GETCFG`
   - `STOP`
3. Check RX log and board LCD/buzzer response.
