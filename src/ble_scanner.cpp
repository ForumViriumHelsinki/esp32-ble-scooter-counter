/**
 * BLE Scanner implementation
 */

#include "ble_scanner.h"
#include "utils.h"
#include <Arduino.h>

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

// External references to globals in main.cpp
extern QueueHandle_t deviceQueue;
extern char scannerMacAddress[18];
extern uint32_t totalDevicesFound;
extern uint32_t scootersFound;
extern uint32_t mqttDropped;

// DeviceData struct must match main.cpp definition
struct DeviceData
{
    char mac[18];
    char addrType[12];
    int rssi;
    int8_t txPower;
    bool hasTxPower;
    char name[32];
    char mfgId[5];
    char mfgData[64];
    char services[128];
    uint16_t appearance;
    bool hasAppearance;
    bool connectable;
    uint8_t advType;
    bool isScooter;
    uint32_t uptime;
    char timestamp[32];
};

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
 * BLE scan callback - queues device data for MQTT publishing
 */
void ScanCallbacks::onResult(const NimBLEAdvertisedDevice *device)
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
    devData.uptime = millis();

    // ISO time (UTC)
    getISO8601Time(devData.timestamp, sizeof(devData.timestamp));

    // Send to queue (non-blocking)
    if (xQueueSend(deviceQueue, &devData, 0) != pdTRUE)
    {
        // Queue full, drop this device
        mqttDropped++;
    }
}

/**
 * Initialize BLE scanner with callbacks
 */
void bleInit(ScanCallbacks *callbacks)
{
    // Initialize NimBLE
    NimBLEDevice::init("");

    // Configure scanner
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(callbacks);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    Serial.println("BLE initialized. Starting scans...\n");
}
