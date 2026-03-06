# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based BLE scooter detector that identifies electric scooters via BLE advertisements and publishes device data over WiFi/MQTT. Used for research into parking area utilization of shared electric scooters in Helsinki. LoRaWAN support may be added in the future for WiFi-free deployments.

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

1. **BLE Scanner** (`ble_scanner.cpp/h`) - Scans for BLE advertisements, filters by device name prefixes (e.g., "Scooter", "TIER", "Voi", "Lime", "Bird", "Dott", "Bolt")
2. **MQTT Handler** (`mqtt_handler.cpp/h`) - WiFi connection, NTP time sync, MQTT publishing (runs on Core 0)
3. **Device Data** (`device_data.h`) - Shared data structure passed via FreeRTOS queue between cores
4. **Display** (`display.cpp/h`) - Optional OLED display support (SSD1306, for boards with `HAS_OLED=1`)

### Dual-Core Architecture

- **Core 1** (Arduino loop): BLE scanning, device detection
- **Core 0** (FreeRTOS task): WiFi/MQTT connection and publishing

Detected devices are passed between cores via a FreeRTOS queue.

### Key Configuration Constants

```cpp
SCAN_DURATION_SEC       10      // BLE scan duration
SCAN_INTERVAL_SEC       10      // Time between scans
RSSI_THRESHOLD          -100    // Minimum RSSI in dBm
DEVICE_QUEUE_SIZE       50      // Max devices queued for MQTT
```

### Board Environments

Three PlatformIO environments are configured:
- `tbeam` - LILYGO T-Beam (LoRa radio present but unused)
- `lora32` - TTGO LoRa32 V2.1 (with SSD1306 OLED)
- `wroom32` - ESP-WROOM-32 (basic dev board)

## Libraries

- `NimBLE-Arduino` - Lightweight BLE stack (preferred over ESP32 BLE Arduino)
- `PubSubClient` - MQTT client
- `Adafruit SSD1306` + `Adafruit GFX` - OLED display (optional)

## Companion Scripts

- `scripts/mqtt_to_sqlite.py` - Subscribes to MQTT and logs BLE scan data to SQLite
- `scripts/analyze_scans.py` - Generates statistics and plots from the SQLite database

## Future Plans

LoRaWAN support may be added as an alternative transport for WiFi-free deployments. Several supported boards already have LoRa radios.
