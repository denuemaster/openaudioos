# OpenAudioOS M0.10 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.10-patch.zip
cp -R OpenAudioOS-M0.10-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build managed_components dependencies.lock sdkconfig sdkconfig.old

unset IDF_COMPONENT_MANAGER

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:

- Serial log should show both RAOP and AirPlay mDNS advertised
- WebUI should show M0.10
- `/api/airplay/status` should show `raop_advertised` and `airplay_advertised`
- On iPhone/macOS, check whether OpenAudioOS appears or whether RTSP requests are logged

This is still not playable AirPlay audio.
