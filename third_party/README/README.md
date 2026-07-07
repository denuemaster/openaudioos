# Third-party Integration Area

This directory is reserved for vendored or submodule-based protocol implementations.

Rules:

- Keep upstream code separate from OpenAudioOS-owned components.
- Preserve upstream licenses.
- Document the upstream commit hash.
- Add a wrapper/adapter instead of modifying upstream code heavily.
- Route decoded PCM into `oaos_airplay_adapter_push_pcm_s16_stereo()`.

Initial candidates:

- `mikebrady/shairport-sync` for mature RAOP/AirPlay work.
- `rbouteiller/airplay-esp32` for ESP32-focused AirPlay 2 work.
- `nqptp` concepts for AirPlay 2 clock sync if needed.
