# OpenAudioOS M0.11 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.11-patch.zip
cp -R OpenAudioOS-M0.11-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build managed_components dependencies.lock sdkconfig sdkconfig.old

unset IDF_COMPONENT_MANAGER

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:

- Select OpenAudioOS from iPhone AirPlay output
- ESP32 should not reboot
- Watch serial log for next RTSP method after OPTIONS
