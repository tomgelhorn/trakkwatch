/*
 * TrakkWatch - ESP32-C3 Smartwatch Firmware
 * 
 * Features:
 * - Heart rate monitoring with MAX30102 sensor
 * - Battery voltage monitoring
 * - E-paper display (200x200 GDEH0154D67)
 * - 4-hour heart rate history graph
 * - Double-tap gesture navigation (BMA400 IMU)
 * - Deep sleep power management
 * 
 * Screen navigation (cyclic via double-tap):
 *   Dashboard -> Graph -> Sleep Summary -> Dashboard
 * 
 * Wake patterns:
 * - Timer: Every 5 minutes for measurement cycle
 * - GPIO (first boot or double-tap): 60-second interactive session;
 *   each confirmed double tap cycles to the next screen;
 *   device returns to deep sleep after 60 s of no interaction.
 */

#include <Arduino.h>
#include "driver/gpio.h"
#include "DataStorage.h"
#include "SystemState.h"
#include "Sensors.h"
#include "DisplayManager.h"

// Measurement interval (5 minutes in microseconds)
#define SLEEP_INTERVAL_US (5ULL * 60ULL * 1000000ULL)

// Measurement duration (10 seconds)
#define MEASUREMENT_DURATION_MS 60000

// Global data storage
HRHistoryBuffer hrHistory;

void setup() {
    Serial.begin(115200);
    delay(500);  // Give serial time to initialize
    
    Serial.println("\n\n=================================");
    Serial.println("TrakkWatch Starting...");
    Serial.println("=================================\n");
    
    // Determine wake reason
    updateWakeReason();
    printSystemState();
    
    // Initialize data storage
    if (!hrHistory.begin()) {
        Serial.println("ERROR: Failed to initialize data storage!");
    }
    
    // Initialize sensors
    if (!initIMU()) {
        Serial.println("ERROR: Failed to initialize IMU!");
    }
    
    if (!initHeartRateSensor()) {
        Serial.println("ERROR: Failed to initialize heart rate sensor!");
    }
    
    // Initialize display
    if (!initDisplay()) {
        Serial.println("ERROR: Failed to initialize display!");
    }
    
    // Handle different wake reasons
    if (wakeReason == WAKE_BOOT || wakeReason == WAKE_TAP) {
        // -------------------------------------------------------
        // Interactive session (first boot OR double-tap wake)
        // Device stays awake up to INACTIVITY_TIMEOUT_MS (60 s).
        // Each confirmed double tap cycles to the next screen and
        // resets the inactivity timer.  When the timer expires the
        // device proceeds to deep sleep.
        // -------------------------------------------------------
        Serial.println("\n>>> Interactive session <<<");

        // On double-tap wake the wake itself counts as one navigation
        // input: advance the screen, then drain any residual IMU flag.
        if (wakeReason == WAKE_TAP) {
            toggleScreen();
            checkTapInterrupt();  // clear any residual interrupt state
        }

        // Measure HR once at session start
        uint8_t hr = measureHeartRate(MEASUREMENT_DURATION_MS);
        float voltage = readBatteryVoltage();

        if (hr > 0) {
            hrHistory.addMeasurement(hr);
            Serial.printf("Stored HR: %d BPM\n", hr);
        } else {
            Serial.println("No valid HR measurement");
        }
        Serial.printf("History: %d / %d measurements\n",
            hrHistory.getCount(), HR_HISTORY_SIZE);

        // Lambda: render whichever screen is currently active
        auto renderCurrentScreen = [&]() {
            if (currentScreen == SCREEN_GRAPH) {
                uint8_t buffer[HR_HISTORY_SIZE];
                uint8_t count = hrHistory.getMeasurements(buffer, HR_HISTORY_SIZE);
                renderGraph(buffer, count);
            } else if (currentScreen == SCREEN_SLEEP_SUMMARY) {
                renderSleepSummary();
            } else {
                renderDashboard(hr, voltage, !hrHistory.hasFullHistory());
            }
        };

        // Initial render
        renderCurrentScreen();

        // Interaction loop: block until 1 minute of no confirmed tap
        uint32_t lastInteraction = millis();
        while (millis() - lastInteraction < INACTIVITY_TIMEOUT_MS) {
            if (checkTapInterrupt()) {
                Serial.println("Double-tap confirmed: advancing screen");
                toggleScreen();
                renderCurrentScreen();
                lastInteraction = millis();
            }
            delay(50);
        }

        Serial.println("Inactivity timeout reached - proceeding to deep sleep");

    } else if (wakeReason == WAKE_TIMER) {
        // -------------------------------------------------------
        // Scheduled measurement cycle (timer wake only)
        // -------------------------------------------------------
        Serial.println("\n>>> Measurement cycle <<<");

        // Measure heart rate
        uint8_t hr = measureHeartRate(MEASUREMENT_DURATION_MS);

        // Read battery voltage
        float voltage = readBatteryVoltage();

        // Store measurement if valid
        if (hr > 0) {
            hrHistory.addMeasurement(hr);
            Serial.printf("Stored HR: %d BPM\n", hr);
        } else {
            Serial.println("No valid HR measurement - skipping storage");
        }

        // Render dashboard (always shown after a timer measurement)
        bool buildingHistory = !hrHistory.hasFullHistory();
        renderDashboard(hr, voltage, buildingHistory);

        Serial.printf("History: %d / %d measurements\n",
            hrHistory.getCount(), HR_HISTORY_SIZE);
    }
    
    // Prepare for deep sleep
    Serial.println("\n=== Entering Deep Sleep ===");
    shutdownSensors();
    hibernateDisplay();
    
    // Configure wake sources
    esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);
    
    // ESP32-C3 uses GPIO wakeup (not ext0/ext1)
    // Configure GPIO6 (D6) to wake on high level
    gpio_wakeup_enable((gpio_num_t)IMU_INTERRUPT_PIN, GPIO_INTR_HIGH_LEVEL);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << IMU_INTERRUPT_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
    
    Serial.printf("Sleep for %d minutes or until tap interrupt\n", 
        (int)(SLEEP_INTERVAL_US / 60000000ULL));
    Serial.println("===========================\n");
    Serial.flush();
    
    // Enter deep sleep
    esp_deep_sleep_start();
}

void loop() {
    // Should never reach here due to deep sleep
    // If we do, something went wrong
    Serial.println("ERROR: Should not be in loop()!");
    delay(1000);
}
