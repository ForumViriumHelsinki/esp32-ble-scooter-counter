/**
 * MQTT Handler for ESP32 BLE Scooter Counter
 * - WiFi connection and reconnection logic
 * - MQTT broker connection and maintenance
 * - NTP time synchronization
 * - MQTT task running on Core 0
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>

/**
 * Connect to WiFi
 * Returns true if connected, false otherwise
 */
bool connectWiFi();

/**
 * Connect to MQTT broker
 * Returns true if connected, false otherwise
 */
bool connectMQTT();

/**
 * Start MQTT task on Core 0
 * Handles WiFi/MQTT connection, NTP sync, and device queue consumption
 */
void startMqttTask();

#endif // MQTT_HANDLER_H
