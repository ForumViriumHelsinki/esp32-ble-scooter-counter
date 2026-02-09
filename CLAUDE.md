# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based BLE scooter detector that identifies electric scooters via BLE advertisements and reports counts over LoRaWAN. Used for monitoring parking area utilization in Helsinki.

## Build System

This is a PlatformIO project using the Arduino framework. Once set up:

```bash
# Build
pio run

# Upload to device
pio run --target upload

# Serial monitor
pio device monitor
```

## Architecture

### Core Components

1. **BLE Scanner** - Scans for BLE advertisements, filters by device name prefixes (e.g., "Scooter", "TIER", "Voi", "Lime", "Bird")
2. **Device Tracker** - Maintains state of detected devices in RTC memory (survives deep sleep)
3. **LoRaWAN Transmitter** - Sends aggregated counts via Digita LoRaWAN (OTAA activation)
4. **Power Manager** - Coordinates deep sleep cycles

### Device State Machine

Devices transition through three states per reporting period:
- **NEW** - First detection in current period
- **PRESENT** - Previously seen, still visible
- **DEPARTED** - Not seen for N consecutive scans (configurable, default 3)

### Memory Constraints

- RTC slow memory: 8 KB available
- Device record: 7 bytes (3-byte MAC suffix + 4-byte timestamps)
- Max tracked devices: ~200 (with safety margin)

### LoRaWAN Payload Format (51 bytes max)

```
Byte 0:     Version + flags (bits 0-3: version, bit 4: list cleared flag)
Byte 1:     Scan count in reporting period
Byte 2:     New device count
Byte 3:     Present device count
Byte 4:     Departed device count
Byte 5:     Longest parking duration (in reporting periods)
Bytes 6-50: Reserved for extensions
```

### Key Configuration Constants

```cpp
SCAN_DURATION_SEC       10      // BLE scan duration
SCAN_INTERVAL_SEC       60      // Time between scans (including sleep)
REPORT_INTERVAL_SEC     300     // LoRaWAN transmission interval
RSSI_THRESHOLD          -90     // Minimum RSSI in dBm
MISSING_SCANS_LIMIT     3       // Scans before marking device departed
```

## Libraries

- `NimBLE-Arduino` - Lightweight BLE stack (preferred over ESP32 BLE Arduino)
- `MCCI LoRaWAN LMIC library` or `LMIC-node` - LoRaWAN stack
