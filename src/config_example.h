/**
 * Configuration file for WiFi and MQTT settings
 *
 * Copy this file to config.h and fill in your values.
 * config.h is in .gitignore and will not be committed.
 */

#ifndef CONFIG_H
#define CONFIG_H

// WiFi settings
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

// MQTT settings
#define MQTT_BROKER "your-mqtt-broker.example.com"
#define MQTT_PORT 1883
#define MQTT_USER ""      // Leave empty if no authentication
#define MQTT_PASSWORD ""  // Leave empty if no authentication

// MQTT topic for device data
#define MQTT_TOPIC "scooter-counter/devices"

// Optional: Client ID prefix (MAC address will be appended)
#define MQTT_CLIENT_PREFIX "scooter-counter-"

#endif // CONFIG_H
