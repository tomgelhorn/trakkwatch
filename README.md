# TrakkWatch - ESP32-C3 Smartwatch Firmware

Battery-efficient smartwatch firmware for the Seeed Xiao ESP32-C3 featuring heart rate and HRV monitoring, sleep detection, tiered long-term history, and gesture navigation.

## Features

- **Heart Rate + HRV**: MAX30102 optical sensor, bandpass-filtered peak detection, SDRR-based HRV
- **Sleep Detection**: 2-of-3 vote on low HR, high HRV, and no-motion (BMA400 INT2)
- **Tiered History**: 5-min samples for 24h · 30-min averages for 7d · 2-hour averages for 30d
- **9-Screen Interface**: Dashboard, HR graphs (1h/4h/24h/7d/30d), HRV graphs (7d/30d), sleep summary
- **Gesture Navigation**: Double-tap cycles through all screens (BMA400 INT1)
- **Deep Sleep**: Timer-only wake, 4 min between sessions (9 min when watch is not worn)
- **Battery Display**: Real-time voltage with fill-bar icon

## Hardware

| Component | Part |
|-----------|------|
| MCU | Seeed Xiao ESP32-C3 |
| Display | GDEH0154D67 1.54" e-paper (200×200, SSD1681) |
| Heart Rate | MAX30102 (I2C) |
| IMU | BMA400 (I2C) |
| Battery | ADC A0, 2× voltage divider |

### Pin Configuration
```
Display:  CS=D9, DC=D1, RES=D2, BUSY=D3, SCK=D8, MOSI=D10, PWR=D8
I2C:      SDA=D4, SCL=D5
IMU INT1: D6  (double-tap)
IMU INT2: D7  (no-motion)
Battery:  A0
```

## Architecture

```
src/
  ├── main.cpp          # FreeRTOS task orchestration and sleep management
  ├── DataStorage.h     # Three-tier NVS circular buffer (TieredHRStorage)
  ├── SystemState.h     # RTC memory state and screen management
  ├── Sensors.h         # MAX30102, BMA400, battery, DSP pipeline
  └── DisplayManager.h  # E-paper rendering (all 9 screens)
```

### Data Storage

Three-tier circular buffer in NVS namespace `trakk`:

| Tier | Resolution | Duration | Keys |
|------|-----------|----------|------|
| T1 | 5 min | 24 h (288 entries) | `hr5m`, `hrv5m`, `slp5m` |
| T2 | 30 min | 7 d (336 entries) | `hr30m`, `hrv30m` |
| T3 | 2 h | 30 d (360 entries) | `hr2h`, `hrv2h` |

Total NVS footprint: ~1.6 KB. T1→T2 promoted every 6 entries; T2→T3 every 4 entries.

**RTC memory** (survives deep sleep, lost on power cycle): `currentScreen`, `bootCount`, sleep session counters (`currentSleepState`, `consecutiveSleepCycles`, `lastSleepDurationCycles`).

### Power Management

- **Active session**: 60s (HR measurement + UI, concurrent FreeRTOS tasks)
- **Deep sleep**: 4 min (wrist detected) / 9 min (no wrist)
- **Sleep current**: ~20 µA (ESP32-C3) + ~14 µA (BMA400) + 0 µA (MAX30102 off)
- **Estimated battery life** with 200 mAh: ~5–7 days

## Operation

1. Wake from deep sleep (timer)
2. Initialize sensors and display; show active status badge
3. Run concurrent tasks: HR/HRV measurement + UI rendering
4. Store result in circular buffer; classify sleep state; update display
5. Hibernate display and sensors; enter deep sleep
6. If user taps near session end, extend session by 60s from last tap

### Screens (double-tap to advance)

| Screen | Description |
|--------|-------------|
| Dashboard | Current HR, HRV (SDRR ms), battery |
| HR 1H / 4H / 24H / 7D / 30D | Heart rate history graphs |
| HRV 7D / 30D | SDRR history graphs |
| Sleep Summary | Sleep state, duration, current HR/HRV |

## Building & Flashing

```bash
pio run -e seeed_xiao_esp32c3            # Build
pio run -t upload -e seeed_xiao_esp32c3  # Upload
pio device monitor -b 115200             # Monitor
```

## Performance

| Resource | Usage |
|----------|-------|
| Flash | ~250–300 KB / 4 MB |
| SRAM | ~60–80 KB / 400 KB |
| NVS | ~1.6 KB |
| RTC | ~20 bytes |
| Boot time | 2–3 s |
| HR measurement | 60 s |
| Dashboard render | 0.5–1 s (partial) |
| Graph render | 2–3 s (full) |


## Libraries

| Library | Version |
|---------|---------|
| GxEPD2 | ^1.6.7 |
| SparkFun MAX3010x | ^1.1.2 |
| SparkFun BMA400 | ^1.0.0 |
| Preferences | built-in (ESP32) |
