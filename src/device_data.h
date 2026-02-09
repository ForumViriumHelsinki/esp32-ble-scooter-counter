/**
 * Device data structure shared across modules
 */

#ifndef DEVICE_DATA_H
#define DEVICE_DATA_H

#include <Arduino.h>

/**
 * BLE device data structure for queue
 * Contains all advertisement data for a detected device
 */
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

#endif // DEVICE_DATA_H
