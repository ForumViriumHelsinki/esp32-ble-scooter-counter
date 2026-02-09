/**
 * Utility functions implementation
 */

#include "utils.h"
#include <NimBLEDevice.h>
#include <time.h>

// External references to globals in main.cpp
extern char scannerMacAddress[18];

// DeviceData struct must be included from main.cpp
// This is defined in main.cpp and accessed here
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
    if (dev.timestamp[0] != '\0')
    {
        written += snprintf(buffer + written, bufferLen - written,
                            ",\"timestamp\":\"%s\"", dev.timestamp);
    }

    snprintf(buffer + written, bufferLen - written, "}");
}
