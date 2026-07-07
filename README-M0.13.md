# OpenAudioOS M0.13 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.13-patch.zip
cp -R OpenAudioOS-M0.13-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build managed_components dependencies.lock sdkconfig sdkconfig.old

unset IDF_COMPONENT_MANAGER

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:

- Existing AirPlay discovery should still work
- Existing WebUI should still work
- Existing placeholder should still work
- Logs should show `AirPlay adapter initialized`

M0.13 does not yet integrate third-party AirPlay code.
