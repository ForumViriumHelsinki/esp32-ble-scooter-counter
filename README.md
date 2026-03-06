# ESP32 BLE Scooter Counter

An ESP32-based BLE (Bluetooth Low Energy) scanner that detects electric scooters by their BLE advertisements and publishes device data over MQTT. Built for research into parking area utilization of shared electric scooters.

Currently being tested in Helsinki, Finland, but the approach should work in any city where multiple scooter operators (Tier, Voi, Lime, Bird, Dott, Bolt, etc.) are present — their scooters continuously broadcast BLE advertisements that this device captures and reports.

> This project is primarily for research purposes. If you'd like to use or build on it, feel free to get in touch: **aapo.rista@forumvirium.fi**

## How It Works

The ESP32 performs continuous BLE scans and identifies scooters by matching advertised device names against known operator prefixes. Each detected BLE advertisement is published as a JSON message to an MQTT broker via WiFi. A companion Python script can subscribe to the MQTT topic and store all data in a SQLite database for later analysis.

### Dual-Core Architecture

- **Core 1** (Arduino loop): BLE scanning and device detection
- **Core 0** (FreeRTOS task): WiFi connection, NTP time sync, and MQTT publishing

Detected devices are passed between cores via a FreeRTOS queue, keeping scanning and network I/O fully decoupled.

### Detected Scooter Brands

The scanner matches device names starting with these prefixes:

- `Scooter` (generic)
- `TIER`
- `Voi`
- `Lime`
- `Bird`
- `Dott`
- `Bolt`

Additional prefixes can be added in `src/ble_scanner.cpp`.

## Hardware

The firmware should work on most ESP32 development boards. It has been tested with:

| Board | BLE | WiFi | OLED | LoRa | Notes |
|-------|-----|------|------|------|-------|
| **LILYGO T-Beam** | Yes | Yes | No | Yes* | Has GPS and LoRa radio |
| **TTGO LoRa32 V2.1** | Yes | Yes | Yes (SSD1306) | Yes* | Built-in OLED display |
| **ESP-WROOM-32** | Yes | Yes | No | No | Basic dev board, cheapest option |

*LoRa radio is present on the board but not currently used by the firmware (see [Future Plans](#future-plans)).

Boards with an OLED display will show live scan statistics when `HAS_OLED=1` is set in the build configuration.

## Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE plugin)
- An MQTT broker (e.g., [Mosquitto](https://mosquitto.org/))
- WiFi network accessible from the ESP32

## Getting Started

### 1. Clone the repository

```bash
git clone https://github.com/ForumViriumHelsinki/esp32-ble-scooter-counter.git
cd esp32-ble-scooter-counter
```

### 2. Configure WiFi and MQTT

Copy the example configuration and fill in your credentials:

```bash
cp src/config_example.h src/config.h
```

Edit `src/config.h` with your settings:

```cpp
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

#define MQTT_BROKER "your-mqtt-broker.example.com"
#define MQTT_PORT 1883
#define MQTT_USER ""        // Leave empty if no authentication
#define MQTT_PASSWORD ""    // Leave empty if no authentication
```

`config.h` is in `.gitignore` and will not be committed.

### 3. Build and upload

```bash
# Build for all configured boards
pio run

# Build and upload for a specific board
pio run -e wroom32 -t upload    # ESP-WROOM-32
pio run -e tbeam -t upload      # LILYGO T-Beam
pio run -e lora32 -t upload     # TTGO LoRa32

# Open serial monitor
pio device monitor
```

## MQTT Message Format

Each detected BLE device is published as a JSON message to the topic `scooter-counter/{SCANNER_MAC}/devices`, where `{SCANNER_MAC}` is the WiFi MAC address of the ESP32 (without colons).

```json
{
  "scanner_id": "AABBCCDDEEFF",
  "mac": "11:22:33:44:55:66",
  "addr_type": "random",
  "rssi": -72,
  "tx_power": -21,
  "name": "Voi_12345",
  "mfg_id": "FFFF",
  "mfg_data": "0102030405",
  "services": "0000fee7-0000-1000-8000-00805f9b34fb",
  "appearance": 0,
  "connectable": true,
  "adv_type": 0,
  "is_scooter": true,
  "uptime": 123456,
  "timestamp": "2026-01-19T17:30:45Z"
}
```

## Key Configuration

These constants can be adjusted in `src/main.cpp`:

| Constant | Default | Description |
|----------|---------|-------------|
| `SCAN_DURATION_SEC` | 10 | BLE scan duration per cycle (seconds) |
| `SCAN_INTERVAL_SEC` | 10 | Time between scan cycles (seconds) |
| `RSSI_THRESHOLD` | -100 | Minimum RSSI to accept (dBm) |
| `DEVICE_QUEUE_SIZE` | 50 | Max devices queued for MQTT publishing |

## Project Structure

```
esp32-ble-scooter-counter/
├── src/
│   ├── main.cpp            # Entry point, scan loop
│   ├── ble_scanner.cpp/h   # BLE scanning and scooter detection
│   ├── mqtt_handler.cpp/h  # WiFi, NTP, MQTT publishing (Core 0)
│   ├── device_data.h       # Shared device data structure
│   ├── display.cpp/h       # OLED display support (optional)
│   ├── utils.cpp/h         # Helper functions
│   ├── config_example.h    # Configuration template
│   └── config.h            # Your local config (gitignored)
├── scripts/
│   ├── mqtt_to_sqlite.py   # MQTT subscriber that logs to SQLite
│   └── analyze_scans.py    # Data analysis and visualization
├── platformio.ini          # PlatformIO build configuration
├── LICENSE
└── README.md
```

## Companion Scripts

### mqtt_to_sqlite.py

Subscribes to the MQTT topic and stores all received BLE scan data in a SQLite database. Supports buffered writes for performance and multiple scanners via wildcard subscription.

```bash
# Subscribe to all scanners and log to database
python scripts/mqtt_to_sqlite.py scans.db --host mqtt.example.com

# Subscribe to a specific scanner
python scripts/mqtt_to_sqlite.py scans.db --host mqtt.example.com --scanner-ids AABBCCDDEEFF

# See all options
python scripts/mqtt_to_sqlite.py --help
```

Requires: `paho-mqtt`

### analyze_scans.py

Generates statistics and visualizations from the SQLite database: device counts, scooter detection rates, RSSI distribution, dwell times, hourly activity patterns, and more.

```bash
# Run full analysis with plots
python scripts/analyze_scans.py scans.db

# Text-only analysis (no plots)
python scripts/analyze_scans.py scans.db --no-plots

# Filter by time range
python scripts/analyze_scans.py scans.db --start-time 2026-01-19T10:00:00 --end-time 2026-01-19T18:00:00
```

Requires: `pandas`, `matplotlib`, `seaborn`

## Libraries

| Library | Purpose |
|---------|---------|
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | Lightweight BLE stack (preferred over ESP32 BLE Arduino) |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MQTT client |
| [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) | OLED display driver (optional, for boards with display) |
| [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) | Graphics primitives for OLED (optional) |

## Future Plans

- **LoRaWAN support**: The current version uses WiFi/MQTT for data transmission. A future version may add LoRaWAN (e.g., via LMIC) as an alternative transport for deployments where WiFi is not available. Several of the supported boards already have LoRa radios.
- **Deep sleep**: Power-optimized mode with configurable scan/sleep cycles for battery-powered deployments.
- **On-device aggregation**: Summarize scooter counts on the device and transmit periodic reports instead of raw advertisement data.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

## Contact

This project is developed by [Forum Virium Helsinki](https://forumvirium.fi/).

For questions, feedback, or collaboration inquiries: **aapo.rista@forumvirium.fi**
