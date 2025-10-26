# ESP32 Compass with ICM-20948

A digital compass project using the ESP32-WROOM-32 microcontroller and Adafruit ICM-20948 9-DOF (Degrees of Freedom) sensor breakout board.

## Hardware Components

- **Microcontroller**: ESP32-WROOM-32
- **Sensor**: Adafruit ICM-20948 9-DOF breakout board
  - 3-axis accelerometer
  - 3-axis gyroscope
  - 3-axis magnetometer (AK09916)
  - Temperature sensor

## Wiring

Connect the ICM-20948 to the ESP32 via I2C:

| ICM-20948 Pin | ESP32 Pin | Description |
|---------------|-----------|-------------|
| VIN           | 3.3V      | Power supply (3.3V) |
| GND           | GND       | Ground |
| SCL           | GPIO 22   | I2C Clock |
| SDA           | GPIO 21   | I2C Data |

**Note**: The ICM-20948 operates at 3.3V. Do not connect to 5V.

## Software Dependencies

This project uses PlatformIO for development. Required libraries:

- `Adafruit ICM20X`
- `Adafruit ICM20948`
- `Adafruit BusIO`
- `Adafruit Unified Sensor`

These are automatically installed by PlatformIO when you build the project.

## Building and Uploading

### Prerequisites

1. Install [PlatformIO](https://platformio.org/install)
2. Install PlatformIO CLI or use PlatformIO IDE (VSCode extension)

### Build the project

```bash
pio run
```

### Upload to ESP32

```bash
pio run --target upload
```

### Monitor serial output

```bash
pio device monitor
```

Or combine upload and monitor:

```bash
pio run --target upload && pio device monitor
```

## Features

The compass application provides:

- **Heading calculation**: Displays the compass heading in degrees (0-360°)
- **Magnetometer readings**: Raw X, Y, Z magnetic field data in microteslas (µT)
- **Accelerometer data**: X, Y, Z acceleration in m/s²
- **Gyroscope data**: X, Y, Z angular velocity in rad/s
- **Temperature**: Sensor temperature in °C

Data is output to the serial monitor at 115200 baud, updating every 500ms.

## Usage

1. Power on the ESP32
2. Open serial monitor at 115200 baud
3. The device will initialize and begin displaying sensor data
4. Hold the sensor flat and rotate to see heading changes
5. Move the sensor away from magnetic interference for accurate readings

## Calibration Notes

For accurate compass readings, you may need to calibrate the magnetometer to account for:

- **Hard iron effects**: Permanent magnetic fields from the device itself
- **Soft iron effects**: Magnetic materials that distort the Earth's magnetic field

Calibration typically involves rotating the sensor in a figure-8 pattern and recording min/max values for each axis.

## Troubleshooting

- **"Failed to find ICM20948 chip"**: Check wiring connections and ensure proper power supply
- **Erratic readings**: Move away from electromagnetic interference (motors, speakers, metal objects)
- **No serial output**: Verify serial monitor baud rate is set to 115200

## License

MIT License - Feel free to modify and use for your projects.
