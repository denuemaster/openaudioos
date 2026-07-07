# AirPlay Stack Evaluation

## Goal

OpenAudioOS should not implement the full AirPlay/FairPlay/RAOP stack from scratch unless absolutely necessary.

The adapter layer in M0.13 creates a clean boundary:

```text
Third-party AirPlay stack
        ↓ decoded signed 16-bit stereo PCM
oaos_airplay_adapter_push_pcm_s16_stereo()
        ↓
oaos_audio_push_pcm_from_source(OAOS_AUDIO_SOURCE_AIRPLAY)
        ↓
OpenAudioOS audio engine
```

## Candidate 1: Shairport Sync

Pros:
- Mature AirPlay receiver.
- Supports AirPlay and AirPlay 2 on Linux/FreeBSD.
- Has known handling for RAOP, metadata, timing and audio output.

Cons:
- Designed for POSIX/Linux, not ESP-IDF.
- AirPlay 2 mode depends on NQPTP concepts for PTP-style timing.
- Porting effort may be significant.

## Candidate 2: rbouteiller/airplay-esp32

Pros:
- Specifically targets ESP32 / ESP32-S3.
- Claims AirPlay 2 receiver functionality.
- Likely closer to ESP-IDF constraints.

Cons:
- Must review license, code quality, dependencies and stability.
- Must verify compatibility with current iOS versions.
- Must keep third-party code cleanly separated.

## Candidate 3: Custom RAOP subset

Pros:
- Full control.
- Can be tailored to ESP32 memory constraints.

Cons:
- FairPlay, ALAC, timing, pairing and metadata are complex.
- Highest risk and slowest path.

## M0.13 Decision

M0.13 does not select a final third-party stack yet.

It creates:
- `oaos_airplay_adapter`
- third-party boundary
- PCM push contract
- status counters

M0.14 should evaluate `rbouteiller/airplay-esp32` first because it is ESP32-specific.
