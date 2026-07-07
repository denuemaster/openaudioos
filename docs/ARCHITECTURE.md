# OpenAudioOS Architecture

OpenAudioOS is designed as an embedded audio platform for ESP32 devices.

## Milestones

## M0
Hardware validation:
- ESP32-S3 boot
- WiFi
- HTTP status page
- I2S
- PCM5102A test tone

## M1
Core infrastructure:
- configurable WiFi
- captive portal
- OTA
- NVS settings
- structured logging

## M2
Audio engine:
- ring buffer
- routing
- gain
- volume
- sample format conversion

## M3
AirPlay baseline:
- reuse mature reverse-engineered open-source implementations where license permits
- RAOP
- ALAC
- mDNS/Bonjour
- buffering

## M4
AirPlay 2 priority:
- integrate existing reverse-engineered AirPlay 2 code where legally and technically possible
- clock sync
- pairing
- multi-room capable architecture

## M5
USB Audio:
- USB Audio Device via ESP32-S3 OTG
- future USB Audio Host
