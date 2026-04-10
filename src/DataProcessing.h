#ifndef DATAPROCESSING_H
#define DATAPROCESSING_H

#include <Arduino.h>

// Maximum number of peaks we can detect (adjust based on expected HR range)
#define MAX_PEAKS 50

// Structure to hold peak detection results
struct PeakProperties {
    int peakCount;
    int peaks[MAX_PEAKS];           // Indices of detected peaks
    float prominences[MAX_PEAKS];   // Prominence values for each peak
    int leftBases[MAX_PEAKS];       // Left base indices
    int rightBases[MAX_PEAKS];      // Right base indices
};

// ============================================================================
// Helper Functions (analogous to scipy internal functions)
// ============================================================================

// Find all local maxima in the signal
// Returns number of maxima found
int _local_maxima_1d(const float* x, int length, int* peaks, int maxPeaks) {
    int peakCount = 0;
    
    // A point is a local maximum if it's greater than both neighbors
    for (int i = 1; i < length - 1; i++) {
        if (x[i] > x[i-1] && x[i] > x[i+1]) {
            if (peakCount < maxPeaks) {
                peaks[peakCount++] = i;
            } else {
                break; // Buffer full
            }
        }
    }
    
    return peakCount;
}

// Select peaks by minimum distance criterion
// Keeps highest peaks when multiple peaks are within distance
void _select_by_peak_distance(int* peaks, const float* x, int* peakCount, int minDistance) {
    if (*peakCount <= 1 || minDistance < 1) {
        return; // Nothing to filter
    }
    
    // Priority: keep peaks with highest values
    // Use a simple algorithm: iterate and remove closer peaks with lower values
    bool keep[MAX_PEAKS];
    for (int i = 0; i < *peakCount; i++) {
        keep[i] = true;
    }
    
    for (int i = 0; i < *peakCount; i++) {
        if (!keep[i]) continue;
        
        // Check all subsequent peaks within distance
        for (int j = i + 1; j < *peakCount; j++) {
            if (!keep[j]) continue;
            
            int distance = peaks[j] - peaks[i];
            if (distance < minDistance) {
                // Remove the peak with lower value
                if (x[peaks[i]] > x[peaks[j]]) {
                    keep[j] = false;
                } else {
                    keep[i] = false;
                    break; // Current peak is removed, move to next
                }
            } else {
                break; // Beyond distance threshold
            }
        }
    }
    
    // Compact the array
    int newCount = 0;
    for (int i = 0; i < *peakCount; i++) {
        if (keep[i]) {
            peaks[newCount++] = peaks[i];
        }
    }
    *peakCount = newCount;
}

// Calculate peak prominences
// Prominence = peak height - max(left_min, right_min)
void _peak_prominences(const float* x, int* peaks, int peakCount, 
                       float* prominences, int* leftBases, int* rightBases) {
    
    for (int i = 0; i < peakCount; i++) {
        int peakIdx = peaks[i];
        float peakValue = x[peakIdx];
        
        // Find minimum to the left
        float leftMin = peakValue;
        int leftBase = 0;
        for (int j = peakIdx - 1; j >= 0; j--) {
            if (x[j] < leftMin) {
                leftMin = x[j];
                leftBase = j;
            }
            // Stop if we encounter a higher peak
            if (x[j] > peakValue) {
                break;
            }
        }
        
        // Find minimum to the right
        float rightMin = peakValue;
        int rightBase = peakIdx;
        for (int j = peakIdx + 1; j < peakCount; j++) {
            if (x[j] < rightMin) {
                rightMin = x[j];
                rightBase = j;
            }
            // Stop if we encounter a higher peak
            if (x[j] > peakValue) {
                break;
            }
        }
        
        // Prominence is peak height above the higher of the two minima
        float baseLevel = max(leftMin, rightMin);
        prominences[i] = peakValue - baseLevel;
        leftBases[i] = leftBase;
        rightBases[i] = rightBase;
    }
}

// Filter peaks by prominence threshold
void _select_by_prominence(int* peaks, float* prominences, int* leftBases, 
                           int* rightBases, int* peakCount, float minProminence) {
    if (minProminence <= 0) {
        return; // No filtering needed
    }
    
    int newCount = 0;
    for (int i = 0; i < *peakCount; i++) {
        if (prominences[i] >= minProminence) {
            peaks[newCount] = peaks[i];
            prominences[newCount] = prominences[i];
            leftBases[newCount] = leftBases[i];
            rightBases[newCount] = rightBases[i];
            newCount++;
        }
    }
    *peakCount = newCount;
}

// ============================================================================
// Main peak finding function (analogous to scipy.signal.find_peaks)
// ============================================================================

/**
 * Find peaks in a 1D signal with optional distance and prominence constraints
 * 
 * @param x            Input signal array
 * @param length       Length of input signal
 * @param distance     Minimum distance between peaks (0 = no constraint)
 * @param prominence   Minimum prominence threshold (0 = no constraint)
 * @param props        Output structure containing peaks and properties
 * @return             Number of peaks found
 * 
 * Structure follows scipy.signal.find_peaks():
 * 1. Find all local maxima
 * 2. Filter by distance if specified
 * 3. Calculate prominence if needed
 * 4. Filter by prominence if specified
 */
int find_peaks(const float* x, int length, int distance, float prominence, 
               PeakProperties* props) {
    
    // Validate input
    if (distance < 0) {
        Serial.println("ERROR: distance must be >= 0");
        return -1;
    }
    
    // Step 1: Find all local maxima (analogous to _local_maxima_1d)
    props->peakCount = _local_maxima_1d(x, length, props->peaks, MAX_PEAKS);
    
    if (props->peakCount == 0) {
        return 0; // No peaks found
    }
    
    // Step 2: Filter by distance if specified (analogous to _select_by_peak_distance)
    if (distance > 0) {
        _select_by_peak_distance(props->peaks, x, &props->peakCount, distance);
    }
    
    if (props->peakCount == 0) {
        return 0; // All peaks filtered out
    }
    
    // Step 3: Calculate prominence if needed (analogous to _peak_prominences)
    if (prominence > 0) {
        _peak_prominences(x, props->peaks, props->peakCount, 
                         props->prominences, props->leftBases, props->rightBases);
        
        // Step 4: Filter by prominence (analogous to _select_by_property)
        _select_by_prominence(props->peaks, props->prominences, 
                             props->leftBases, props->rightBases, 
                             &props->peakCount, prominence);
    }
    
    return props->peakCount;
}

// ============================================================================
// Convenience function for simple peak detection (most common use case)
// ============================================================================

/**
 * Simplified peak detection with just distance constraint
 * Returns array of peak indices and count
 */
int find_peaks_simple(const float* x, int length, int* peaks, 
                     int maxPeaks, int minDistance) {
    int peakCount = _local_maxima_1d(x, length, peaks, maxPeaks);
    if (minDistance > 0 && peakCount > 1) {
        _select_by_peak_distance(peaks, x, &peakCount, minDistance);
    }
    return peakCount;
}

#endif // DATAPROCESSING_H