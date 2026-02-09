/**
 * OLED Display implementation
 */

#include "display.h"
#include "config.h"

#if HAS_OLED

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED display settings
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr uint8_t OLED_ADDR = 0x3C;
constexpr int8_t OLED_RESET = -1; // Reset pin (-1 = share Arduino reset pin)

// External references to globals in main.cpp
extern char scannerMacAddress[18];

// Global OLED display object
static Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// Display auto-off timer settings
constexpr uint32_t DISPLAY_TIMEOUT_MS = 2000; // Turn off display after 2 seconds
static TimerHandle_t displayTimer = nullptr;

/**
 * Timer callback to turn off display
 * Called by FreeRTOS timer after timeout
 */
static void displayTimerCallback(TimerHandle_t xTimer)
{
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    Serial.println("OLED display turned off (auto-timeout)");
}

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

    // Create one-shot timer for auto-off functionality
    displayTimer = xTimerCreate(
        "DisplayTimer",                    // Timer name
        pdMS_TO_TICKS(DISPLAY_TIMEOUT_MS), // Timer period in ticks
        pdFALSE,                           // Auto-reload: false (one-shot timer)
        nullptr,                           // Timer ID (not used)
        displayTimerCallback               // Callback function
    );

    if (displayTimer == nullptr)
    {
        Serial.println("ERROR: Failed to create display timer!");
    }
    else
    {
        Serial.printf("Display auto-off timer created (%d ms timeout)\n", DISPLAY_TIMEOUT_MS);
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
    // Turn on display (in case it was off)
    display.ssd1306_command(SSD1306_DISPLAYON);

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

    // Start/restart auto-off timer
    if (displayTimer != nullptr)
    {
        xTimerReset(displayTimer, 0);
    }
}

/**
 * Update OLED with scan statistics
 */
void oledUpdateScanStats(uint32_t devices, uint32_t scooters, uint32_t published, uint32_t dropped)
{
    // Turn on display (in case it was off)
    display.ssd1306_command(SSD1306_DISPLAYON);

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

    // Start/restart auto-off timer
    if (displayTimer != nullptr)
    {
        xTimerReset(displayTimer, 0);
    }
}

/**
 * Turn off OLED display
 */
void oledOff()
{
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    Serial.println("OLED display turned off");
}

/**
 * Turn on OLED display
 */
void oledOn()
{
    display.ssd1306_command(SSD1306_DISPLAYON);
    Serial.println("OLED display turned on");
}

#endif // HAS_OLED
