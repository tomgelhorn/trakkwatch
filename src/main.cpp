/*
 * TrakkWatch - ESP32-C3 Smartwatch Firmware
 *
 * Runtime model:
 * - Timer-only wake: device wakes on schedule
 * - Task A: heart-rate measurement (runs independently)
 * - Task B: IMU interrupt handling + UI rendering
 * - If interaction continues near session boundary, sleep is delayed
 *   until 60 seconds after the last confirmed double-tap.
 */

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "DataStorage.h"
#include "SystemState.h"
#include "Sensors.h"
#include "DisplayManager.h"

// Sleep interval (4 minutes) + active window (1 minute) = 5-minute cycle
#define SLEEP_INTERVAL_US (4ULL * 60ULL * 1000000ULL)

// Active measurement/session baseline duration (1 minute)
#define MEASUREMENT_DURATION_MS 60000UL
#define ACTIVE_WINDOW_MS MEASUREMENT_DURATION_MS

// Global data storage
TieredHRStorage hrHistory;

// Task handles
TaskHandle_t hrTaskHandle = nullptr;
TaskHandle_t uiTaskHandle = nullptr;

// Shared session state
SemaphoreHandle_t stateMutex = nullptr;
SemaphoreHandle_t historyMutex = nullptr;

bool sessionStopRequested = false;
bool measurementComplete = false;
bool renderRequested = false;
RTC_DATA_ATTR uint8_t latestHeartRate = 0;
float latestBatteryVoltage = 0.0f;
uint16_t latestSDNN = 0;
uint32_t lastTapTimestampMs = 0;

static bool lockState(TickType_t waitTicks = pdMS_TO_TICKS(50))
{
   if (stateMutex == nullptr)
   {
      return false;
   }
   return xSemaphoreTake(stateMutex, waitTicks) == pdTRUE;
}

static bool lockHistory(TickType_t waitTicks = pdMS_TO_TICKS(200))
{
   if (historyMutex == nullptr)
   {
      return false;
   }
   return xSemaphoreTake(historyMutex, waitTicks) == pdTRUE;
}

static void unlockState()
{
   xSemaphoreGive(stateMutex);
}

static void unlockHistory()
{
   xSemaphoreGive(historyMutex);
}

static void renderCurrentScreen(uint8_t hrValue, float batteryVoltage, bool isMeasuringActive)
{
   if (currentScreen == SCREEN_DASHBOARD)
   {
      setDashboardMeasuringActive(isMeasuringActive);
      renderDashboard(hrValue, batteryVoltage);
      return;
   }
   if (currentScreen == SCREEN_SLEEP_SUMMARY)
   {
      renderSleepSummary();
      return;
   }

   // All graph screens share one stack buffer (sized for the largest tier)
   uint8_t histBuf[TieredHRStorage::T3_SIZE];
   memset(histBuf, 0, sizeof(histBuf));
   uint16_t n = 0;

   if (lockHistory())
   {
      switch (currentScreen)
      {
      case SCREEN_HR_1H:
         n = hrHistory.getLastN(1, false, histBuf, 12);
         break;
      case SCREEN_HR_4H:
         n = hrHistory.getLastN(1, false, histBuf, 48);
         break;
      case SCREEN_HR_24H:
         n = hrHistory.getAll(1, false, histBuf);
         break;
      case SCREEN_HR_7D:
         n = hrHistory.getAll(2, false, histBuf);
         break;
      case SCREEN_HR_1MO:
         n = hrHistory.getAll(3, false, histBuf);
         break;
      case SCREEN_HRV_24H:
         n = hrHistory.getAll(1, true, histBuf);
         break;
      case SCREEN_HRV_7D:
         n = hrHistory.getAll(2, true, histBuf);
         break;
      default:
         break;
      }
      unlockHistory();
   }

   switch (currentScreen)
   {
   case SCREEN_HR_1H:
      renderGraph(histBuf, n, "1-Hour HR", "1h ago");
      break;
   case SCREEN_HR_4H:
      renderGraph(histBuf, n, "4-Hour HR", "4h ago");
      break;
   case SCREEN_HR_24H:
      renderGraph(histBuf, n, "24-Hour HR", "24h ago");
      break;
   case SCREEN_HR_7D:
      renderGraph(histBuf, n, "7-Day HR", "7d ago");
      break;
   case SCREEN_HR_1MO:
      renderGraph(histBuf, n, "30-Day HR", "30d ago");
      break;
   case SCREEN_HRV_24H:
      renderHRVGraph(histBuf, n, "24h HRV", "24h ago");
      break;
   case SCREEN_HRV_7D:
      renderHRVGraph(histBuf, n, "7-Day HRV", "7d ago");
      break;
   default:
      break;
   }
}

static void heartRateTask(void *parameter)
{
   (void)parameter;

   HRVResult result = measureHeartRate(MEASUREMENT_DURATION_MS);

   if (result.valid && result.bpm > 0)
   {
      if (lockHistory(pdMS_TO_TICKS(500)))
      {
         uint8_t clampedHRV = (result.sdnn_ms > 255) ? 255 : (uint8_t)result.sdnn_ms;
         hrHistory.addMeasurement(result.bpm, clampedHRV);
         unlockHistory();
         Serial.printf("Stored HR: %d BPM, SDNN: %d ms\n", result.bpm, result.sdnn_ms);
      }
      else
      {
         Serial.println("WARNING: Failed to lock history mutex, HR not stored");
      }
   }
   else
   {
      Serial.println("No valid HR measurement");
   }

   setDashboardSDNN(result.sdnn_ms);

   if (lockState())
   {
      latestHeartRate = result.bpm;
      latestSDNN = result.sdnn_ms;
      measurementComplete = true;
      renderRequested = true;
      unlockState();
   }

   while (true)
   {
      bool shouldStop = false;
      if (lockState(pdMS_TO_TICKS(20)))
      {
         shouldStop = sessionStopRequested;
         unlockState();
      }
      if (shouldStop)
      {
         break;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
   }

   hrTaskHandle = nullptr;
   vTaskDelete(nullptr);
}

static void uiTask(void *parameter)
{
   (void)parameter;

   uint8_t hrSnapshot = 0;
   float batterySnapshot = 0.0f;
   bool measuringSnapshot = true;
   bool previousMeasuringSnapshot = true;

   if (lockState())
   {
      renderRequested = false;
      hrSnapshot = latestHeartRate;
      batterySnapshot = latestBatteryVoltage;
      measuringSnapshot = !measurementComplete;
      previousMeasuringSnapshot = measuringSnapshot;
      unlockState();
   }
   renderCurrentScreen(hrSnapshot, batterySnapshot, measuringSnapshot);

   while (true)
   {
      bool shouldStop = false;
      bool shouldRender = false;

      if (lockState(pdMS_TO_TICKS(20)))
      {
         shouldStop = sessionStopRequested;
         shouldRender = renderRequested;
         renderRequested = false;
         hrSnapshot = latestHeartRate;
         batterySnapshot = latestBatteryVoltage;
         measuringSnapshot = !measurementComplete;
         unlockState();
      }

      if (shouldStop)
      {
         break;
      }

      uint32_t tapEvents = consumeTapInterrupts();
      if (tapEvents > 0)
      {
         for (uint32_t i = 0; i < tapEvents; i++)
         {
            uint32_t now = millis();
            toggleScreen();

            if (lockState())
            {
               lastTapTimestampMs = now;
               hrSnapshot = latestHeartRate;
               batterySnapshot = latestBatteryVoltage;
               unlockState();
            }

            shouldRender = true;
            Serial.println("Double-tap confirmed: advancing screen");
         }
      }

      if (shouldRender)
      {
         bool measurementJustCompleted = previousMeasuringSnapshot && !measuringSnapshot;
         if (measurementJustCompleted && currentScreen == SCREEN_DASHBOARD && hrSnapshot > 0)
         {
            setDashboardMeasuringActive(false);
            setDashboardSDNN(latestSDNN);
            renderCurrentScreen(hrSnapshot, batterySnapshot, measuringSnapshot);
         }
         else
         {
            renderCurrentScreen(hrSnapshot, batterySnapshot, measuringSnapshot);
         }
      }

      previousMeasuringSnapshot = measuringSnapshot;

      vTaskDelay(pdMS_TO_TICKS(30));
   }

   uiTaskHandle = nullptr;
   vTaskDelete(nullptr);
}

void setup()
{
   Serial.begin(115200);
   delay(500); // Give serial time to initialize

   Serial.println("\n\n=================================");
   Serial.println("TrakkWatch Starting...");
   Serial.println("=================================\n");

   // Determine wake reason
   updateWakeReason();
   printSystemState();

   // Initialize data storage
   if (!hrHistory.begin())
   {
      Serial.println("ERROR: Failed to initialize data storage!");
   }

   // Initialize sensors
   if (!initIMU())
   {
      Serial.println("ERROR: Failed to initialize IMU!");
   }

   if (!initHeartRateSensor())
   {
      Serial.println("ERROR: Failed to initialize heart rate sensor!");
   }

   // Initialize display
   if (!initDisplay())
   {
      Serial.println("ERROR: Failed to initialize display!");
   }

   // Wake state indicator: partial update only in the badge area.
   updatePowerStatusBadge(true);

   // Drain any stale interrupt edges before starting this active session.
   consumeTapInterrupts();

   // Create synchronization primitives
   stateMutex = xSemaphoreCreateMutex();
   historyMutex = xSemaphoreCreateMutex();
   bool mutexReady = (stateMutex != nullptr && historyMutex != nullptr);
   if (!mutexReady)
   {
      Serial.println("ERROR: Failed to create mutexes");
   }

   // Session state initialization
   latestBatteryVoltage = readBatteryVoltage();
   measurementComplete = false;
   renderRequested = true;
   lastTapTimestampMs = 0;
   sessionStopRequested = false;

   Serial.println("\n>>> Active session (HR + UI concurrency) <<<");

   BaseType_t uiCreated = pdFAIL;
   BaseType_t hrCreated = pdFAIL;
   if (mutexReady)
   {
      uiCreated = xTaskCreate(uiTask, "UI", 6144, nullptr, 1, &uiTaskHandle);
      hrCreated = xTaskCreate(heartRateTask, "HR", 8192, nullptr, 2, &hrTaskHandle);
   }

   if (!mutexReady || uiCreated != pdPASS || hrCreated != pdPASS)
   {
      Serial.println("ERROR: Failed to create tasks, falling back to synchronous mode");

      if (uiTaskHandle != nullptr)
      {
         vTaskDelete(uiTaskHandle);
         uiTaskHandle = nullptr;
      }
      if (hrTaskHandle != nullptr)
      {
         vTaskDelete(hrTaskHandle);
         hrTaskHandle = nullptr;
      }

      HRVResult result = measureHeartRate(MEASUREMENT_DURATION_MS);
      if (result.valid && result.bpm > 0 && lockHistory(pdMS_TO_TICKS(500)))
      {
         uint8_t clampedHRV = (result.sdnn_ms > 255) ? 255 : (uint8_t)result.sdnn_ms;
         hrHistory.addMeasurement(result.bpm, clampedHRV);
         unlockHistory();
      }
      latestHeartRate = result.bpm;
      latestSDNN = result.sdnn_ms;
      setDashboardSDNN(result.sdnn_ms);
      renderCurrentScreen(result.bpm, latestBatteryVoltage, false);
   }
   else
   {
      const uint32_t sessionStartMs = millis();
      const uint32_t baselineEndMs = sessionStartMs + ACTIVE_WINDOW_MS;

      // Keep device active for baseline window + optional inactivity extension.
      while (true)
      {
         bool hrDone = false;
         uint32_t lastTapMs = 0;
         if (lockState(pdMS_TO_TICKS(20)))
         {
            hrDone = measurementComplete;
            lastTapMs = lastTapTimestampMs;
            unlockState();
         }

         uint32_t now = millis();
         bool baselineElapsed = (int32_t)(now - baselineEndMs) >= 0;

         if (!baselineElapsed || !hrDone)
         {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
         }

         if (lastTapMs == 0 || (now - lastTapMs) >= INACTIVITY_TIMEOUT_MS)
         {
            Serial.println("Session complete - proceeding to deep sleep");
            break;
         }

         vTaskDelay(pdMS_TO_TICKS(50));
      }

      if (lockState())
      {
         sessionStopRequested = true;
         unlockState();
      }

      // Give tasks a chance to exit gracefully.
      vTaskDelay(pdMS_TO_TICKS(200));

      if (uiTaskHandle != nullptr)
      {
         vTaskDelete(uiTaskHandle);
         uiTaskHandle = nullptr;
      }
      if (hrTaskHandle != nullptr)
      {
         vTaskDelete(hrTaskHandle);
         hrTaskHandle = nullptr;
      }
   }

   if (lockHistory())
   {
      Serial.printf("History: T1=%d/288 entries\n", hrHistory.getCount());
      unlockHistory();
   }

   // Prepare for deep sleep
   Serial.println("\n=== Entering Deep Sleep ===");
   setDashboardMeasuringActive(false);
   setDashboardSDNN(latestSDNN);
   renderDashboard(latestHeartRate, latestBatteryVoltage); // Final screen update before sleep
   // Sleep state indicator: partial update only in the badge area.
   updatePowerStatusBadge(false);

   shutdownSensors();
   hibernateDisplay();

   // Configure wake sources
   esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);

   Serial.printf("Sleep for %d minutes (timer wake only)\n",
                 (int)(SLEEP_INTERVAL_US / 60000000ULL));
   Serial.println("===========================\n");
   Serial.flush();

   // Enter deep sleep
   esp_deep_sleep_start();
}

void loop()
{
   // Should never reach here due to deep sleep
   // If we do, something went wrong
   Serial.println("ERROR: Should not be in loop()!");
   delay(1000);
}
