#ifndef SENSORS_H
#define SENSORS_H

#include <Wire.h>
#include "MAX30105.h"
#include "SparkFun_BMA400_Arduino_Library.h"
#include "heartRate.h"

// Pin definitions
#define BATTERY_PIN A0
#define IMU_INTERRUPT_PIN D3

// Heart rate thresholds
#define IR_FINGER_THRESHOLD 50000
#define MIN_BPM 40
#define MAX_BPM 180

// Simple beat detection algorithm (based on SparkFun heartRate library)
static const byte RATE_SIZE = 4;
static byte rates[RATE_SIZE];
static byte rateSpot = 0;
static long lastBeat = 0;

// IR threshold for beat detection
static long lastIRValue = 0;
static bool beatDetected = false;

// Global sensor objects
MAX30105 particleSensor;
BMA400 accelerometer;

// Interrupt flag for IMU
volatile bool tapInterruptFlag = false;

void imuInterruptHandler() {
    tapInterruptFlag = true;
}

// Initialize heart rate sensor
bool initHeartRateSensor() {
    Serial.println("Initializing MAX30102...");
    
    if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
        Serial.println("ERROR: MAX30102 not found!");
        return false;
    }
    
    // Configure with low power settings
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);  // Low power red LED
    particleSensor.setPulseAmplitudeGreen(0);   // Green LED off
    
    Serial.println("MAX30102 initialized");
    return true;
}

// Initialize IMU with double-tap detection
bool initIMU() {
    Serial.println("Initializing BMA400...");
    
    Wire.begin();
    
    if (accelerometer.beginI2C() != BMA400_OK) {
        Serial.println("ERROR: BMA400 not found!");
        return false;
    }
    
    // Configure for tap detection (based on working_code/imu_i2c_interrupt.cpp)
    accelerometer.setODR(BMA400_ODR_200HZ);  // 200 Hz required for tap detection
    accelerometer.setRange(BMA400_RANGE_16G);  // High range for tap spikes
    
    // Configure tap detection
    bma400_tap_conf tapConfig;
    tapConfig.axes_sel = BMA400_TAP_Z_AXIS_EN;  // Z-axis only
    tapConfig.sensitivity = BMA400_TAP_SENSITIVITY_0;  // Most sensitive (0-7)
    tapConfig.tics_th = BMA400_TICS_TH_18_DATA_SAMPLES;     // Max time between peaks
    tapConfig.quiet = BMA400_QUIET_60_DATA_SAMPLES;       // Min time between taps
    tapConfig.quiet_dt = BMA400_QUIET_DT_4_DATA_SAMPLES;     // Min time for double-tap
    tapConfig.int_chan = BMA400_INT_CHANNEL_1;
    
    if (accelerometer.setTapInterrupt(&tapConfig) != BMA400_OK) {
        Serial.println("ERROR: Failed to configure tap interrupt");
        return false;
    }
    
    // Enable interrupts
    accelerometer.setInterruptPinMode(BMA400_INT_CHANNEL_1, BMA400_INT_PUSH_PULL_ACTIVE_1);
    accelerometer.enableInterrupt(BMA400_DOUBLE_TAP_INT_EN, true);
    
    // Setup GPIO interrupt
    pinMode(IMU_INTERRUPT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(IMU_INTERRUPT_PIN), imuInterruptHandler, RISING);
    
    Serial.println("BMA400 initialized with double-tap detection");
    return true;
}

// Measure heart rate over specified duration (in milliseconds)
// Returns averaged BPM or 0 if no finger detected
uint8_t measureHeartRate(uint32_t durationMs) {
    Serial.printf("Measuring heart rate for %d seconds...\n", durationMs / 1000);
    
    // Reset averaging arrays
    memset(rates, 0, RATE_SIZE);
    rateSpot = 0;
    lastBeat = 0;
    lastIRValue = 0;
    beatDetected = false;
    
    uint32_t startTime = millis();
    uint32_t lastPrint = startTime;
    float beatsPerMinute = 0;
    int beatAvg = 0;
    bool fingerDetected = false;
    
    while (millis() - startTime < durationMs) {
        long irValue = particleSensor.getIR();
        
        // Check if finger is present
        if (irValue > IR_FINGER_THRESHOLD) {
            fingerDetected = true;
            
            if (checkForBeat(irValue)) {
                long delta = millis() - lastBeat;
                lastBeat = millis();
                
                beatsPerMinute = 60.0 / (delta / 1000.0);
                
                // Store valid readings
                if (beatsPerMinute >= MIN_BPM && beatsPerMinute <= MAX_BPM) {
                    rates[rateSpot++] = (byte)beatsPerMinute;
                    rateSpot %= RATE_SIZE;
                    
                    // Calculate average
                    beatAvg = 0;
                    for (byte x = 0; x < RATE_SIZE; x++) {
                        beatAvg += rates[x];
                    }
                    beatAvg /= RATE_SIZE;
                }
            }
        }
        Serial.printf("  IR=%ld, BPM=%.1f, Avg=%d %s\n", 
            irValue, beatsPerMinute, beatAvg, 
            irValue < IR_FINGER_THRESHOLD ? "(no finger)" : "");
        // Print progress every second
        // if (millis() - lastPrint >= 1000) {
        //     Serial.printf("  IR=%ld, BPM=%.1f, Avg=%d %s\n", 
        //         irValue, beatsPerMinute, beatAvg, 
        //         irValue < IR_FINGER_THRESHOLD ? "(no finger)" : "");
        //     lastPrint = millis();
        // }
        
        delay(20);  // Sample at ~50 Hz
    }
    
    if (!fingerDetected) {
        Serial.println("No finger detected during measurement");
        return 0;
    }
    
    Serial.printf("Heart rate measurement complete: %d BPM\n", beatAvg);
    return (uint8_t)beatAvg;
}

// Read battery voltage with averaging
float readBatteryVoltage() {
    pinMode(BATTERY_PIN, INPUT);
    
    uint32_t voltage = 0;
    const int samples = 16;
    
    for (int i = 0; i < samples; i++) {
        voltage += analogReadMilliVolts(BATTERY_PIN);
        delay(10);
    }
    
    // Average and apply voltage divider correction (2x)
    float volts = 2.0 * voltage / samples / 1000.0;
    
    Serial.printf("Battery voltage: %.2fV\n", volts);
    return volts;
}

// Check if IMU tap interrupt occurred and clear it
bool checkTapInterrupt() {
    if (!tapInterruptFlag) {
        return false;
    }
    
    tapInterruptFlag = false;
    
    // Read and clear interrupt status
    uint16_t intStatus = 0;
    if (accelerometer.getInterruptStatus(&intStatus) == BMA400_OK) {
        if (intStatus & BMA400_ASSERTED_D_TAP_INT) {
            Serial.println("Double tap detected!");
            return true;
        }
    }
    
    return false;
}

// Shutdown sensors for low power deep sleep
void shutdownSensors() {
    // MAX30102 can be put in low power mode
    particleSensor.shutDown();
    
    // BMA400 stays active for tap detection during sleep
    // (it has very low power consumption in normal mode)
    
    Serial.println("Sensors prepared for deep sleep");
}

#endif // SENSORS_H
