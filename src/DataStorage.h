#ifndef DATASTORAGE_H
#define DATASTORAGE_H

#include <Preferences.h>

#define HR_HISTORY_SIZE 48  // 4 hours at 5-minute intervals

class HRHistoryBuffer {
private:
    Preferences prefs;
    uint8_t hrValues[HR_HISTORY_SIZE];
    uint8_t currentIndex;
    uint32_t lastUpdateTime;
    bool initialized;

public:
    HRHistoryBuffer() : currentIndex(0), lastUpdateTime(0), initialized(false) {
        memset(hrValues, 0, HR_HISTORY_SIZE);
    }

    // Initialize and load from NVS
    bool begin() {
        if (!prefs.begin("hrwatch", false)) {
            Serial.println("Failed to initialize Preferences");
            return false;
        }
        
        // Load stored data
        currentIndex = prefs.getUChar("idx", 0);
        lastUpdateTime = prefs.getULong("lasttime", 0);
        
        // Load HR history array
        size_t len = prefs.getBytes("hrdata", hrValues, HR_HISTORY_SIZE);
        if (len != HR_HISTORY_SIZE) {
            // First boot or corrupted data - initialize to zeros
            Serial.println("Initializing new HR history buffer");
            memset(hrValues, 0, HR_HISTORY_SIZE);
            currentIndex = 0;
            lastUpdateTime = 0;
            save();  // Save clean state
        } else {
            Serial.printf("Loaded HR history: idx=%d, count=%d\n", currentIndex, getCount());
        }
        
        initialized = true;
        return true;
    }

    // Add a new heart rate measurement
    void addMeasurement(uint8_t hr) {
        if (!initialized) return;
        
        hrValues[currentIndex] = hr;
        currentIndex = (currentIndex + 1) % HR_HISTORY_SIZE;
        lastUpdateTime = millis();
        
        save();
        
        Serial.printf("Added HR=%d at index %d\n", hr, (currentIndex - 1 + HR_HISTORY_SIZE) % HR_HISTORY_SIZE);
    }

    // Get all measurements in chronological order (oldest to newest)
    // Returns actual count of valid measurements (non-zero values)
    uint8_t getMeasurements(uint8_t* buffer, uint8_t maxCount) {
        if (!initialized || maxCount < HR_HISTORY_SIZE) return 0;
        
        // Copy from circular buffer in chronological order
        for (uint8_t i = 0; i < HR_HISTORY_SIZE; i++) {
            uint8_t idx = (currentIndex + i) % HR_HISTORY_SIZE;
            buffer[i] = hrValues[idx];
        }
        
        return HR_HISTORY_SIZE;
    }

    // Get count of valid measurements (non-zero)
    uint8_t getCount() {
        uint8_t count = 0;
        for (uint8_t i = 0; i < HR_HISTORY_SIZE; i++) {
            if (hrValues[i] > 0) count++;
        }
        return count;
    }

    // Check if we have a full 4 hours of data
    bool hasFullHistory() {
        return getCount() >= HR_HISTORY_SIZE;
    }

    // Clear all data
    void clear() {
        memset(hrValues, 0, HR_HISTORY_SIZE);
        currentIndex = 0;
        lastUpdateTime = 0;
        save();
        Serial.println("HR history cleared");
    }

    // Get last update time
    uint32_t getLastUpdateTime() {
        return lastUpdateTime;
    }

private:
    // Save current state to NVS
    void save() {
        if (!initialized) return;
        
        prefs.putUChar("idx", currentIndex);
        prefs.putULong("lasttime", lastUpdateTime);
        prefs.putBytes("hrdata", hrValues, HR_HISTORY_SIZE);
    }
};

#endif // DATASTORAGE_H
