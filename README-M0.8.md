# OpenAudioOS M0.8 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.8-patch.zip
cp -R OpenAudioOS-M0.8-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock
export IDF_COMPONENT_MANAGER=0

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:
- WebUI shows M0.8
- Test tone still works
- AirPlay placeholder stream switches source to AirPlay and plays 880 Hz tone
- `/api/airplay/status` returns AirPlay foundation state
