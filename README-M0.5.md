# OpenAudioOS M0.5 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.5-patch.zip
cp -R OpenAudioOS-M0.5-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock
export IDF_COMPONENT_MANAGER=0

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:

- Device should boot in STA-only mode if WiFi is already saved
- WebUI should no longer claim mDNS/local URL
- `/api/status` should show OTA partition status
- Audio controls should still work
- OTA upload should accept `build/OpenAudioOS.bin`
