# OpenAudioOS M0.7 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.7-patch.zip
cp -R OpenAudioOS-M0.7-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock
export IDF_COMPONENT_MANAGER=0

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:

- WebUI should show M0.7
- Tone plays with Test Tone source selected
- Switching to AirPlay/USB/Spotify placeholders should silence the tone
- Switching back to Test Tone should resume tone
- `/api/status` should show `active_source`
