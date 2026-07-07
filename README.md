# OpenAudioOS M0

Minimal ESP-IDF 5.3.x prototype for ESP32-S3 N16R8 + PCM5102A.

## Current Features

- Hardcoded WiFi
- HTTP status page
- I2S output
- PCM5102A test tone
- Serial logging
- No external components
- No ESP-IDF Component Manager dependency
- No custom partition table

## Hardware

| ESP32-S3 | PCM5102A |
|---|---|
| 5V | VIN |
| GND | GND |
| GPIO11 | BCK |
| GPIO12 | DIN |
| GPIO13 | LCK / LRCK |

Important: Use real ESP32 GND to PCM5102A GND. Do not use a GPIO as ground.

## WiFi

SSID: `WiFi2`  
Password: `thermi555`

## Build

```bash
. ~/esp/esp-idf/export.sh
cd OpenAudioOS-M0

rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock
export IDF_COMPONENT_MANAGER=0

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Use your real serial port if different:

```bash
ls /dev/cu.*
```

## Browser

After boot, check the serial monitor for the IP address and open:

```text
http://<ip-address>/
```

## Expected serial output

You should see:

```text
OpenAudioOS M0 starting
WiFi connected
Got IP
HTTP server started
I2S initialized
Audio test tone started
```
