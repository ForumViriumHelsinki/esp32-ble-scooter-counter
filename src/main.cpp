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
 * [ ] rename timestamp to uptime and isotime to timestamp
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include "config.h"

// OLED display support (conditional)
#if HAS_OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

// Configuration
constexpr uint32_t SCAN_DURATION_SEC = 10;
constexpr uint32_t SCAN_INTERVAL_SEC = 10;
constexpr int8_t RSSI_THRESHOLD = -100;

// Debug settings
constexpr bool DEBUG_MQTT = false; // Set to true to print each published message

// OLED display settings
#if HAS_OLED
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr uint8_t OLED_ADDR = 0x3C;
constexpr int8_t OLED_RESET = -1; // Reset pin (-1 = share Arduino reset pin)
// Note: OLED_SDA and OLED_SCL are already defined in pins_arduino.h for this board
#endif

// Queue settings
constexpr size_t DEVICE_QUEUE_SIZE = 50;

// WiFi/MQTT retry settings
constexpr uint32_t WIFI_RETRY_DELAY_MS = 5000;
constexpr uint32_t MQTT_RETRY_DELAY_MS = 5000;
constexpr uint32_t MQTT_LOOP_DELAY_MS = 10;
constexpr uint32_t MQTT_STATS_INTERVAL_MS = 60000; // Print stats every 60 seconds

// NTP settings
const char *NTP_SERVER = "pool.ntp.org";
const char *NTP_SERVER2 = "time.nist.gov";
const long GMT_OFFSET_SEC = 0;     // UTC time
const int DAYLIGHT_OFFSET_SEC = 0; // No daylight saving

// Known scooter name prefixes
const char *SCOOTER_PREFIXES[] = {
    "Scooter",
    "TIER",
    "Voi",
    "Lime",
    "Bird",
    "Dott",
    "Ruuvi",
    "Bolt"};
constexpr size_t NUM_PREFIXES = sizeof(SCOOTER_PREFIXES) / sizeof(SCOOTER_PREFIXES[0]);

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
    uint32_t timestamp;
    char isotime[32]; // "2026-02-09T14:30:45Z\0"
};

// Global objects
static QueueHandle_t deviceQueue = nullptr;
static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static char mqttClientId[32];
static char scannerMacAddress[18]; // "AA:BB:CC:DD:EE:FF\0"
static char mqttTopic[64];         // "scooter-counter/AABBCCDDEEFF/devices\0"

#if HAS_OLED
static Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
#endif

static uint32_t totalDevicesFound = 0;
static uint32_t scootersFound = 0;
static uint32_t mqttPublished = 0;
static uint32_t mqttDropped = 0;

// Forward declarations for OLED functions (must be outside #if block to be visible everywhere)
#if HAS_OLED
void oledInit();
void oledUpdateStatus(const char *status1, const char *status2 = nullptr, const char *status3 = nullptr);
void oledUpdateScanStats(uint32_t devices, uint32_t scooters, uint32_t published, uint32_t dropped);
#endif

/**
 * Get current time as ISO8601 UTC string
 * Returns empty string if time is not yet synchronized
 */
bool getISO8601Time(char *buffer, size_t bufferLen)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    if (now < 8 * 3600 * 24) // Less than ~8 days since epoch = not synchronized
    {
        buffer[0] = '\0';
        return false;
    }

    gmtime_r(&now, &timeinfo);
    strftime(buffer, bufferLen, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return true;
}

/**
 * Check if device name starts with any known scooter prefix
 */
bool isScooterName(const std::string &name)
{
    if (name.empty())
    {
        return false;
    }
    for (size_t i = 0; i < NUM_PREFIXES; i++)
    {
        if (name.rfind(SCOOTER_PREFIXES[i], 0) == 0)
        {
            return true;
        }
    }
    return false;
}

/**
 * Convert bytes to hex string
 */
void bytesToHex(const uint8_t *data, size_t len, char *output, size_t outputLen)
{
    size_t maxBytes = (outputLen - 1) / 2;
    size_t bytesToConvert = (len < maxBytes) ? len : maxBytes;

    for (size_t i = 0; i < bytesToConvert; i++)
    {
        snprintf(output + (i * 2), 3, "%02X", data[i]);
    }
    output[bytesToConvert * 2] = '\0';
}

/**
 * Get address type string
 */
const char *getAddrTypeStr(uint8_t addrType)
{
    switch (addrType)
    {
    case BLE_ADDR_PUBLIC:
        return "public";
    case BLE_ADDR_RANDOM:
        return "random";
    case BLE_ADDR_PUBLIC_ID:
        return "public_id";
    case BLE_ADDR_RANDOM_ID:
        return "random_id";
    default:
        return "unknown";
    }
}

/**
 * Escape JSON string (handles quotes and backslashes)
 */
void escapeJsonString(const char *input, char *output, size_t outputLen)
{
    size_t j = 0;
    for (size_t i = 0; input[i] != '\0' && j < outputLen - 1; i++)
    {
        char c = input[i];
        if (c == '"' || c == '\\')
        {
            if (j + 2 >= outputLen)
                break;
            output[j++] = '\\';
            output[j++] = c;
        }
        else if (c >= 32 && c < 127)
        {
            output[j++] = c;
        }
        // Skip control characters
    }
    output[j] = '\0';
}

/**
 * Build JSON message from device data
 */
void buildJsonMessage(const DeviceData &dev, char *buffer, size_t bufferLen)
{
    char escapedName[64];
    escapeJsonString(dev.name, escapedName, sizeof(escapedName));

    int written = snprintf(buffer, bufferLen,
                           "{"
                           "\"scanner_id\":\"%s\","
                           "\"mac\":\"%s\","
                           "\"addr_type\":\"%s\","
                           "\"rssi\":%d,",
                           scannerMacAddress,
                           dev.mac,
                           dev.addrType,
                           dev.rssi);

    // TX Power (optional)
    if (dev.hasTxPower)
    {
        written += snprintf(buffer + written, bufferLen - written,
                            "\"tx_power\":%d,", dev.txPower);
    }

    // Name
    written += snprintf(buffer + written, bufferLen - written,
                        "\"name\":\"%s\",", escapedName);

    // Manufacturer data (optional)
    if (dev.mfgId[0] != '\0')
    {
        written += snprintf(buffer + written, bufferLen - written,
                            "\"mfg_id\":\"%s\",\"mfg_data\":\"%s\",",
                            dev.mfgId, dev.mfgData);
    }

    // Services (optional)
    if (dev.services[0] != '\0')
    {
        // Convert semicolon-separated to JSON array
        written += snprintf(buffer + written, bufferLen - written, "\"services\":[");
        char servicesCopy[128];
        strncpy(servicesCopy, dev.services, sizeof(servicesCopy) - 1);
        servicesCopy[sizeof(servicesCopy) - 1] = '\0';

        char *token = strtok(servicesCopy, ";");
        bool first = true;
        while (token != nullptr)
        {
            if (!first)
            {
                written += snprintf(buffer + written, bufferLen - written, ",");
            }
            written += snprintf(buffer + written, bufferLen - written, "\"%s\"", token);
            first = false;
            token = strtok(nullptr, ";");
        }
        written += snprintf(buffer + written, bufferLen - written, "],");
    }

    // Appearance (optional)
    if (dev.hasAppearance)
    {
        written += snprintf(buffer + written, bufferLen - written,
                            "\"appearance\":%d,", dev.appearance);
    }

    // Final fields
    written += snprintf(buffer + written, bufferLen - written,
                        "\"connectable\":%s,"
                        "\"adv_type\":%d,"
                        "\"is_scooter\":%s,"
                        "\"timestamp\":%u",
                        dev.connectable ? "true" : "false",
                        dev.advType,
                        dev.isScooter ? "true" : "false",
                        dev.timestamp);

    // ISO time (optional, only if synchronized)
    if (dev.isotime[0] != '\0')
    {
        written += snprintf(buffer + written, bufferLen - written,
                            ",\"isotime\":\"%s\"", dev.isotime);
    }

    snprintf(buffer + written, bufferLen - written, "}");
}

/**
 * Connect to WiFi
 */
bool connectWiFi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return true;
    }

    Serial.printf("[MQTT] Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000)
    {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("\n[MQTT] WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
#if HAS_OLED
        char ipStr[32];
        snprintf(ipStr, sizeof(ipStr), "IP: %s", WiFi.localIP().toString().c_str());
        oledUpdateStatus("WiFi connected", ipStr, "Connecting MQTT...");
#endif
        return true;
    }
    else
    {
        Serial.println("\n[MQTT] WiFi connection failed");
        return false;
    }
}

/**
 * Connect to MQTT broker
 */
bool connectMQTT()
{
    if (mqtt.connected())
    {
        return true;
    }

    Serial.printf("[MQTT] Connecting to broker: %s:%d\n", MQTT_BROKER, MQTT_PORT);

    bool connected;
    if (strlen(MQTT_USER) > 0)
    {
        connected = mqtt.connect(mqttClientId, MQTT_USER, MQTT_PASSWORD);
    }
    else
    {
        connected = mqtt.connect(mqttClientId);
    }

    if (connected)
    {
        Serial.printf("[MQTT] Connected as %s\n", mqttClientId);
#if HAS_OLED
        oledUpdateStatus("MQTT connected", "Ready to scan", "");
#endif
        return true;
    }
    else
    {
        Serial.printf("[MQTT] Connection failed, rc=%d\n", mqtt.state());
        return false;
    }
}

/**
 * MQTT task - runs on Core 0
 */
void mqttTask(void *param)
{
    Serial.println("[MQTT] Task started on Core 0");

    // Initial WiFi connection
    while (!connectWiFi())
    {
        vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
    }

    // Configure NTP time synchronization
    Serial.println("[MQTT] Configuring NTP time sync...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER, NTP_SERVER2);

    // Wait for time to be set (max 10 seconds)
    Serial.print("[MQTT] Waiting for NTP sync");
    int retries = 0;
    time_t now = 0;
    while (now < 8 * 3600 * 24 && retries < 20)
    {
        time(&now);
        delay(500);
        Serial.print(".");
        retries++;
    }
    Serial.println();

    if (now > 8 * 3600 * 24)
    {
        char timeStr[32];
        getISO8601Time(timeStr, sizeof(timeStr));
        Serial.printf("[MQTT] NTP synchronized: %s\n", timeStr);
    }
    else
    {
        Serial.println("[MQTT] NTP sync timeout (will retry in background)");
    }

    // Configure MQTT
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setBufferSize(512); // Increase buffer for JSON messages

    DeviceData device;
    char jsonBuffer[512];
    uint32_t lastStatsReport = millis();

    while (true)
    {
        // Ensure WiFi is connected
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("[MQTT] WiFi disconnected, reconnecting...");
            if (!connectWiFi())
            {
                vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
                continue;
            }
        }

        // Ensure MQTT is connected
        if (!mqtt.connected())
        {
            if (!connectMQTT())
            {
                vTaskDelay(pdMS_TO_TICKS(MQTT_RETRY_DELAY_MS));
                continue;
            }
        }

        // Keep MQTT connection alive
        mqtt.loop();

        // Try to receive from queue (blocking with timeout)
        if (xQueueReceive(deviceQueue, &device, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            // Build JSON message
            buildJsonMessage(device, jsonBuffer, sizeof(jsonBuffer));

            // Publish
            if (mqtt.publish(mqttTopic, jsonBuffer))
            {
                mqttPublished++;
                if (DEBUG_MQTT)
                {
                    Serial.printf("[MQTT] Published: %s (RSSI: %d)\n", device.mac, device.rssi);
                }
            }
            else
            {
                mqttDropped++;
                if (DEBUG_MQTT)
                {
                    Serial.printf("[MQTT] Failed to publish: %s\n", device.mac);
                }
            }
        }

        // Print periodic stats report (only in non-debug mode)
        if (!DEBUG_MQTT && (millis() - lastStatsReport >= MQTT_STATS_INTERVAL_MS))
        {
            char timeStr[32];
            if (getISO8601Time(timeStr, sizeof(timeStr)))
            {
                Serial.printf("[MQTT] %s - Published: %d, Dropped: %d\n", timeStr, mqttPublished, mqttDropped);
            }
            else
            {
                Serial.printf("[MQTT] Published: %d, Dropped: %d\n", mqttPublished, mqttDropped);
            }
            lastStatsReport = millis();
        }

        // Small delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(MQTT_LOOP_DELAY_MS));
    }
}

/**
 * BLE scan callback - queues device data for MQTT publishing
 */
class ScanCallbacks : public NimBLEScanCallbacks
{
    void onResult(const NimBLEAdvertisedDevice *device) override
    {
        totalDevicesFound++;

        DeviceData devData = {};

        // MAC address
        std::string addr = device->getAddress().toString();
        strncpy(devData.mac, addr.c_str(), sizeof(devData.mac) - 1);

        // Address type
        uint8_t addrType = device->getAddress().getType();
        strncpy(devData.addrType, getAddrTypeStr(addrType), sizeof(devData.addrType) - 1);

        // RSSI
        devData.rssi = device->getRSSI();

        // Name
        std::string name = device->getName();
        strncpy(devData.name, name.c_str(), sizeof(devData.name) - 1);

        // Is scooter
        devData.isScooter = isScooterName(name);
        if (devData.isScooter)
        {
            scootersFound++;
        }

        // TX Power
        devData.hasTxPower = device->haveTXPower();
        if (devData.hasTxPower)
        {
            devData.txPower = device->getTXPower();
        }

        // Manufacturer data
        if (device->haveManufacturerData())
        {
            std::string mfgData = device->getManufacturerData();
            if (mfgData.length() >= 2)
            {
                uint16_t companyId = (uint8_t)mfgData[0] | ((uint8_t)mfgData[1] << 8);
                snprintf(devData.mfgId, sizeof(devData.mfgId), "%04X", companyId);

                if (mfgData.length() > 2)
                {
                    bytesToHex((const uint8_t *)mfgData.data() + 2,
                               mfgData.length() - 2,
                               devData.mfgData,
                               sizeof(devData.mfgData));
                }
            }
        }

        // Service UUIDs
        if (device->haveServiceUUID())
        {
            String services;
            for (size_t i = 0; i < device->getServiceUUIDCount(); i++)
            {
                if (i > 0)
                {
                    services += ";";
                }
                services += device->getServiceUUID(i).toString().c_str();
            }
            strncpy(devData.services, services.c_str(), sizeof(devData.services) - 1);
        }

        // Appearance
        devData.hasAppearance = device->haveAppearance();
        if (devData.hasAppearance)
        {
            devData.appearance = device->getAppearance();
        }

        // Connectable and advertising type
        devData.connectable = device->isConnectable();
        devData.advType = device->getAdvType();

        // Timestamp (millis since boot)
        devData.timestamp = millis();

        // ISO time (UTC)
        getISO8601Time(devData.isotime, sizeof(devData.isotime));

        // Send to queue (non-blocking)
        if (xQueueSend(deviceQueue, &devData, 0) != pdTRUE)
        {
            // Queue full, drop this device
            mqttDropped++;
        }
    }
};

static ScanCallbacks scanCallbacks;

#if HAS_OLED
/**
 * Initialize OLED display
 */
void oledInit()
{
    Wire.begin(); // Use default I2C pins (SDA=21, SCL=22 on TTGO LoRa32)

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
    {
        Serial.println("ERROR: SSD1306 allocation failed!");
        return;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("BLE Scooter");
    display.println("Counter");
    display.println();
    display.println("Initializing...");
    display.display();

    Serial.println("OLED display initialized");
}

/**
 * Update OLED display with current status
 */
void oledUpdateStatus(const char *status1, const char *status2, const char *status3)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    // Line 1: WiFi SSID
    display.print("WiFi: ");
    display.println(WIFI_SSID);

    // Line 2: MAC address
    display.print("MAC: ");
    display.println(scannerMacAddress);

    // Empty line
    display.println();

    // Status lines
    if (status1)
    {
        display.println(status1);
    }
    if (status2)
    {
        display.println(status2);
    }
    if (status3)
    {
        display.println(status3);
    }

    display.display();
}

/**
 * Update OLED with scan statistics
 */
void oledUpdateScanStats(uint32_t devices, uint32_t scooters, uint32_t published, uint32_t dropped)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    // Line 1: WiFi SSID
    display.print("WiFi: ");
    display.println(WIFI_SSID);

    // Line 2: MAC address (last 6 chars only to save space)
    display.print("MAC: ");
    display.println(&scannerMacAddress[9]); // Show last 8 chars (DD:EE:FF)

    // Empty line
    display.println();

    // Statistics
    display.print("Devices: ");
    display.println(devices);

    display.print("Scooters: ");
    display.println(scooters);

    display.print("Published: ");
    display.println(published);

    display.display();
}
#endif

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
    BaseType_t result = xTaskCreatePinnedToCore(
        mqttTask,   // Task function
        "mqttTask", // Task name
        8192,       // Stack size
        nullptr,    // Parameters
        1,          // Priority
        nullptr,    // Task handle
        0           // Core 0
    );

    if (result != pdPASS)
    {
        Serial.println("ERROR: Failed to create MQTT task!");
        while (true)
        {
            delay(1000);
        }
    }
    Serial.println("MQTT task started on Core 0");

    // Initialize NimBLE
    NimBLEDevice::init("");

    // Configure scanner
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCallbacks);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    Serial.println("BLE initialized. Starting scans...\n");
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
