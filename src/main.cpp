/**
 * ESP32 BLE Scooter Counter - WiFi/MQTT Version
 *
 * Scans for BLE advertisements and publishes discovered devices via MQTT.
 * Uses dual-core architecture:
 * - Core 1 (Arduino loop): BLE scanning, device detection
 * - Core 0 (MQTT task): WiFi connection, MQTT publishing
 */

/**
 * TODO:
 * [ ]
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"

// Module headers
#include "utils.h"
#include "ble_scanner.h"
#include "mqtt_handler.h"
#include "display.h"

// Configuration
constexpr uint32_t SCAN_DURATION_SEC = 10;
constexpr uint32_t SCAN_INTERVAL_SEC = 10;
constexpr int8_t RSSI_THRESHOLD = -100;

// Queue settings
constexpr size_t DEVICE_QUEUE_SIZE = 50;

// Device data structure for queue
struct DeviceData
{
    char mac[18];      // "AA:BB:CC:DD:EE:FF\0"
    char addrType[12]; // "public", "random", etc.
    int rssi;
    int8_t txPower;
    bool hasTxPower;
    char name[32];
    char mfgId[5];      // "XXXX\0"
    char mfgData[64];   // Hex string (limited length)
    char services[128]; // Semicolon-separated UUIDs
    uint16_t appearance;
    bool hasAppearance;
    bool connectable;
    uint8_t advType;
    bool isScooter;
    uint32_t uptime;
    char timestamp[32]; // "2026-02-09T14:30:45Z\0"
};

// Global objects
QueueHandle_t deviceQueue = nullptr;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
char mqttClientId[32];
char scannerMacAddress[18]; // "AA:BB:CC:DD:EE:FF\0"
char mqttTopic[64];         // "scooter-counter/AABBCCDDEEFF/devices\0"

// Global counters
uint32_t totalDevicesFound = 0;
uint32_t scootersFound = 0;
uint32_t mqttPublished = 0;
uint32_t mqttDropped = 0;

// BLE scan callbacks
static ScanCallbacks scanCallbacks;

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("================================");
    Serial.println("  ESP32 BLE Scooter Counter");
    Serial.println("  WiFi/MQTT Version");
    Serial.println("================================");

#if defined(BOARD_TBEAM)
    Serial.println("Board: LILYGO T-Beam");
#elif defined(BOARD_LORA32)
    Serial.println("Board: TTGO LoRa32");
#else
    Serial.println("Board: Unknown");
#endif

    Serial.printf("Scan duration: %d sec\n", SCAN_DURATION_SEC);
    Serial.printf("Scan interval: %d sec\n", SCAN_INTERVAL_SEC);
    Serial.printf("RSSI threshold: %d dBm\n", RSSI_THRESHOLD);
    Serial.println();

    // Get WiFi MAC address
    uint8_t mac[6];
    WiFi.macAddress(mac);

    // Format MAC address as string (AA:BB:CC:DD:EE:FF)
    snprintf(scannerMacAddress, sizeof(scannerMacAddress), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Generate unique MQTT client ID
    snprintf(mqttClientId, sizeof(mqttClientId), "%s%02X%02X%02X",
             MQTT_CLIENT_PREFIX, mac[3], mac[4], mac[5]);

    // Build dynamic MQTT topic with MAC address (without colons for cleaner topic)
    snprintf(mqttTopic, sizeof(mqttTopic), "scooter-counter/%02X%02X%02X%02X%02X%02X/devices",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    Serial.printf("Scanner MAC: %s\n", scannerMacAddress);
    Serial.printf("MQTT Client ID: %s\n", mqttClientId);
    Serial.printf("MQTT Topic: %s\n", mqttTopic);

#if HAS_OLED
    // Initialize OLED display
    oledInit();
    oledUpdateStatus("Starting...", "WiFi connecting", "");
#endif

    // Create device queue
    deviceQueue = xQueueCreate(DEVICE_QUEUE_SIZE, sizeof(DeviceData));
    if (deviceQueue == nullptr)
    {
        Serial.println("ERROR: Failed to create device queue!");
        while (true)
        {
            delay(1000);
        }
    }
    Serial.printf("Device queue created (size: %d)\n", DEVICE_QUEUE_SIZE);

    // Start MQTT task on Core 0
    startMqttTask();

    // Initialize BLE
    bleInit(&scanCallbacks);
}

void loop()
{
    // Reset counters for this scan
    totalDevicesFound = 0;
    scootersFound = 0;

    char timeStr[32];
    if (getISO8601Time(timeStr, sizeof(timeStr)))
    {
        Serial.printf("--- Starting BLE scan at %s ---\n", timeStr);
    }
    else
    {
        Serial.println("--- Starting BLE scan ---");
    }

    // Start blocking scan
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->getResults(SCAN_DURATION_SEC * 1000, false);

    Serial.printf("\n=== Scan complete ===\n");
    Serial.printf("Devices found: %d, Scooters: %d\n", totalDevicesFound, scootersFound);
    Serial.printf("MQTT published: %d, Dropped: %d\n", mqttPublished, mqttDropped);
    Serial.println();

#if HAS_OLED
    // Update OLED with scan statistics
    oledUpdateScanStats(totalDevicesFound, scootersFound, mqttPublished, mqttDropped);
#endif

    // Clear results to free memory
    scan->clearResults();

    // Wait before next scan
    uint32_t waitTime = SCAN_INTERVAL_SEC - SCAN_DURATION_SEC;
    Serial.printf("Next scan in %d seconds...\n\n", waitTime);
    delay(waitTime * 1000);
}
