/**
 * MQTT Handler implementation
 */

#include "mqtt_handler.h"
#include "device_data.h"
#include "utils.h"
#include "config.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

#if HAS_OLED
#include "display.h"
#endif

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

// Debug settings
constexpr bool DEBUG_MQTT = false; // Set to true to print each published message

// External references to globals in main.cpp
extern QueueHandle_t deviceQueue;
extern WiFiClient wifiClient;
extern PubSubClient mqtt;
extern char mqttClientId[32];
extern char scannerMacAddress[18];
extern char mqttTopic[64];
extern uint32_t mqttPublished;
extern uint32_t mqttDropped;

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
static void mqttTask(void *param)
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
 * Start MQTT task on Core 0
 */
void startMqttTask()
{
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
}
