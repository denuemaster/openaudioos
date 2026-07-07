# Third-party Code

This directory is for external protocol stacks.

## Current target

`third_party/airplay-esp32`

Fetch it with:

```bash
tools/fetch_airplay_esp32.sh
```

Do not mix upstream code directly into OpenAudioOS components.

Use adapter boundaries:

```text
third_party stack
    ↓ decoded PCM
oaos_airplay_adapter_push_pcm_s16_stereo()
    ↓
oaos_audio
```
