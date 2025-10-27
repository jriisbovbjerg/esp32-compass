# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project Overview

ESP32-based digital compass using the ICM-20948 9-DOF sensor (accelerometer, gyroscope, magnetometer). This is a PlatformIO/Arduino firmware project targeting the ESP32-WROOM-32 microcontroller.

## Build System: PlatformIO

This project uses PlatformIO, not Arduino IDE directly. All commands use the `pio` CLI.

### Essential Commands

**Build the project:**
```bash
pio run
```

**Upload firmware to ESP32:**
```bash
pio run --target upload
```

**Monitor serial output (115200 baud):**
```bash
pio device monitor
```

**Upload and monitor (common workflow):**
```bash
pio run --target upload && pio device monitor
```

**Clean build artifacts:**
```bash
pio run --target clean
```

## Project Structure

```
esp32-compass/
├── src/
│   └── main.cpp          # Single-file application entry point
├── platformio.ini        # Build configuration, libraries, board settings
├── include/              # Empty - for future header files
├── lib/                  # Empty - for custom libraries
└── test/                 # Empty - for unit tests
```

## Hardware Configuration

- **Board:** ESP32-WROOM-32 (`esp32dev` in platformio.ini)
- **Sensor:** Adafruit ICM-20948 via I2C
- **I2C Pins:** SDA=GPIO21, SCL=GPIO22 (hardcoded in main.cpp)
- **Serial Baud:** 115200
- **Power:** 3.3V only (sensor cannot tolerate 5V)

## Code Architecture

### Single-File Design
The entire application is in `src/main.cpp` with:
- `setup()`: Initializes Serial, I2C, ICM20948 sensor, configures ranges
- `loop()`: Reads sensor events, calculates heading via `atan2()`, prints data every 500ms

### Key Concepts
- **Heading calculation:** Uses magnetometer X/Y components with `atan2()` for 0-360° compass bearing
- **Sensor ranges:** Accelerometer set to ±4G, Gyroscope to 500°/s, Magnetometer to 10Hz
- **No calibration:** Current implementation lacks hard/soft iron compensation (mentioned in README but not implemented)

## Dependencies

Defined in `platformio.ini` under `lib_deps`:
- `Adafruit ICM20X` (v2.0.5)
- `Adafruit ICM20948` (v1.0.5)
- `Adafruit BusIO` (v1.14.1)
- `Adafruit Unified Sensor` (v1.1.9)

PlatformIO automatically downloads these on first build.

## Development Notes

- **No tests:** The `test/` directory is empty. Hardware-in-loop testing requires physical device.
- **No linting/formatting configured:** No `.clang-format` or similar tools in project.
- **Debug level:** Set to 0 in platformio.ini (`-DCORE_DEBUG_LEVEL=0`)
- **Upload speed:** 921600 baud for fast flashing

## Common Modifications

When adding features:
1. Keep code in `main.cpp` for simplicity, or split into `include/` if complexity grows
2. Add new libraries to `lib_deps` in `platformio.ini`
3. Remember I2C pins are fixed (21/22) - change `I2C_SDA`/`I2C_SCL` defines if needed
4. Serial output uses `Serial.printf()` - maintain consistent formatting

## Troubleshooting Hardware

- **"Failed to find ICM20948 chip":** Check wiring (especially SDA/SCL), verify 3.3V power
- **Erratic compass readings:** Sensor needs distance from magnetic interference; may need calibration
- **No serial output:** Confirm 115200 baud rate in monitor
