/**
 * OLED Display support for ESP32 BLE Scooter Counter
 * - Display initialization
 * - Status and statistics display updates
 * - All code wrapped in HAS_OLED conditionals
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

#if HAS_OLED

/**
 * Initialize OLED display
 */
void oledInit();

/**
 * Update OLED display with current status
 */
void oledUpdateStatus(const char* status1, const char* status2 = nullptr, const char* status3 = nullptr);

/**
 * Update OLED with scan statistics
 */
void oledUpdateScanStats(uint32_t devices, uint32_t scooters, uint32_t published, uint32_t dropped);

/**
 * Turn off OLED display
 */
void oledOff();

/**
 * Turn on OLED display
 */
void oledOn();

#endif // HAS_OLED

#endif // DISPLAY_H
