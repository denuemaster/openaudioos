# Porting rbouteiller/airplay-esp32 into OpenAudioOS

## Why this candidate

`rbouteiller/airplay-esp32` is ESP32-focused and targets AirPlay 2 receivers on ESP32/ESP32-S3 devices. It is therefore a better first candidate than porting a Linux-first project such as Shairport Sync.

## M0.14 scope

M0.14 does not compile the third-party stack yet.

It adds:

- `oaos_airplay_backend`
- selects `rbouteiller/airplay-esp32` as first candidate
- `tools/fetch_airplay_esp32.sh`
- adapter boundary for PCM output

## Integration target

The third-party AirPlay code should eventually call:

```c
oaos_airplay_adapter_claim_source();
oaos_airplay_adapter_push_pcm_s16_stereo(frames, frame_count, timeout_ms);
oaos_airplay_adapter_release_source();
```

## Porting steps for M0.15+

1. Fetch third-party code.
2. Identify the audio output callback or I2S write path.
3. Disable direct I2S output in the third-party stack.
4. Redirect decoded PCM into `oaos_airplay_adapter_push_pcm_s16_stereo()`.
5. Decide ownership of:
   - mDNS advertisement
   - RTSP socket
   - FairPlay setup
   - RTP sockets
   - ALAC/AAC decoder
   - clock sync
6. Avoid duplicate services:
   - our internal `oaos_airplay` currently advertises `_raop` and `_airplay`
   - real backend may need to own those services instead
7. Keep upstream code separate.

## Important boundary decision

Once the third-party backend owns real AirPlay, our internal `oaos_airplay` component should become either:

- status facade only, or
- disabled when backend is active

We must avoid two RAOP services fighting for port 5000.
