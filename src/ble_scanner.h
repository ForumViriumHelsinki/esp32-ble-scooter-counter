/**
 * BLE Scanner for ESP32 BLE Scooter Counter
 * - BLE scanning callback implementation
 * - Scooter name detection
 * - BLE initialization and configuration
 */

#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <NimBLEDevice.h>

/**
 * Check if device name starts with any known scooter prefix
 */
bool isScooterName(const std::string& name);

/**
 * BLE scan callback - queues device data for MQTT publishing
 */
class ScanCallbacks : public NimBLEScanCallbacks
{
    void onResult(const NimBLEAdvertisedDevice* device) override;
};

/**
 * Initialize BLE scanner with callbacks
 */
void bleInit(ScanCallbacks* callbacks);

#endif // BLE_SCANNER_H
