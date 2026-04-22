#ifndef SYSTEMSTATE_H
#define SYSTEMSTATE_H

#include <Arduino.h>
#include "esp_sleep.h"

// Screen modes — double-tap cycles forward through all views
#define SCREEN_DASHBOARD 0
#define SCREEN_HR_1H 1
#define SCREEN_HR_4H 2
#define SCREEN_HR_24H 3
#define SCREEN_HR_7D 4
#define SCREEN_HR_1MO 5
#define SCREEN_HRV_24H 6
#define SCREEN_HRV_7D 7
#define SCREEN_SLEEP_SUMMARY 8
#define SCREEN_COUNT 9

// Inactivity timeout for interactive wake sessions (milliseconds)
#define INACTIVITY_TIMEOUT_MS 60000UL

// Wake reasons
#define WAKE_TIMER 0
#define WAKE_BOOT 1

// RTC memory survives deep sleep
RTC_DATA_ATTR uint8_t currentScreen = SCREEN_DASHBOARD;
RTC_DATA_ATTR uint32_t bootCount = 0;

// Global state for current wake cycle
uint8_t wakeReason = WAKE_BOOT;

// Update wake reason based on ESP32 sleep wakeup cause
void updateWakeReason()
{
   esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

   bootCount++;

   switch (cause)
   {
   case ESP_SLEEP_WAKEUP_TIMER:
      wakeReason = WAKE_TIMER;
      Serial.println("Wake: Timer (scheduled measurement)");
      break;

   case ESP_SLEEP_WAKEUP_UNDEFINED:
   default:
      wakeReason = WAKE_BOOT;
      Serial.printf("Wake: Power-on boot (count=%d)\n", bootCount);
      // Reset screen to dashboard on first boot
      if (bootCount == 1)
      {
         currentScreen = SCREEN_DASHBOARD;
      }
      break;
   }
}

// Cycle through all screens on each double-tap
void toggleScreen()
{
   currentScreen = (currentScreen + 1) % SCREEN_COUNT;
   const char *screenNames[] = {
       "DASHBOARD", "HR_1H", "HR_4H", "HR_24H",
       "HR_7D", "HR_1MO", "HRV_24H", "HRV_7D", "SLEEP_SUMMARY"};
   Serial.printf("Switched to %s screen\n",
                 currentScreen < SCREEN_COUNT ? screenNames[currentScreen] : "UNKNOWN");
}

// Print current system state for debugging
void printSystemState()
{
   Serial.println("\n=== System State ===");
   Serial.printf("Boot count: %d\n", bootCount);
   const char *screenNames[] = {
       "DASHBOARD", "HR_1H", "HR_4H", "HR_24H",
       "HR_7D", "HR_1MO", "HRV_24H", "HRV_7D", "SLEEP_SUMMARY"};
   Serial.printf("Current screen: %s\n",
                 currentScreen < SCREEN_COUNT ? screenNames[currentScreen] : "UNKNOWN");
   Serial.printf("Wake reason: %s\n",
                 wakeReason == WAKE_TIMER ? "TIMER" : "BOOT");
   Serial.println("==================\n");
}

#endif // SYSTEMSTATE_H
