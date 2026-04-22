#ifndef SENSORS_H
#define SENSORS_H

#include <Wire.h>
#include <math.h>
#include <stdlib.h>
#include "MAX30105.h"
#include "SparkFun_BMA400_Arduino_Library.h"

// Pin definitions
#define BATTERY_PIN A0
#define IMU_INTERRUPT_PIN D6

// Heart rate thresholds
#define IR_FINGER_THRESHOLD 50000
#define MIN_BPM 40
#define MAX_BPM 180

// Sampling and filter constants
#define SAMPLE_INTERVAL_MS 2  // 400 Hz sample rate
#define FILTER_FS 400         // Must match 1000/SAMPLE_INTERVAL_MS
#define PEAK_MIN_DISTANCE 160 // 0.4s at 400 Hz (max ~150 BPM)
#define PEAK_MAX_COUNT 300    // Max peaks in one measurement window

// Result struct for heart rate + HRV measurement
struct HRVResult
{
   uint8_t bpm;      // Mean heart rate (0 = no reading)
   uint16_t sdnn_ms; // SDNN in milliseconds (std dev of RR intervals)
   bool valid;       // True when finger was detected and enough peaks found
};

// Butterworth bandpass 0.5-5 Hz, order 4, fs=400 Hz  (SOS form, 4 sections × 6 coeffs)
// Generated with: scipy.signal.butter(4, [0.5, 5.0], btype='band', fs=400, output='sos')
static const float BANDPASS_SOS[4][6] = {
    {1.4249966321e-06f, 2.8499932642e-06f, 1.4249966321e-06f, 1.0f, -1.8881015656f, 0.8921266839f},
    {1.0f, 2.0f, 1.0f, 1.0f, -1.9464478276f, 0.9522195618f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.9835071373f, 0.9835958332f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.9948397875f, 0.9949039447f}};

// Global sensor objects
MAX30105 particleSensor;
BMA400 accelerometer;

// Interrupt event counter for IMU (incremented in ISR)
volatile uint32_t tapInterruptCount = 0;

void IRAM_ATTR imuInterruptHandler()
{
   tapInterruptCount++;
}

// Initialize heart rate sensor
bool initHeartRateSensor()
{
   Serial.println("Initializing MAX30102...");

   if (!particleSensor.begin(Wire, I2C_SPEED_FAST))
   {
      Serial.println("ERROR: MAX30102 not found!");
      return false;
   }

   // Setup to sense a nice looking saw tooth on the plotter
   byte ledBrightness = 0x1F; // Options: 0=Off to 255=50mA
   byte sampleAverage = 1;    // Options: 1, 2, 4, 8, 16, 32
   byte ledMode = 2;          // Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
   int sampleRate = 400;      // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
   int pulseWidth = 411;      // Options: 69, 118, 215, 411
   int adcRange = 4096;       // Options: 2048, 4096, 8192, 16384

   particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); // Configure sensor with these settings

   Serial.println("MAX30102 initialized");
   return true;
}

// Initialize IMU with double-tap detection
bool initIMU()
{
   Serial.println("Initializing BMA400...");

   Wire.begin();

   if (accelerometer.beginI2C() != BMA400_OK)
   {
      Serial.println("ERROR: BMA400 not found!");
      return false;
   }

   // Configure for tap detection (based on working_code/imu_i2c_interrupt.cpp)
   accelerometer.setODR(BMA400_ODR_200HZ);   // 200 Hz required for tap detection
   accelerometer.setRange(BMA400_RANGE_16G); // High range for tap spikes

   // Configure tap detection
   bma400_tap_conf tapConfig;
   tapConfig.axes_sel = BMA400_TAP_Z_AXIS_EN;           // Z-axis only
   tapConfig.sensitivity = BMA400_TAP_SENSITIVITY_0;    // Most sensitive (0-7)
   tapConfig.tics_th = BMA400_TICS_TH_18_DATA_SAMPLES;  // Max time between peaks
   tapConfig.quiet = BMA400_QUIET_60_DATA_SAMPLES;      // Min time between taps
   tapConfig.quiet_dt = BMA400_QUIET_DT_4_DATA_SAMPLES; // Min time for double-tap
   tapConfig.int_chan = BMA400_INT_CHANNEL_1;

   if (accelerometer.setTapInterrupt(&tapConfig) != BMA400_OK)
   {
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

// ---------------------------------------------------------------------------
// DSP helpers (translated from hrv_analysis.ipynb)
// ---------------------------------------------------------------------------

// Single forward SOS filter pass (Direct-Form II Transposed)
static void applySOSFilterPass(const float *x, float *y, int n)
{
   for (int i = 0; i < n; i++)
      y[i] = x[i];

   for (int s = 0; s < 4; s++)
   {
      float b0 = BANDPASS_SOS[s][0], b1 = BANDPASS_SOS[s][1], b2 = BANDPASS_SOS[s][2];
      float a1 = BANDPASS_SOS[s][4], a2 = BANDPASS_SOS[s][5];
      float w1 = 0.0f, w2 = 0.0f;

      for (int i = 0; i < n; i++)
      {
         float w0 = y[i] - a1 * w1 - a2 * w2;
         y[i] = b0 * w0 + b1 * w1 + b2 * w2;
         w2 = w1;
         w1 = w0;
      }
   }
}

// In-place filtfilt (forward-backward filtering, matches scipy.signal.filtfilt)
static void applyBandpassFiltfilt(float *data, int n)
{
   float *tmp = (float *)malloc(n * sizeof(float));
   if (!tmp)
   {
      Serial.println("ERROR: filtfilt malloc failed");
      return;
   }

   // Forward pass
   applySOSFilterPass(data, tmp, n);

   // Reverse
   for (int i = 0; i < n / 2; i++)
   {
      float t = tmp[i];
      tmp[i] = tmp[n - 1 - i];
      tmp[n - 1 - i] = t;
   }

   // Second forward pass (equivalent to backward pass on original)
   applySOSFilterPass(tmp, data, n);

   // Reverse back to original order
   for (int i = 0; i < n / 2; i++)
   {
      float t = data[i];
      data[i] = data[n - 1 - i];
      data[n - 1 - i] = t;
   }

   free(tmp);
}

// Peak detection with minimum distance and prominence threshold
// Returns count of detected peaks; indices stored in peakIndices[]
static int detectPeaks(const float *data, int n, int minDist, float threshold,
                       int *peakIndices, int maxPeaks)
{
   int count = 0;

   // Step 1: find all local maxima above threshold
   for (int i = 1; i < n - 1 && count < maxPeaks; i++)
   {
      if (data[i] > data[i - 1] && data[i] >= data[i + 1] && data[i] > threshold)
      {
         peakIndices[count++] = i;
      }
   }

   // Step 2: enforce minimum distance – keep the taller peak when two are too close
   for (int i = 1; i < count;)
   {
      if (peakIndices[i] - peakIndices[i - 1] < minDist)
      {
         // Remove the smaller peak
         int removeIdx = (data[peakIndices[i]] >= data[peakIndices[i - 1]]) ? i - 1 : i;
         for (int j = removeIdx; j < count - 1; j++)
         {
            peakIndices[j] = peakIndices[j + 1];
         }
         count--;
         if (removeIdx < i)
            i = removeIdx + 1; // re-check from same position
      }
      else
      {
         i++;
      }
   }

   return count;
}

// ---------------------------------------------------------------------------
// Measure heart rate + HRV over specified duration
// Collects raw IR at 100 Hz, then applies bandpass filter + peak detection
// ---------------------------------------------------------------------------
HRVResult measureHeartRate(uint32_t durationMs)
{
   Serial.printf("Measuring heart rate for %d seconds (raw IR capture)...\n", durationMs / 1000);

   HRVResult result = {0, 0, false};
   // Buffer capacity: worst-case 100 Hz for the full duration
   int bufCapacity = durationMs / SAMPLE_INTERVAL_MS;

   // Heap-allocate buffers (~24 KB each for 60 s at 100 Hz)
   int32_t *rawIR = (int32_t *)malloc(bufCapacity * sizeof(int32_t));
   float *signal = (float *)malloc(bufCapacity * sizeof(float));
   if (!rawIR || !signal)
   {
      Serial.println("ERROR: Failed to allocate IR buffers");
      free(rawIR);
      free(signal);
      return result;
   }

   // --- Phase 1: Collect raw IR data for exactly durationMs (time-based) ---
   uint32_t startTime = millis();
   uint32_t lastPrint = startTime;
   bool fingerDetected = false;
   int collected = 0;

   while ((millis() - startTime) < durationMs && collected < bufCapacity)
   {
      long irValue = particleSensor.getIR();

      if (irValue > IR_FINGER_THRESHOLD)
         fingerDetected = true;

      rawIR[collected++] = (int32_t)irValue;

      if (millis() - lastPrint >= 1000)
      {
         Serial.printf("  [%ds] IR=%ld, samples=%d (cap:%d) %s\n",
                       (int)((millis() - startTime) / 1000), irValue, collected, bufCapacity,
                       irValue < IR_FINGER_THRESHOLD ? "(no finger)" : "");
         lastPrint = millis();
      }

      // Yield to FreeRTOS scheduler; sensor I2C read (~10ms) provides natural pacing
      delay(1);
   }

   uint32_t totalCollectionMs = millis() - startTime;
   float actualIntervalMs = (float)totalCollectionMs / collected;

   if (!fingerDetected)
   {
      Serial.println("No finger detected during measurement");
      free(rawIR);
      free(signal);
      return result;
   }

   Serial.printf("Collection complete: %d samples in %d ms (%.1f Hz)\n",
                 collected, (int)totalCollectionMs, 1000.0f / actualIntervalMs);

   // --- Phase 2: Bandpass filter (0.5–5 Hz) ---
   // Remove DC offset first – raw IR values are ~100k; the large DC causes
   // enormous filter transients that drown out the actual heartbeat AC component.
   float dcOffset = 0;
   for (int i = 0; i < collected; i++)
      dcOffset += (float)rawIR[i];
   dcOffset /= collected;
   for (int i = 0; i < collected; i++)
      signal[i] = (float)rawIR[i] - dcOffset;
   free(rawIR); // No longer needed

   Serial.printf("DC offset removed: %.0f\n", dcOffset);
   Serial.println("Applying bandpass filter (0.5-5 Hz)...");
   applyBandpassFiltfilt(signal, collected);

   // --- Phase 3: Peak detection ---
   // Skip first/last 2 seconds to avoid filter edge transients
   int trimSamples = (int)(2000.0f / actualIntervalMs);
   int startIdx = trimSamples;
   int endIdx = collected - trimSamples;
   if (endIdx <= startIdx)
   {
      startIdx = 0;
      endIdx = collected;
   }
   int regionLen = endIdx - startIdx;

   // Compute std dev on trimmed (transient-free) region
   float mean = 0;
   for (int i = startIdx; i < endIdx; i++)
      mean += signal[i];
   mean /= regionLen;

   float variance = 0;
   for (int i = startIdx; i < endIdx; i++)
   {
      float diff = signal[i] - mean;
      variance += diff * diff;
   }
   float stddev = sqrtf(variance / regionLen);
   float prominenceThreshold = 0.3f * stddev;

   Serial.printf("Filtered region [%d..%d]: mean=%.2f, stddev=%.2f, threshold=%.2f\n",
                 startIdx, endIdx, mean, stddev, prominenceThreshold);

   int *peakIndices = (int *)malloc(PEAK_MAX_COUNT * sizeof(int));
   if (!peakIndices)
   {
      Serial.println("ERROR: Failed to allocate peak buffer");
      free(signal);
      return result;
   }

   // Compute min peak distance from actual sample rate (0.4s minimum between beats)
   int actualMinDist = (int)(400.0f / actualIntervalMs); // 0.4s / intervalMs
   int peakCount = detectPeaks(signal + startIdx, regionLen, actualMinDist,
                               prominenceThreshold, peakIndices, PEAK_MAX_COUNT);
   free(signal);

   Serial.printf("Detected %d peaks (threshold=%.2f)\n", peakCount, prominenceThreshold);

   if (peakCount < 2)
   {
      Serial.println("Not enough peaks for HR/HRV calculation");
      free(peakIndices);
      return result;
   }

   // --- Phase 4: Compute RR intervals, HR, and SDNN ---
   int rrCount = peakCount - 1;
   float rrSum = 0;

   // First pass: compute mean RR (using actual measured sample interval)
   for (int i = 0; i < rrCount; i++)
   {
      float rr = (float)(peakIndices[i + 1] - peakIndices[i]) * actualIntervalMs;
      rrSum += rr;
   }
   float meanRR = rrSum / rrCount;

   // Second pass: compute SDNN (std dev of RR intervals)
   float rrVariance = 0;
   for (int i = 0; i < rrCount; i++)
   {
      float rr = (float)(peakIndices[i + 1] - peakIndices[i]) * actualIntervalMs;
      float diff = rr - meanRR;
      rrVariance += diff * diff;
   }
   float sdnn = sqrtf(rrVariance / rrCount);

   free(peakIndices);

   uint8_t bpm = (uint8_t)(60000.0f / meanRR);
   if (bpm < MIN_BPM || bpm > MAX_BPM)
   {
      Serial.printf("Computed BPM %d out of range [%d-%d]\n", bpm, MIN_BPM, MAX_BPM);
      return result;
   }

   result.bpm = bpm;
   result.sdnn_ms = (uint16_t)sdnn;
   result.valid = true;

   Serial.printf("Heart rate: %d BPM, SDNN: %d ms (%d RR intervals)\n",
                 result.bpm, result.sdnn_ms, rrCount);
   return result;
}

// Read battery voltage with averaging
float readBatteryVoltage()
{
   pinMode(BATTERY_PIN, INPUT);

   uint32_t voltage = 0;
   const int samples = 16;

   for (int i = 0; i < samples; i++)
   {
      voltage += analogReadMilliVolts(BATTERY_PIN);
      delay(10);
   }

   // Average and apply voltage divider correction (2x)
   float volts = 2.0 * voltage / samples / 1000.0;

   Serial.printf("Battery voltage: %.2fV\n", volts);
   return volts;
}

// Atomically drain pending tap interrupt events accumulated by the ISR.
uint32_t consumeTapInterrupts()
{
   noInterrupts();
   uint32_t count = tapInterruptCount;
   tapInterruptCount = 0;
   interrupts();
   return count;
}

// Shutdown sensors for low power deep sleep
void shutdownSensors()
{
   // MAX30102 can be put in low power mode
   particleSensor.shutDown();

   // Deep-sleep wake is timer-only, so IMU interrupt line is not used during sleep.
   detachInterrupt(digitalPinToInterrupt(IMU_INTERRUPT_PIN));
   accelerometer.enableInterrupt(BMA400_DOUBLE_TAP_INT_EN, false);

   Serial.println("Sensors prepared for deep sleep");
}

#endif // SENSORS_H
