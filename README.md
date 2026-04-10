# TrakkWatch - ESP32-C3 Smartwatch Firmware

A battery-efficient smartwatch implementation for the Xiao ESP32C3 featuring heart rate monitoring, battery voltage display, 4-hour historical data tracking, and gesture-based navigation while awake.

## Features

### Core Functionality
- **Heart Rate Monitoring**: Continuous monitoring using MAX30102 optical sensor
- **Battery Voltage Display**: Real-time battery level with visual indicator
- **4-Hour History Graph**: Rolling buffer storing 48 HR measurements (5-minute intervals)
- **Dual-Screen Interface**: Dashboard and graph views
- **Gesture Navigation**: Double-tap to switch screens (BMA400 IMU)
- **Deep Sleep Power Management**: Timer-only wake with 1-minute active + 4-minute sleep baseline

### Hardware Components
- **MCU**: Seeed Xiao ESP32-C3 (160 MHz RISC-V, 400 KB RAM)
- **Display**: GDEH0154D67 1.54" e-paper (200×200 pixels, SSD1681 controller)
- **Heart Rate**: MAX30102 optical sensor (I2C)
- **IMU**: BMA400 accelerometer with tap detection (I2C)
- **Battery**: Voltage monitoring via ADC pin A0 (2x divider)

### Pin Configuration
```
Display:
  CS:   D9 (GPIO9)
  DC:   D1 (GPIO1)
  RES:  D2 (GPIO2)
  BUSY: D3 (GPIO3)
  SPI:  D8=SCK (GPIO8), D10=MOSI (GPIO10)
  PWR:  GPIO8

I2C Sensors (shared bus):
  SDA:  D4 (GPIO6)
  SCL:  D5 (GPIO7)
  
IMU Interrupt: D6 (GPIO6)
Battery ADC:   A0
```

## Architecture

### File Structure
```
src/
  ├── main.cpp              # Main firmware loop and state machine
  ├── DataStorage.h         # NVS persistence for HR history
  ├── SystemState.h         # RTC memory for wake state tracking
  ├── Sensors.h             # MAX30102, BMA400, battery integration
  └── DisplayManager.h      # E-paper rendering (dashboard & graph)
```

### Data Storage Strategy
- **NVS Flash (Preferences)**: 48-point circular buffer (~50 bytes)
  - Survives deep sleep and power cycles
  - Keys: `hrdata`, `idx`, `lasttime` in namespace `hrwatch`
- **RTC Memory**: Transient state (screen mode, graph timer)
  - Variables: `currentScreen`, `graphViewStartTime`, `bootCount`
  - Lost on power cycle, persists through deep sleep

### Power Management
- **Active Cycle**: ~10-15 seconds every 5 minutes
  - 10s HR measurement
  - 1s battery reading
  - 2-3s display update
- **Deep Sleep**: ~4 minutes 45 seconds
  - ESP32-C3 at ~20 µA
  - BMA400 active for tap detection (~14 µA)
  - MAX30102 shut down (0 µA)
- **Estimated Battery Life**: 
  - With 200 mAh battery: ~5-7 days
  - Depends on battery capacity and tap usage

## Operation

### Wake Behavior

#### Timer Wake (Periodic Cycle)
1. Wake from deep sleep
2. Initialize sensors and display
3. Start 1-minute active session
4. Measure heart rate while allowing double-tap screen navigation
5. Store HR in circular buffer when valid
6. Return to deep sleep for 4 minutes
7. If user is still navigating near session end, extend awake time and sleep 60 seconds after the last double tap

### Display Screens

#### Dashboard
- **Large HR Display**: Current BPM (or "--" if no reading)
- **Battery Indicator**: Icon + voltage (top-right)
- **Status Message**: "Building history..." for first 48 measurements
- **Instructions**: "Double-tap for graph"
- **Refresh**: Partial update (~0.5s)

#### Graph View
- **X-axis**: 4 hours (48 data points)
- **Y-axis**: 40-180 BPM range
- **Gridlines**: Every 20 BPM
- **Plot**: Connected line graph with 2px circles
- **Instructions**: "Double-tap to return"
- **Refresh**: Full update (~2s)

## Building & Flashing

### Prerequisites
- PlatformIO IDE (VS Code extension)
- USB-C cable for Xiao ESP32C3

### Build Commands
```bash
# Build firmware
pio run -e seeed_xiao_esp32c3

# Upload to device
pio run -t upload -e seeed_xiao_esp32c3

# Monitor serial output
pio device monitor -b 115200

# Upload and monitor
pio run -t upload && pio device monitor -b 115200
```

### VS Code Tasks
- **Build**: `Ctrl+Alt+B` → "PlatformIO: Build"
- **Upload**: `Ctrl+Alt+U` → "PlatformIO: Upload"
- **Monitor**: `Ctrl+Alt+S` → "PlatformIO: Serial Monitor"

## Testing & Validation

### First Boot Test
1. Flash firmware and connect serial monitor
2. Verify initialization messages:
   ```
   TrakkWatch Starting...
   Wake: Power-on boot (count=1)
   MAX30102 initialized
   BMA400 initialized with double-tap detection
   Display initialized (200x200)
   ```
3. Place finger on MAX30102 sensor for 10 seconds
4. Verify HR measurement (typical: 60-100 BPM)
5. Check dashboard displays HR and battery voltage
6. Confirm "Building history..." message appears

### Measurement Cycle Test
1. Wait for device to enter deep sleep (~5 min)
2. Device should wake automatically and measure
3. Verify serial output shows wake reason: "Timer"
4. Check HR value stored in history
5. Repeat until 48 measurements collected (4 hours)

### Gesture Navigation Test
1. Wait for timer wake (device must already be awake)
2. Perform firm double-tap on device
3. Verify screen switches in cycle (Dashboard -> Graph -> Sleep Summary)
4. Confirm heart-rate measurement continues while switching screens
5. Tap near end of the 1-minute session and verify sleep waits 60 seconds after the last tap

### Battery Monitoring Test
1. Measure actual battery voltage with multimeter
2. Compare with displayed voltage (should be within ±0.1V)
3. Test full range (3.6V to 4.2V if using Li-Po)
4. Verify battery icon fills proportionally

### Persistence Test
1. Remove power from device completely
2. Reconnect power
3. Verify history data survives (check serial: "Loaded HR history")
4. Confirm graph shows data from before power cycle

### Deep Sleep Current Test
1. Use µA-capable multimeter on VCC line
2. Measure during deep sleep period
3. Expected: 15-30 µA total
   - ESP32-C3: ~20 µA
   - BMA400: ~14 µA
   - MAX30102: 0 µA (shut down)

## Troubleshooting

### No Heart Rate Detected
- **Symptom**: HR always shows "--" or 0
- **Causes**:
  1. No finger on sensor (IR value < 50000)
  2. Poor skin contact (try moistening finger)
  3. MAX30102 not initialized (check I2C connection)
- **Fix**: Ensure firm, steady pressure on sensor

### Double-Tap Not Working
- **Symptom**: Screen doesn't switch on tap
- **Causes**:
  1. Tap too soft (increase tap force)
  2. Wrong axis (BMA400 configured for Z-axis only)
  3. Interrupt pin not connected (check D6 wiring)
- **Fix**: Tap harder and perpendicular to display surface

### Display Ghost Images
- **Symptom**: Previous screen elements visible
- **Causes**:
  1. Too many partial refreshes without full refresh
- **Fix**: Power cycle or wait for next full refresh (graph view)

### History Data Lost
- **Symptom**: Graph empty after power cycle
- **Causes**:
  1. NVS flash not initialized (check Preferences errors in serial)
  2. Flash corruption
- **Fix**: Reflash firmware or call `hrHistory.clear()` once

### High Battery Drain
- **Symptom**: Battery depletes in <2 days
- **Causes**:
  1. Not entering deep sleep (check serial for "Entering Deep Sleep")
  2. Frequent tap wakes (check bootCount increments slowly)
  3. MAX30102 not shutting down
- **Fix**: Verify `shutdownSensors()` called before sleep

## Customization

### Adjust Measurement Interval
Edit `main.cpp`:
```cpp
// Change to 10 minutes (600 seconds)
#define SLEEP_INTERVAL_US (10ULL * 60ULL * 1000000ULL)

// Update history size accordingly (10 min intervals = 24 points for 4h)
// In DataStorage.h:
#define HR_HISTORY_SIZE 24
```

### Change Graph Auto-Return Timeout
Edit `SystemState.h`:
```cpp
if (elapsed > 120000) {  // 120 seconds (2 minutes)
```

### Modify Display Orientation
Edit `DisplayManager.h`:
```cpp
display.setRotation(3);  // 0=0°, 1=90°, 2=180°, 3=270°
```

### Adjust Beat Detection Sensitivity
Edit `Sensors.h`:
```cpp
static const int BEAT_THRESHOLD = 200;  // Lower = more sensitive
static const long MIN_BEAT_INTERVAL = 300;  // Allow up to 200 BPM
```

## Performance Metrics

### Memory Usage
- **Flash**: ~250-300 KB (out of 4 MB)
- **SRAM**: ~60-80 KB (out of 400 KB)
- **NVS**: 50 bytes (HR history)
- **RTC**: 12 bytes (state variables)

### Timing
- **Full boot**: 2-3 seconds
- **HR measurement**: 10 seconds (configurable)
- **Dashboard render**: 0.5-1 seconds (partial refresh)
- **Graph render**: 2-3 seconds (full refresh)
- **Deep sleep entry**: <100 ms

## Known Limitations

1. **No Real-Time Clock**: Time tracking uses relative sleep cycles
2. **Single User**: No multi-user profiles
3. **No Step Counting**: BMA400 capable but not implemented
4. **Limited History**: 4 hours only (~50 bytes storage)
5. **Manual Time Sync**: No WiFi/NTP time sync
6. **Monochrome Display**: E-paper is black & white only
7. **Fixed Intervals**: 5-minute measurement intervals (adjustable in code)

## Future Enhancements

### Possible Additions
- [ ] Step counting using BMA400 built-in pedometer
- [ ] Sleep quality analysis (HR variability during night)
- [ ] Low battery warning and shutdown
- [ ] WiFi sync to cloud (upload history)
- [ ] Multiple graph time ranges (1h, 4h, 24h)
- [ ] Blood oxygen (SpO2) using MAX30102 red+IR
- [ ] Alarm/notification vibration motor
- [ ] RTC module (DS3231) for accurate timekeeping
- [ ] E-paper optimization (reduce ghost images)
- [ ] LVGL GUI for more complex screens

## License & Credits

### Libraries Used
- **GxEPD2** (v1.6.7): E-paper display driver
- **SparkFun MAX3010x** (v1.1.2): Heart rate sensor
- **SparkFun BMA400** (v1.0.0): IMU accelerometer
- **Preferences**: ESP32 NVS storage (built-in)

### Hardware Design
- Seeed Studio: Xiao ESP32C3 board
- Waveshare/Good Display: GDEH0154D67 e-paper
- Maxim Integrated: MAX30102 sensor
- Bosch Sensortec: BMA400 IMU

### Development
Created for embedded smartwatch applications using PlatformIO and Arduino framework.

## Support

For issues, feature requests, or contributions:
1. Check troubleshooting section above
2. Enable debug output (115200 baud serial)
3. Verify wiring matches pin configuration
4. Test individual components using `working_code/` examples
