# OpenAudioOS M0.3 Patch

This patch refactors M0.2 into ESP-IDF components.

## Apply

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.3-patch.zip
cp -R OpenAudioOS-M0.3-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock
export IDF_COMPONENT_MANAGER=0

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

## Test

- Existing WiFi credentials should still be loaded from NVS
- Open the device IP
- Confirm volume/frequency/start/stop works
- Confirm test tone still plays
