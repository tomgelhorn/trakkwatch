#ifndef SYSTEMSTATE_H
#define SYSTEMSTATE_H

#include <Arduino.h>
#include "esp_sleep.h"

// Screen modes
#define SCREEN_DASHBOARD     0
#define SCREEN_GRAPH         1
#define SCREEN_SLEEP_SUMMARY 2

// Inactivity timeout for interactive wake sessions (milliseconds)
#define INACTIVITY_TIMEOUT_MS 60000UL

// Wake reasons
#define WAKE_TIMER 0
#define WAKE_TAP 1
#define WAKE_BOOT 2

// RTC memory survives deep sleep
RTC_DATA_ATTR uint8_t currentScreen = SCREEN_DASHBOARD;
RTC_DATA_ATTR uint32_t bootCount = 0;

// Global state for current wake cycle
uint8_t wakeReason = WAKE_BOOT;

// Update wake reason based on ESP32 sleep wakeup cause
void updateWakeReason() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    
    bootCount++;
    
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            wakeReason = WAKE_TIMER;
            Serial.println("Wake: Timer (scheduled measurement)");
            break;
            
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_GPIO:
            wakeReason = WAKE_TAP;
            Serial.println("Wake: GPIO interrupt (double tap detected)");
            break;
            
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            wakeReason = WAKE_BOOT;
            Serial.printf("Wake: Power-on boot (count=%d)\n", bootCount);
            // Reset screen to dashboard on first boot
            if (bootCount == 1) {
                currentScreen = SCREEN_DASHBOARD;
            }
            break;
    }
}

// Returns true when the wake reason should start an interactive session
bool isInteractiveWake() {
    return wakeReason == WAKE_BOOT || wakeReason == WAKE_TAP;
}

// Cycle through screens: Dashboard -> Graph -> Sleep Summary -> Dashboard
void toggleScreen() {
    if (currentScreen == SCREEN_DASHBOARD) {
        currentScreen = SCREEN_GRAPH;
        Serial.println("Switched to GRAPH screen");
    } else if (currentScreen == SCREEN_GRAPH) {
        currentScreen = SCREEN_SLEEP_SUMMARY;
        Serial.println("Switched to SLEEP SUMMARY screen");
    } else {
        currentScreen = SCREEN_DASHBOARD;
        Serial.println("Switched to DASHBOARD screen");
    }
}

// Print current system state for debugging
void printSystemState() {
    Serial.println("\n=== System State ===");
    Serial.printf("Boot count: %d\n", bootCount);
    const char* screenNames[] = { "DASHBOARD", "GRAPH", "SLEEP_SUMMARY" };
    Serial.printf("Current screen: %s\n",
        currentScreen < 3 ? screenNames[currentScreen] : "UNKNOWN");
    Serial.printf("Wake reason: %s\n",
        wakeReason == WAKE_TIMER ? "TIMER" :
        wakeReason == WAKE_TAP  ? "TAP"  : "BOOT");
    Serial.println("==================\n");
}

#endif // SYSTEMSTATE_H
