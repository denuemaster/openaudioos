# OpenAudioOS M0.4 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.4-patch.zip
cp -R OpenAudioOS-M0.4-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock
export IDF_COMPONENT_MANAGER=0

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:

- Device should boot in STA-only mode if WiFi is already saved
- Setup AP should only appear if WiFi config is missing
- Open `http://openaudioos.local` or the IP address
- Test audio controls
