/**
 * Utility functions for ESP32 BLE Scooter Counter
 * - Time formatting (ISO8601)
 * - JSON message building
 * - Byte/string conversion utilities
 */

#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include "device_data.h"

/**
 * Get current time as ISO8601 UTC string
 * Returns false if time is not yet synchronized
 */
bool getISO8601Time(char* buffer, size_t bufferLen);

/**
 * Build JSON message from device data
 */
void buildJsonMessage(const DeviceData& dev, char* buffer, size_t bufferLen);

/**
 * Convert bytes to hex string
 */
void bytesToHex(const uint8_t* data, size_t len, char* output, size_t outputLen);

/**
 * Get address type string
 */
const char* getAddrTypeStr(uint8_t addrType);

/**
 * Escape JSON string (handles quotes and backslashes)
 */
void escapeJsonString(const char* input, char* output, size_t outputLen);

#endif // UTILS_H
