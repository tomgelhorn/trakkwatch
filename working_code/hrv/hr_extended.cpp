/*
  HRV Real-time Analysis for ESP32
  Collects 10 seconds of MAX30102 data, applies Butterworth bandpass filtering,
  detects peaks, calculates R-R intervals, and outputs heart rate metrics.
  
  Minimal libraries: Wire.h, MAX30105.h only
*/

#include <Wire.h>
#include "MAX30105.h"

MAX30105 Sensor;

// Configuration
const int SAMPLE_RATE = 400;           // Hz (must match sensor config)
const int COLLECTION_TIME = 10;        // seconds
const int BUFFER_SIZE = SAMPLE_RATE * COLLECTION_TIME;  // 4000 samples
const int MAX_PEAKS = 30;              // Max expected peaks in 10 seconds

// Data buffers
float irBuffer[BUFFER_SIZE];
float filteredBuffer[BUFFER_SIZE];
int peakIndices[MAX_PEAKS];
int rrIntervals[MAX_PEAKS - 1];

// Biquad filter structure for bandpass (0.5-5 Hz)
struct BiquadFilter {
  float b0, b1, b2, a1, a2;  // Filter coefficients
  float x1, x2, y1, y2;      // State variables
};

// Two biquad sections for 4th order Butterworth bandpass (0.5-5 Hz @ 400 Hz)
// Precomputed coefficients for efficiency
BiquadFilter bpf1 = {0.00048424, 0, -0.00048424, -1.98223495, 0.98309374, 0, 0, 0, 0};
BiquadFilter bpf2 = {0.00048424, 0, -0.00048424, -1.99612856, 0.99710732, 0, 0, 0, 0};

// Apply single biquad filter
float applyBiquad(BiquadFilter &f, float input) {
  float output = f.b0 * input + f.b1 * f.x1 + f.b2 * f.x2
                 - f.a1 * f.y1 - f.a2 * f.y2;
  
  f.x2 = f.x1;
  f.x1 = input;
  f.y2 = f.y1;
  f.y1 = output;
  
  return output;
}

// Apply bandpass filter (cascade of two biquads)
void applyBandpassFilter(float* input, float* output, int length) {
  // Reset filter states
  bpf1.x1 = bpf1.x2 = bpf1.y1 = bpf1.y2 = 0;
  bpf2.x1 = bpf2.x2 = bpf2.y1 = bpf2.y2 = 0;
  
  // Apply cascaded biquad filters
  for (int i = 0; i < length; i++) {
    float temp = applyBiquad(bpf1, input[i]);
    output[i] = applyBiquad(bpf2, temp);
  }
}

// Simple peak detection with minimum distance constraint
int findPeaks(float* data, int length, int* peaks, int maxPeaks) {
  int peakCount = 0;
  const int MIN_DISTANCE = SAMPLE_RATE * 0.4;  // 0.4s minimum (max 150 BPM)
  
  // Calculate adaptive threshold (mean + 0.3 * std)
  float mean = 0, stdDev = 0;
  for (int i = 0; i < length; i++) {
    mean += data[i];
  }
  mean /= length;
  
  for (int i = 0; i < length; i++) {
    float diff = data[i] - mean;
    stdDev += diff * diff;
  }
  stdDev = sqrt(stdDev / length);
  float threshold = stdDev * 0.3;
  
  // Find peaks
  int lastPeakIdx = -MIN_DISTANCE;
  
  for (int i = 1; i < length - 1; i++) {
    // Check if it's a local maximum above threshold
    if (data[i] > data[i-1] && data[i] > data[i+1] && data[i] > threshold) {
      // Check minimum distance from last peak
      if (i - lastPeakIdx >= MIN_DISTANCE) {
        if (peakCount < maxPeaks) {
          peaks[peakCount++] = i;
          lastPeakIdx = i;
        } else {
          break;  // Buffer full
        }
      }
    }
  }
  
  return peakCount;
}

// Calculate R-R intervals from peak indices
int calculateRRIntervals(int* peaks, int peakCount, int* rrIntervals) {
  const float MS_PER_SAMPLE = 1000.0 / SAMPLE_RATE;  // 2.5 ms per sample @ 400 Hz
  
  for (int i = 0; i < peakCount - 1; i++) {
    rrIntervals[i] = (peaks[i+1] - peaks[i]) * MS_PER_SAMPLE;
  }
  
  return peakCount - 1;
}

// Calculate HRV metrics
void calculateHRVMetrics(int* rrIntervals, int count) {
  if (count < 2) {
    Serial.println("Not enough R-R intervals for analysis");
    return;
  }
  
  // Calculate mean R-R interval
  float meanRR = 0;
  for (int i = 0; i < count; i++) {
    meanRR += rrIntervals[i];
  }
  meanRR /= count;
  
  // Calculate SDNN (standard deviation of R-R intervals)
  float variance = 0;
  for (int i = 0; i < count; i++) {
    float diff = rrIntervals[i] - meanRR;
    variance += diff * diff;
  }
  float sdnn = sqrt(variance / count);
  
  // Calculate RMSSD (root mean square of successive differences)
  float sumSquaredDiff = 0;
  for (int i = 0; i < count - 1; i++) {
    float diff = rrIntervals[i+1] - rrIntervals[i];
    sumSquaredDiff += diff * diff;
  }
  float rmssd = sqrt(sumSquaredDiff / (count - 1));
  
  // Calculate heart rate from mean R-R
  float heartRate = 60000.0 / meanRR;
  
  // Find min/max R-R
  int minRR = rrIntervals[0];
  int maxRR = rrIntervals[0];
  for (int i = 1; i < count; i++) {
    if (rrIntervals[i] < minRR) minRR = rrIntervals[i];
    if (rrIntervals[i] > maxRR) maxRR = rrIntervals[i];
  }
  
  // Output results
  Serial.println("\n========== HRV ANALYSIS RESULTS ==========");
  Serial.print("R-R Intervals: "); Serial.println(count);
  Serial.print("Mean R-R:      "); Serial.print(meanRR, 1); Serial.println(" ms");
  Serial.print("Min R-R:       "); Serial.print(minRR); Serial.println(" ms");
  Serial.print("Max R-R:       "); Serial.print(maxRR); Serial.println(" ms");
  Serial.print("Range:         "); Serial.print(maxRR - minRR); Serial.println(" ms");
  Serial.println();
  Serial.print("Heart Rate:    "); Serial.print(heartRate, 1); Serial.println(" BPM");
  Serial.print("SDNN:          "); Serial.print(sdnn, 1); Serial.println(" ms");
  Serial.print("RMSSD:         "); Serial.print(rmssd, 1); Serial.println(" ms");
  Serial.println("==========================================\n");
}

void setup() {
  Serial.begin(115200);
  Serial.println("HRV Analysis - Initializing...");
  
  // Initialize sensor
  if (!Sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found. Check wiring/power.");
    while (1);
  }
  
  // Configure sensor for HRV analysis
  byte ledBrightness = 0x1F;  // 31
  byte sampleAverage = 1;     // No averaging
  byte ledMode = 2;           // Red + IR
  int sampleRate = 400;       // 400 Hz
  int pulseWidth = 411;       // 411 µs
  int adcRange = 4096;        // 4096
  
  Sensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  
  Serial.println("Ready! Place finger on sensor.");
  Serial.println("Will collect data for 10 seconds, then analyze.\n");
  delay(2000);
}

void loop() {
  Serial.println("=== Starting 10-second data collection ===");
  unsigned long startTime = millis();
  
  // Collect IR data for 10 seconds
  for (int i = 0; i < BUFFER_SIZE; i++) {
    irBuffer[i] = Sensor.getIR();
    Serial.println(irBuffer[i]);
    // Maintain sample rate timing (2.5ms per sample @ 400Hz)
    while (millis() - startTime < (i + 1) * 2.5) {
      delayMicroseconds(100);
    }
    
    // Progress indicator
    if (i % SAMPLE_RATE == 0) {
      //Serial.print(".");
    }
  }
  Serial.println(" Done!");
  
  Serial.print("Collection time: ");
  Serial.print((millis() - startTime) / 1000.0, 2);
  Serial.println(" seconds");
  
  // Apply bandpass filter
  Serial.println("Applying bandpass filter (0.5-5 Hz)...");
  applyBandpassFilter(irBuffer, filteredBuffer, BUFFER_SIZE);
  
  // Detect peaks
  Serial.println("Detecting peaks...");
  int peakCount = findPeaks(filteredBuffer, BUFFER_SIZE, peakIndices, MAX_PEAKS);
  Serial.print("Peaks found: "); Serial.println(peakCount);
  
  if (peakCount < 5) {
    Serial.println("ERROR: Too few peaks detected. Check finger placement.");
    delay(5000);
    return;
  }
  
  // Calculate R-R intervals
  Serial.println("Calculating R-R intervals...");
  int rrCount = calculateRRIntervals(peakIndices, peakCount, rrIntervals);
  
  // Calculate and display HRV metrics
  calculateHRVMetrics(rrIntervals, rrCount);
  
  // Wait before next measurement
  Serial.println("Waiting 5 seconds before next measurement...\n");
  delay(5000);
}